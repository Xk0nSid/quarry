/* quarry.h

   In exactly ONE .c file:
       #define QUARRY_IMPLEMENTATION
       #include "quarry.h"

   ALLOCATORS
       qry_Allocator is {proc, ctx}. A zero-initialized one ({0}) is the heap.

       qry_Allocator heap = {0};
       int *p = QRY_NEW_ARRAY(heap, int, 100);   // may be NULL
       qry_free(heap, p, 100 * sizeof *p);

       qry_Arena *arena = qry_arena_new(heap, 0);          // 64 KiB blocks from
   the heap qry_Allocator al = qry_arena_allocator(arena); char *buf =
   qry_alloc(al, 256);                     // no per-allocation free needed buf
   = qry_resize(al, buf, 256, 1024);               // grows in place if it is
   the newest qry_arena_reset(arena);                             // reuse the
   memory qry_arena_free(arena);                              // release
   everything

       To write your own allocator, implement qry_AllocProc (contract in the
       allocators section below) and wrap it as {proc, ctx}.

   ARRAYS
       int *a = NULL;            // an empty array is just NULL
       qry_arr_push(a, 42);
       for (size_t i = 0; i < qry_arr_len(a); i++) printf("%d\n", a[i]);
       qry_arr_free(a);          // frees and sets a = NULL

   STRINGS
       qry_Str  : view = pointer + length. Passed by value, never owns memory.
       char *sb : builder = a dynamic array of char, kept NUL-terminated
                  (after any qry_sb_* call), so it can be passed to libc.

       char *sb = NULL;
       qry_sb_append(&sb, QRY_S("hello"));
       qry_sb_printf(&sb, " %d", 42);
       printf("%s\n", sb);                        // hello 42
       qry_Str v = qry_sb_view(sb);
       printf(QRY_STR_FMT "\n", QRY_STR_ARG(v));
       qry_arr_free(sb);

       qry_Str line = QRY_S("a,b,c");
       while (line.len) {
           qry_Str f = qry_str_chop(&line, ',');  // "a", "b", "c"
       }

   Notes:
       - An array/builder pointer can move when it grows, so keep a single copy
         of it (pass &sb or return it when a function needs to modify it).
       - A view into a builder is invalidated when the builder grows.
       - DO NOT append a view that points into the same builder.
       - Macro arguments may be evaluated more than once: no side effects in
   them.
       - Out of memory aborts.
*/
#ifndef QUARRY_H
#define QUARRY_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* -------------------------------------------------------------- allocators */

/* An allocator is a function pointer plus a context pointer. One proc handles
   every operation. The contract for each op:

     QRY_OP_ALLOC     ptr == NULL, old_size == 0. Return a block of new_size bytes
                      aligned to `align`, or NULL. Memory is NOT zeroed.
     QRY_OP_RESIZE    ptr/old_size describe a live block from this allocator (ptr may
                      be NULL, meaning alloc). Return a block of at least new_size
                      bytes whose first min(old_size, new_size) bytes match the old
                      block. The result may equal ptr (grown in place) or differ (the
                      allocator copied). On failure return NULL and leave ptr valid.
     QRY_OP_FREE      release ptr (old_size is its size). May be a no-op (arenas).
     QRY_OP_FREE_ALL  release everything this allocator handed out (arenas, pools).
                      A no-op for the heap.

   `align` is a power of two. Allocators return NULL on failure and never abort;
   the caller decides what failure means. */

typedef enum {
    QRY_OP_ALLOC,
    QRY_OP_RESIZE,
    QRY_OP_FREE,
    QRY_OP_FREE_ALL
} qry_Op;

typedef void *(*qry_AllocProc)(void *ctx, qry_Op op, void *ptr, size_t old_size,
                               size_t new_size, size_t align);

typedef struct qry_Allocator {
    qry_AllocProc proc; /* NULL means "use the heap" */
    void *ctx;
} qry_Allocator;

#define QRY_DEFAULT_ALIGN 16

/* All of these accept a zero-initialized qry_Allocator ({0}) as the heap. */
void *qry_alloc_align(qry_Allocator a, size_t size, size_t align);
void *qry_alloc(qry_Allocator a, size_t size);      /* align 16 */
void *qry_alloc_zero(qry_Allocator a, size_t size); /* align 16, zeroed */
void *qry_alloc_array(qry_Allocator a, size_t elem_size,
                      size_t count); /* overflow-checked */
