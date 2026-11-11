#define QUARRY_IMPLEMENTATION
#include "quarry.h"
#include <stdio.h>

/* ---------------------------------------------------------------- 1. arrays */

typedef struct {
    const char *name;
    int score;
} Player;

/* To modify an array inside a function, pass its address: it may move on
 * growth. */
static void add_square(int **a, int n) { qry_arr_push(*a, n * n); }

static void example_arrays(void) {
    puts("== arrays ==");
    int *a = NULL; /* an empty array is just NULL */
    for (int i = 1; i <= 5; i++)
        add_square(&a, i);
    qry_arr_push(a, 99);

    printf("len=%zu last=%d\n", qry_arr_len(a), qry_arr_last(a));
    printf("popped %d\n", qry_arr_pop(a));
    for (size_t i = 0; i < qry_arr_len(a); i++)
        printf("%d ", a[i]);
    puts("");
    qry_arr_clear(a); /* keeps the capacity */
    qry_arr_free(a);  /* frees, a is NULL again */

    Player *ps = NULL; /* arrays of structs work the same way */
    Player p1 = {"ada", 10}, p2 = {"linus", 7};
    qry_arr_push(ps, p1);
    qry_arr_push(ps, p2);
    for (size_t i = 0; i < qry_arr_len(ps); i++) {
        printf("%s: %d\n", ps[i].name, ps[i].score);
    }
    qry_arr_free(ps);
}

/* ---------------------------------------------------------------- 2. strings
 */

static void example_views(void) {
    puts("== string views ==");
    qry_Str s = QRY_S("  key = value  "); /* literals: no strlen */
    qry_Str t = qry_str_trim(s);
    printf("[" QRY_STR_FMT "]\n", QRY_STR_ARG(t)); /* [key = value] */

    ptrdiff_t eq = qry_str_find(t, QRY_S("="));
    qry_Str key = qry_str_trim(qry_str_slice(t, 0, (size_t)eq));
    qry_Str val = qry_str_trim(qry_str_slice(t, (size_t)eq + 1, t.len));
    printf("key=" QRY_STR_FMT " val=" QRY_STR_FMT "\n", QRY_STR_ARG(key),
           QRY_STR_ARG(val));

    printf("starts_with 'key': %d, eq: %d\n",
           qry_str_starts_with(key, QRY_S("ke")),
           qry_str_eq(val, qry_str("value")));

    /* qry_str_chop is a tokenizer: returns the text before the delimiter and
   * advances */
    qry_Str csv = QRY_S("a,bb,,ccc");
    while (csv.len) {
        qry_Str f = qry_str_chop(&csv, ',');
        printf("field: [" QRY_STR_FMT "]\n", QRY_STR_ARG(f));
    }
}

static void example_builder(void) {
    puts("== string builder ==");
    char *sb = NULL; /* a builder is a char* array, always NUL-terminated */
    qry_sb_append(&sb, QRY_S("hello"));
    qry_sb_putc(&sb, ',');
    qry_sb_printf(&sb, " %s #%d", "world", 42);
    puts(sb); /* works with libc as is */
    printf("length=%zu\n", qry_arr_len(sb));

    qry_Str v = qry_sb_view(sb); /* view: invalid once sb grows again */
    printf("view: " QRY_STR_FMT "\n", QRY_STR_ARG(qry_str_slice(v, 0, 5)));
    qry_arr_free(sb);
}

/* parse "k=v;k=v" into views (no copies), then rebuild it as text */
typedef struct {
    qry_Str key, val;
} Pair;

