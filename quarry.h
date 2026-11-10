#ifndef QUARRY_H
#define QUARRY_H

#include <stddef.h>
#include <stdlib.h>

// Dynamic Array

typedef struct {
  size_t len;
  size_t cap;
} qry_ArrHdr;

#define QRY_HDR_SIZE ((sizeof(qry_ArrHdr) + 15u) & ~(size_t)15u)
#define qry__hdr(a) ((qry_ArrHdr *)((char *)(a) - QRY_HDR_SIZE))

void *qry__arr_grow(void *a, size_t elem_size, size_t add);

#define qry_arr_len(a) ((a) ? qry__hdr(a)->len : (size_t)0)
#define qry_arr_cap(a) ((a) ? qry__hdr(a)->cap : (size_t)0)
#define qry_arr_last(a) ((a)[qry__hdr(a)->len - 1])

/* make room for n more elements */
#define qry_arr_reserve(a, n)                                                  \
  ((!(a) || qry__hdr(a)->len + (n) > qry__hdr(a)->cap)                         \
       ? (void)((a) = qry__arr_grow((a), sizeof *(a), (n)))                    \
       : (void)0)

#define qry_arr_push(a, v)                                                     \
  (qry_arr_reserve(a, 1), (a)[qry__hdr(a)->len++] = (v))

#define qry_arr_pop(a) ((a)[--qry__hdr(a)->len])
#define qry_arr_clear(a) ((void)((a) ? (qry__hdr(a)->len = 0) : 0))
#define qry_arr_free(a) ((void)((a) ? (free(qry__hdr(a)), (a) = NULL) : NULL))

// String

typedef struct {
  const char *ptr;
  size_t len;
} qry_Str;

qry_Str qry_str(const char *s);
qry_Str qry_str_from(const char *s, size_t len);

#define QRY_S(lit) ((qry_Str){(lit), sizeof(lit) - 1})

#define QRY_STR_FMT "%.*s"
#define QRY_STR_ARG(s) (int)(s).len, (s).ptr

#if defined(__GNUC__) || defined(__clang__)
#define QRY__PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define QRY__PRINTF(f, a)
#endif

int qry_str_eq(qry_Str a, qry_Str b);
int qry_str_cmp(qry_Str a, qry_Str b);
int qry_str_starts_with(qry_Str s, qry_Str prefix);
int qry_str_ends_with(qry_Str s, qry_Str suffix);
long qry_str_find(qry_Str s, qry_Str needle); /* index or -1 */

qry_Str qry_str_slice(qry_Str s, size_t start, size_t end);
qry_Str qry_str_trim(qry_Str s);
qry_Str qry_str_chop(qry_Str *s, char delim);

void qry_sb_append(char **sb, qry_Str s);
void qry_sb_csappend(char **sb, const char *s);
void qry_sb_putc(char **sb, char c);
void qry_sb_printf(char **sb, const char *fmt, ...);
qry_Str qry_sb_view(const char *sb); /* view of the builder's contents */
char *qry_str_dup(qry_Str s);

#endif // QUARRY_H

#ifdef QUARRY_IMPLEMENTATION

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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
  const char *hit = s->len ? (const char *)memchr(s->ptr, delim, s->len) : NULL;
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

/* NOTE: every builder call reserves one extra byte for the terminating NUL */

void qry_sb_append(char **sb, qry_Str s) {
  qry_arr_reserve(*sb, s.len + 1);
  if (s.len)
    memcpy(*sb + qry__hdr(*sb)->len, s.ptr, s.len);
  qry__hdr(*sb)->len += s.len;
  (*sb)[qry__hdr(*sb)->len] = '\0';
}

void qry_sb_csappend(char **sb, const char *cs) {
  qry_sb_append(sb, qry_str(cs));
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
  n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);

  if (n < 0) {
    va_end(ap2);
    qry_sb_append(sb, qry_str_from(NULL, 0));
    return;
  }

  qry_arr_reserve(*sb, (size_t)n + 1);
  vsnprintf(*sb + qry__hdr(*sb)->len, (size_t)n + 1, fmt, ap2);
  qry__hdr(*sb)->len += (size_t)n;
  va_end(ap2);
}

qry_Str qry_sb_view(const char *sb) {
  return qry_str_from(sb, qry_arr_len(sb));
}

#endif /* QUARRY_IMPLEMENTATION */