void *qry_resize_align(qry_Allocator a, void *ptr, size_t old_size,
                       size_t new_size, size_t align);
void *qry_resize(qry_Allocator a, void *ptr, size_t old_size, size_t new_size);
void qry_free(qry_Allocator a, void *ptr, size_t size);
void qry_free_all(qry_Allocator a);

#define QRY_NEW(a, T) ((T *)qry_alloc((a), sizeof(T)))
#define QRY_NEW_ARRAY(a, T, n) ((T *)qry_alloc_array((a), sizeof(T), (n)))

/* malloc/realloc/free. Supports alignment up to 16. */
qry_Allocator qry_heap(void);

/* Arena: grows by chaining blocks obtained from a backing allocator.
   Allocations bigger than a block get their own block.
   - RESIZE extends (or shrinks) the most recent allocation in place when it
   fits.
   - FREE of the most recent allocation gives its bytes back; any other FREE is
   a no-op.
   - FREE_ALL / qry_arena_reset keeps one block for reuse. */
typedef struct qry_Arena qry_Arena;

/* block_size 0 = 64 KiB. Returns NULL if the backing allocator fails. */
qry_Arena *qry_arena_new(qry_Allocator backing, size_t block_size);
void qry_arena_reset(qry_Arena *a);
void qry_arena_free(qry_Arena *a); /* releases all blocks and the arena */
qry_Allocator qry_arena_allocator(qry_Arena *a);

/* ----------------------------------------------------------------- arrays */

typedef struct {
    size_t len, cap;
} qry_ArrHdr;

/* header size rounded to 16 so elements stay well aligned */
#define QRY_HDR_SIZE ((sizeof(qry_ArrHdr) + 15u) & ~(size_t)15u)
#define qry__hdr(a) ((qry_ArrHdr *)((char *)(a) - QRY_HDR_SIZE))

void *qry__arr_grow(void *a, size_t elem_size, size_t add);

#define qry_arr_len(a) ((a) ? qry__hdr(a)->len : (size_t)0)
#define qry_arr_cap(a) ((a) ? qry__hdr(a)->cap : (size_t)0)
#define qry_arr_last(a) ((a)[qry__hdr(a)->len - 1])

/* make room for n more elements */
#define qry_arr_reserve(a, n)                                                  \
    ((!(a) || qry__hdr(a)->len + (n) > qry__hdr(a)->cap)                       \
         ? (void)((a) = qry__arr_grow((a), sizeof *(a), (n)))                  \
         : (void)0)

#define qry_arr_push(a, v)                                                     \
    (qry_arr_reserve(a, 1), (a)[qry__hdr(a)->len++] = (v))

#define qry_arr_pop(a) ((a)[--qry__hdr(a)->len])
#define qry_arr_clear(a) ((void)((a) ? (qry__hdr(a)->len = 0) : 0))
#define qry_arr_free(a) ((void)((a) ? (free(qry__hdr(a)), (a) = NULL) : NULL))

/* ---------------------------------------------------------------- strings */

typedef struct {
    const char *ptr;
    size_t len;
} qry_Str;

/* view of a string literal, no strlen. Literals only. */
#define QRY_S(lit) ((qry_Str){(lit), sizeof(lit) - 1})

/* printf helpers: printf(QRY_STR_FMT, QRY_STR_ARG(s)) */
#define QRY_STR_FMT "%.*s"
#define QRY_STR_ARG(s) (int)(s).len, (s).ptr

#if defined(__GNUC__) || defined(__clang__)
#define QRY__PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define QRY__PRINTF(f, a)
#endif

/* views (a view with ptr == NULL and len == 0 is a valid empty string) */
qry_Str qry_str(const char *cstr); /* uses strlen */
qry_Str qry_str_from(const char *p, size_t len);
int qry_str_eq(qry_Str a, qry_Str b);
int qry_str_starts_with(qry_Str s, qry_Str prefix);
ptrdiff_t qry_str_find(qry_Str s, qry_Str needle);          /* index, or -1 */
qry_Str qry_str_slice(qry_Str s, size_t start, size_t end); /* clamps */
qry_Str qry_str_trim(qry_Str s); /* ASCII whitespace */
qry_Str qry_str_chop(qry_Str *s,
                     char delim); /* text before delim; *s advances past it */