static void example_parse(void) {
    puts("== parse + build ==");
    qry_Str input = QRY_S("name=sid;lang=c;;os=linux");
    Pair *pairs = NULL;

    while (input.len) {
        qry_Str item = qry_str_chop(&input, ';');
        if (!item.len)
            continue; /* skip empty items */
        Pair p;
        p.key = qry_str_chop(&item, '='); /* item becomes the part after '=' */
        p.val = item;
        qry_arr_push(pairs, p);
    }

    char *out = NULL;
    for (size_t i = 0; i < qry_arr_len(pairs); i++)
        qry_sb_printf(&out, "%s" QRY_STR_FMT ":" QRY_STR_FMT, i ? ", " : "",
                      QRY_STR_ARG(pairs[i].key), QRY_STR_ARG(pairs[i].val));
    puts(out); /* name:sid, lang:c, os:linux */

    qry_arr_free(out);
    qry_arr_free(pairs);
}

/* -------------------------------------------------------------- 3. allocators
 */

static void example_heap_and_arena(void) {
    puts("== allocators ==");
    qry_Allocator heap = {0}; /* zero value = the heap */

    int *nums = QRY_NEW_ARRAY(heap, int, 8); /* may be NULL on failure */
    if (!nums)
        return;
    for (int i = 0; i < 8; i++)
        nums[i] = i;
    nums = (int *)qry_resize(heap, nums, 8 * sizeof *nums, 16 * sizeof *nums);
    printf("after resize nums[7]=%d\n", nums[7]);
    qry_free(heap, nums, 16 * sizeof *nums); /* free needs the size */

    /* an arena: allocate freely, release everything at once */
    qry_Arena *arena = qry_arena_new(heap, 0); /* 64 KiB blocks from the heap */
    qry_Allocator al = qry_arena_allocator(arena);

    Player *p = QRY_NEW(al, Player);
    p->name = "grace";
    p->score = 3;

    char *text = (char *)qry_alloc(al, 16);
    snprintf(text, 16, "scratch");
    text = (char *)qry_resize(al, text, 16,
                              4096); /* newest allocation: grows in place */
    printf("%s / %s\n", p->name, text);

    /* copy a view into the arena as a NUL-terminated string */
    qry_Str v = QRY_S("copied");
    char *copy = (char *)qry_alloc(al, v.len + 1);
    memcpy(copy, v.ptr, v.len);
    copy[v.len] = '\0';
    puts(copy);

    /* per-frame scratch pattern */
    for (int frame = 0; frame < 3; frame++) {
        int *tmp = QRY_NEW_ARRAY(al, int, 1000);
        tmp[999] = frame;
        printf("frame %d scratch ok (%d)\n", frame, tmp[999]);
        qry_arena_reset(arena); /* reuse the same memory next frame */
    }
    qry_arena_free(arena);
}

/* A custom allocator: a proc and a context. This one counts, then defers to the
 * heap. */
typedef struct {
    size_t allocs, frees;
} Stats;

static void *stats_proc(void *ctx, qry_Op op, void *ptr, size_t old_size,
                        size_t new_size, size_t align) {
    Stats *s = (Stats *)ctx;
    qry_Allocator heap = qry_heap();
    switch (op) {
    case QRY_OP_ALLOC:
        s->allocs++;
        return qry_alloc_align(heap, new_size, align);
    case QRY_OP_RESIZE:
        return qry_resize_align(heap, ptr, old_size, new_size, align);
    case QRY_OP_FREE:
        s->frees++;
        qry_free(heap, ptr, old_size);
        return NULL;
    case QRY_OP_FREE_ALL:
        return NULL;
    }
    return NULL;
}

static void example_custom_allocator(void) {
    puts("== custom allocator ==");
    Stats stats = {0, 0};
    qry_Allocator counted = {stats_proc, &stats};

    /* an arena that gets its blocks from our counting allocator */
    qry_Arena *arena = qry_arena_new(counted, 1024);
    qry_Allocator al = qry_arena_allocator(arena);
    for (int i = 0; i < 100; i++)
        qry_alloc(al, 100); /* ~10 KB -> several 1 KiB blocks */
    qry_arena_free(arena);
    printf("backing allocs=%zu frees=%zu\n", stats.allocs, stats.frees);
}

int main(void) {
    example_arrays();
    example_views();
    example_builder();
    example_parse();
    example_heap_and_arena();
    example_custom_allocator();
    return 0;
}