/* builder (char * dynamic array, always NUL-terminated after these calls) */
void qry_sb_append(char **sb, qry_Str s);
void qry_sb_putc(char **sb, char c);
void qry_sb_printf(char **sb, const char *fmt, ...) QRY__PRINTF(2, 3);
qry_Str qry_sb_view(const char *sb);

#endif /* QUARRY_H */

#ifdef QUARRY_IMPLEMENTATION

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef QRY_ASSERT
#include <assert.h>
#define QRY_ASSERT(x) assert(x)
#endif

/* -------------------------------------------------------------- allocators */

static void *qry__heap_proc(void *ctx, qry_Op op, void *ptr, size_t old_size,
                            size_t new_size, size_t align) {
    (void)ctx;
    (void)old_size;
    switch (op) {
    case QRY_OP_ALLOC:
        if (align > QRY_DEFAULT_ALIGN)
            return NULL;
        return malloc(new_size ? new_size : 1);
    case QRY_OP_RESIZE:
        if (align > QRY_DEFAULT_ALIGN)
            return NULL;
        return realloc(ptr, new_size ? new_size : 1);
    case QRY_OP_FREE:
        free(ptr);
        return NULL;
    case QRY_OP_FREE_ALL:
        return NULL;
    }
    return NULL;
}

qry_Allocator qry_heap(void) {
    qry_Allocator a;
    a.proc = qry__heap_proc;
    a.ctx = NULL;
    return a;
}

static qry_AllocProc qry__proc(qry_Allocator a) {
    return a.proc ? a.proc : qry__heap_proc;
}

void *qry_alloc_align(qry_Allocator a, size_t size, size_t align) {
    QRY_ASSERT(align && (align & (align - 1)) == 0);
    return qry__proc(a)(a.ctx, QRY_OP_ALLOC, NULL, 0, size, align);
}

void *qry_alloc(qry_Allocator a, size_t size) {
    return qry_alloc_align(a, size, QRY_DEFAULT_ALIGN);
}

void *qry_alloc_zero(qry_Allocator a, size_t size) {
    void *p = qry_alloc(a, size);
    if (p)
        memset(p, 0, size);
    return p;
}

void *qry_alloc_array(qry_Allocator a, size_t elem_size, size_t count) {
    if (elem_size && count > SIZE_MAX / elem_size)
        return NULL;
    return qry_alloc(a, elem_size * count);
}

void *qry_resize_align(qry_Allocator a, void *ptr, size_t old_size,
                       size_t new_size, size_t align) {
    QRY_ASSERT(align && (align & (align - 1)) == 0);
    return qry__proc(a)(a.ctx, QRY_OP_RESIZE, ptr, old_size, new_size, align);
}

void *qry_resize(qry_Allocator a, void *ptr, size_t old_size, size_t new_size) {
    return qry_resize_align(a, ptr, old_size, new_size, QRY_DEFAULT_ALIGN);
}

void qry_free(qry_Allocator a, void *ptr, size_t size) {
    if (ptr)
        qry__proc(a)(a.ctx, QRY_OP_FREE, ptr, size, 0, 0);
}

void qry_free_all(qry_Allocator a) {
    qry__proc(a)(a.ctx, QRY_OP_FREE_ALL, NULL, 0, 0, 0);
}

/* ------------------------------------------------------------------ arena */

typedef struct qry__Block qry__Block;
struct qry__Block {
    qry__Block *next;
    size_t cap; /* usable bytes after the (16-aligned) block header */
    size_t used;
};
#define QRY__BLOCK_HDR ((sizeof(qry__Block) + 15u) & ~(size_t)15u)

struct qry_Arena {
    qry_Allocator backing;
    qry__Block *head; /* block currently being filled */
    size_t block_size;
};

static void *qry__block_take(qry__Block *b, size_t size, size_t align) {
    uintptr_t base = (uintptr_t)b + QRY__BLOCK_HDR;
    uintptr_t cur = base + b->used;
    uintptr_t p = (cur + (align - 1)) & ~(uintptr_t)(align - 1);
    size_t used = (size_t)(p - base) + size;
    if (used < size || used > b->cap)
        return NULL;
    b->used = used;
    return (void *)p;
}

static qry__Block *qry__block_new(qry_Arena *a, size_t cap) {
    qry__Block *b;
    if (cap > SIZE_MAX - QRY__BLOCK_HDR)
        return NULL;
    b = (qry__Block *)qry_alloc_align(a->backing, QRY__BLOCK_HDR + cap, 16);
    if (!b)
        return NULL;
    b->next = NULL;
    b->cap = cap;
    b->used = 0;
    return b;
}

static void *qry__arena_alloc(qry_Arena *a, size_t size, size_t align) {
    size_t need;
    qry__Block *b;
    void *p;

    if (a->head && (p = qry__block_take(a->head, size, align)) != NULL)
        return p;

    need = size + align; /* worst-case alignment slack */
    if (need < size)
        return NULL;

    if (need > a->block_size) {
        /* oversized: own block, linked behind the head so the head keeps filling */
        b = qry__block_new(a, need);
        if (!b)
            return NULL;
        if (a->head) {
            b->next = a->head->next;
            a->head->next = b;
        } else {
            a->head = b;
        }
        return qry__block_take(b, size, align);
    }

    b = qry__block_new(a, a->block_size);
    if (!b)
        return NULL;
    b->next = a->head;
    a->head = b;
    return qry__block_take(b, size, align);
}

/* Is [ptr, ptr + size) the most recent allocation in the head block? */
static int qry__arena_is_top(qry_Arena *a, void *ptr, size_t size) {
    uintptr_t base, top, p = (uintptr_t)ptr;
    if (!a->head || !ptr)
        return 0;
    base = (uintptr_t)a->head + QRY__BLOCK_HDR;
    top = base + a->head->used;
    return p >= base && p <= top && p + size == top;
}

static void *qry__arena_proc(void *ctx, qry_Op op, void *ptr, size_t old_size,
                             size_t new_size, size_t align) {
    qry_Arena *a = (qry_Arena *)ctx;
    switch (op) {
    case QRY_OP_ALLOC:
        return qry__arena_alloc(a, new_size, align);

    case QRY_OP_RESIZE: {
        void *np;
        if (!ptr)
            return qry__arena_alloc(a, new_size, align);
        if (qry__arena_is_top(a, ptr, old_size)) { /* grow or shrink in place */
            uintptr_t base = (uintptr_t)a->head + QRY__BLOCK_HDR;
            size_t start = (size_t)((uintptr_t)ptr - base);
            size_t nused = start + new_size;
            if (nused >= start && nused <= a->head->cap) {
                a->head->used = nused;
                return ptr;
            }
        }
        np = qry__arena_alloc(
            a, new_size, align); /* otherwise copy; old becomes dead space */
        if (!np)
            return NULL;
        memcpy(np, ptr, old_size < new_size ? old_size : new_size);
        return np;
    }

    case QRY_OP_FREE:
        if (qry__arena_is_top(
                a, ptr, old_size)) /* give back the most recent allocation */
            a->head->used = (size_t)((uintptr_t)ptr -
                                     ((uintptr_t)a->head + QRY__BLOCK_HDR));
        return NULL;

    case QRY_OP_FREE_ALL:
        qry_arena_reset(a);
        return NULL;
    }
    return NULL;
}

qry_Arena *qry_arena_new(qry_Allocator backing, size_t block_size) {
    qry_Arena *a = (qry_Arena *)qry_alloc(backing, sizeof *a);
    if (!a)
        return NULL;
    a->backing = backing;
    a->head = NULL;
    a->block_size = block_size ? block_size : 64 * 1024;
    return a;
}

qry_Allocator qry_arena_allocator(qry_Arena *a) {
    qry_Allocator al;
    al.proc = qry__arena_proc;
    al.ctx = a;
    return al;
}

void qry_arena_reset(qry_Arena *a) {
    qry__Block *b, *next;
    if (!a->head)
        return;
    for (b = a->head->next; b; b = next) {
        next = b->next;
        qry_free(a->backing, b, QRY__BLOCK_HDR + b->cap);
    }
    a->head->next = NULL;
    a->head->used = 0;
}

void qry_arena_free(qry_Arena *a) {
    qry__Block *b, *next;
    qry_Allocator backing;
    if (!a)
        return;
    for (b = a->head; b; b = next) {
        next = b->next;
        qry_free(a->backing, b, QRY__BLOCK_HDR + b->cap);
    }
    backing = a->backing;
    qry_free(backing, a, sizeof *a);
}

/* ----------------------------------------------------------------- arrays */

void *qry__arr_grow(void *a, size_t elem_size, size_t add) {
    qry_ArrHdr *h = a ? qry__hdr(a) : NULL;
    size_t len = h ? h->len : 0;
    size_t cap = h ? h->cap : 0;
    size_t need = len + add;
    size_t new_cap = cap * 2;

    if (need <= cap)
        return a;
    if (new_cap < need)
        new_cap = need;
    if (new_cap < 4)
        new_cap = 4;

    h = (qry_ArrHdr *)realloc(h, QRY_HDR_SIZE + new_cap * elem_size);
    if (!h)
        abort();
    if (!a)
        h->len = 0;
    h->cap = new_cap;
    return (char *)h + QRY_HDR_SIZE;
}

/* ------------------------------------------------------------ string views */

qry_Str qry_str(const char *cstr) {
    qry_Str s;
    s.ptr = cstr;
    s.len = cstr ? strlen(cstr) : 0;
    return s;
}

qry_Str qry_str_from(const char *p, size_t len) {
    qry_Str s;
    s.ptr = p;
    s.len = len;
    return s;
}

int qry_str_eq(qry_Str a, qry_Str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

int qry_str_starts_with(qry_Str s, qry_Str prefix) {
    return prefix.len <= s.len &&
           (prefix.len == 0 || memcmp(s.ptr, prefix.ptr, prefix.len) == 0);
}

ptrdiff_t qry_str_find(qry_Str s, qry_Str needle) {
    size_t i;
    if (needle.len == 0)
        return 0;
    if (needle.len > s.len)
        return -1;
    for (i = 0; i + needle.len <= s.len; i++)
        if (memcmp(s.ptr + i, needle.ptr, needle.len) == 0)
            return (ptrdiff_t)i;
    return -1;
}

qry_Str qry_str_slice(qry_Str s, size_t start, size_t end) {
    if (end > s.len)
        end = s.len;
    if (start > end)
        start = end;
    return qry_str_from(s.ptr + start, end - start);
}

static int qry__is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
           c == '\f';
}

qry_Str qry_str_trim(qry_Str s) {
    size_t start = 0, end = s.len;
    while (start < end && qry__is_space(s.ptr[start]))
        start++;
    while (end > start && qry__is_space(s.ptr[end - 1]))
        end--;
    return qry_str_from(s.ptr + start, end - start);
}

qry_Str qry_str_chop(qry_Str *s, char delim) {
    qry_Str out;
    const char *hit =
        s->len ? (const char *)memchr(s->ptr, delim, s->len) : NULL;
    if (hit) {
        size_t n = (size_t)(hit - s->ptr);
        out = qry_str_from(s->ptr, n);
        s->ptr += n + 1;
        s->len -= n + 1;
    } else {
        out = *s;
        s->ptr += s->len;
        s->len = 0;
    }
    return out;
}

/* ----------------------------------------------------------- string builder */

/* every builder call reserves one extra byte for the terminating NUL */

void qry_sb_append(char **sb, qry_Str s) {
    qry_arr_reserve(*sb, s.len + 1);
    if (s.len)
        memcpy(*sb + qry__hdr(*sb)->len, s.ptr, s.len);
    qry__hdr(*sb)->len += s.len;
    (*sb)[qry__hdr(*sb)->len] = '\0';
}

void qry_sb_putc(char **sb, char c) {
    qry_arr_reserve(*sb, 2);
    (*sb)[qry__hdr(*sb)->len++] = c;
    (*sb)[qry__hdr(*sb)->len] = '\0';
}

void qry_sb_printf(char **sb, const char *fmt, ...) {
    va_list ap, ap2;
    int n;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap); /* first pass: measure */
    va_end(ap);

    if (n < 0) { /* formatting error: append nothing */
        va_end(ap2);
        qry_sb_append(sb, qry_str_from(NULL, 0));
        return;
    }

    qry_arr_reserve(*sb, (size_t)n + 1);
    vsnprintf(*sb + qry__hdr(*sb)->len, (size_t)n + 1, fmt,
              ap2); /* second pass: write */
    qry__hdr(*sb)->len += (size_t)n;
    va_end(ap2);
}

qry_Str qry_sb_view(const char *sb) {
    return qry_str_from(sb, qry_arr_len(sb));
}

#endif /* QUARRY_IMPLEMENTATION */
