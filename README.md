# quarry

A small, single-header C library (stb-style) that provides the base layer for writing C programs: **allocators, dynamic arrays, and strings**. Hash maps are planned.

- One file: `quarry.h`. No build system, no dependencies beyond the C standard library.
- C99.
- Every public name starts with `qry_` (macros with `QRY_`).
- Allocation failure is never hidden: allocators return `NULL`, and the array and string builder code aborts on out-of-memory.

> **Status:** work in progress. The API may change.

## Quick start

Copy `quarry.h` into your project. In **exactly one** `.c` file, define `QUARRY_IMPLEMENTATION` before including it:

```c
#define QUARRY_IMPLEMENTATION
#include "quarry.h"
```

Everywhere else, include it normally:

```c
#include "quarry.h"
```

A complete program:

```c
#define QUARRY_IMPLEMENTATION
#include "quarry.h"
#include <stdio.h>

int main(void) {
    int *a = NULL;                       /* an empty array is just NULL */
    for (int i = 0; i < 5; i++) {
        qry_arr_push(a, i * i);
    }

    for (size_t i = 0; i < qry_arr_len(a); i++) {
        printf("%d ", a[i]);
    }
    puts("");

    qry_arr_free(a);

    char *sb = NULL;                     /* a string builder is a char array */
    qry_sb_printf(&sb, "hello %s #%d", "world", 42);
    puts(sb);
    qry_arr_free(sb);
    return 0;
}
```

See `quarry_examples.c` for a longer, runnable tour of everything below.

## Allocators

An allocator is a `{proc, ctx}` pair passed by value. A single `proc` handles every operation:

| Operation | Meaning |
|---|---|
| `QRY_OP_ALLOC` | Return `new_size` bytes aligned to `align`, or `NULL`. Memory is not zeroed. |
| `QRY_OP_RESIZE` | Return a block of at least `new_size` bytes with the old contents preserved. May return the same pointer (grown in place) or a new one (copied). On failure return `NULL` and leave the old block valid. `ptr == NULL` means alloc. |
| `QRY_OP_FREE` | Release a block. May be a no-op. |
| `QRY_OP_FREE_ALL` | Release everything the allocator handed out. A no-op for the heap. |

`free` and `resize` receive the old size, so allocators don't need hidden per-allocation headers.

A zero-initialized allocator (`qry_Allocator a = {0};`) means the heap, so structs that hold an allocator need no setup.

```c
qry_Allocator heap = {0};
int *p = QRY_NEW_ARRAY(heap, int, 100);          /* may be NULL */

p = qry_resize(heap, p, 100 * sizeof *p, 200 * sizeof *p);

qry_free(heap, p, 200 * sizeof *p);
```

| Function / macro | Description |
|---|---|
| `qry_alloc(a, size)` | Allocate, 16-byte aligned. |
| `qry_alloc_align(a, size, align)` | Allocate with an explicit power-of-two alignment. |
| `qry_alloc_zero(a, size)` | Allocate zeroed memory. |
| `qry_alloc_array(a, elem_size, count)` | Allocate an array, `NULL` on size overflow. |
| `qry_resize(a, ptr, old, new)` / `qry_resize_align(...)` | Resize a block. |
| `qry_free(a, ptr, size)` | Free one block. |
| `qry_free_all(a)` | Free everything (arenas, pools). |
| `QRY_NEW(a, T)`, `QRY_NEW_ARRAY(a, T, n)` | Typed allocation helpers. |
| `qry_heap()` | The heap allocator (same behavior as `{0}`). Supports alignment up to 16. |

### Arena

An arena hands out memory by bumping a pointer and releases it all at once. It grows by chaining blocks it requests from a **backing allocator**, so it can sit on the heap, on another arena, or on a custom provided allocator.

```c
qry_Arena *arena = qry_arena_new(heap, 0);       /* 0 = 64 KiB blocks */
qry_Allocator al = qry_arena_allocator(arena);

Item *item = QRY_NEW(al, Item);
char *buf  = qry_alloc(al, 256);
buf = qry_resize(al, buf, 256, 4096);            /* newest allocation: grows in place */

qry_arena_reset(arena);                          /* reuse the memory */
qry_arena_free(arena);                           /* release everything */
```

- `RESIZE` grows or shrinks the **most recent** allocation in place when it fits. Any other resize allocates a new block and copies (the old one stays as unused space until reset).
- `FREE` of the most recent allocation gives its bytes back. Any other free is a no-op.
- `qry_arena_reset` (and `FREE_ALL`) keeps one block for reuse and frees the rest.
- Requests larger than a block get their own block.
- `qry_arena_new` returns `NULL` if the backing allocator fails; allocations return `NULL` when the backing allocator runs out.

### Writing your own allocator

Implement `qry_AllocProc` and wrap it as `{proc, ctx}`:

```c
typedef struct { size_t allocs; } Stats;

static void *counting_proc(void *ctx, qry_Op op, void *ptr,
                           size_t old_size, size_t new_size, size_t align) {
    Stats *s = (Stats *)ctx;
    qry_Allocator heap = qry_heap();
    switch (op) {
      case QRY_OP_ALLOC: {
          s->allocs++;
          return qry_alloc_align(heap, new_size, align);
      }
      case QRY_OP_RESIZE: {
          return qry_resize_align(heap, ptr, old_size, new_size, align);
      }
      case QRY_OP_FREE: {
          qry_free(heap, ptr, old_size);
          return NULL;
      }
      case QRY_OP_FREE_ALL: {
          return NULL;
      }
    }
    return NULL;
}

Stats stats = {0};
qry_Allocator counted = { counting_proc, &stats };
```

## Dynamic arrays

An array is a plain `T *` with a small hidden header (length and capacity) stored just before the first element. An empty array is `NULL`, indexing is `a[i]`, and the pointer works with any code that expects a `T *`.

```c
int *a = NULL;
qry_arr_push(a, 42);

printf("%zu items, last = %d\n", qry_arr_len(a), qry_arr_last(a));

int v = qry_arr_pop(a);
qry_arr_clear(a);          /* length = 0, keeps capacity */
qry_arr_free(a);           /* frees and sets a = NULL */
```

| Macro | Description |
|---|---|
| `qry_arr_len(a)`, `qry_arr_cap(a)` | Length and capacity (0 for `NULL`). |
| `qry_arr_push(a, v)` | Append, growing if needed. |
| `qry_arr_pop(a)` | Remove and return the last element. |
| `qry_arr_last(a)` | The last element. |
| `qry_arr_reserve(a, n)` | Make room for `n` more elements. |
| `qry_arr_clear(a)` | Set length to 0. |
| `qry_arr_free(a)` | Free the array and set `a` to `NULL`. |

Rules:

- **The pointer can change when the array grows.** It must be an lvalue, keep one canonical copy, and to modify an array inside a function pass its address (`int **a`) or return the new pointer.
- Macro arguments may be evaluated more than once. Don't put side effects in them.
- `qry_arr_pop` and `qry_arr_last` do not check for an empty array.
- Growth doubles the capacity (minimum 4) using `realloc`; out-of-memory aborts.

## Strings

Two types work together:

- **`qry_Str`** is a *view*: a pointer and a length, passed by value, that never owns memory. Substrings and tokens allocate nothing, strings with embedded NULs work, and the length is O(1). A view with `ptr == NULL` and `len == 0` is a valid empty string.
- **`char *`** is a *builder*: a dynamic array of `char` that is kept NUL-terminated after every builder call, so it can be passed straight to `printf`, `fopen`, and the rest of libc. Its length is `qry_arr_len(sb)`, and you free it with `qry_arr_free(sb)`.

### Views

| Function | Description |
|---|---|
| `QRY_S("literal")` | View of a string literal, no `strlen`. **Literals only** (it uses `sizeof`). |
| `qry_str(cstr)` | View of a C string (uses `strlen`; `NULL` gives an empty view). |
| `qry_str_from(ptr, len)` | View from a pointer and a length. |
| `qry_str_eq(a, b)` | Equality. |
| `qry_str_starts_with(s, prefix)` | Prefix test. |
| `qry_str_find(s, needle)` | Index of the first match, or `-1`. |
| `qry_str_slice(s, start, end)` | Sub-view; indices are clamped. |
| `qry_str_trim(s)` | Strip ASCII whitespace from both ends. |
| `qry_str_chop(&s, delim)` | Return the text before `delim` and advance `s` past it. |

Printing a view:

```c
printf("name: " QRY_STR_FMT "\n", QRY_STR_ARG(name));   /* "%.*s" with (int)len, ptr */
```

`qry_str_chop` works as a tokenizer:

```c
qry_Str line = QRY_S("a,b,c");
while (line.len) {
    qry_Str field = qry_str_chop(&line, ',');   /* "a", "b", "c" */
}
```

A trailing delimiter does not produce a final empty field (`"a,b,"` gives `a` and `b`), but an empty field in the middle is returned as an empty view.

### Builder

```c
char *sb = NULL;
qry_sb_append(&sb, QRY_S("hello"));
qry_sb_putc(&sb, ',');
qry_sb_printf(&sb, " %s #%d", "world", 42);
puts(sb);                                  /* hello, world #42 */
qry_Str v = qry_sb_view(sb);
qry_arr_free(sb);
```

| Function | Description |
|---|---|
| `qry_sb_append(&sb, view)` | Append a view. |
| `qry_sb_putc(&sb, c)` | Append one character. |
| `qry_sb_printf(&sb, fmt, ...)` | Append formatted text (measures with `vsnprintf`, then writes; no fixed size limit). Format strings are checked by the compiler on GCC and Clang. |
| `qry_sb_view(sb)` | A view of the builder's contents. |

Rules:

- A view into a builder is **invalidated when the builder grows**.
- Do not append a view that points into the same builder.
- To append a C string, use `qry_sb_append(&sb, qry_str(cstr))`. Do not use `QRY_S` on a `char *`: it measures the pointer, not the string.

## Options

Define these before including the header in the implementation file:

| Macro | Effect |
|---|---|
| `QRY_ASSERT(x)` | Override the assert used for argument checks (default: `assert`). |

## Notes and limitations

- **C only.** `QRY_S` uses a compound literal, which is C99 but not valid C++.
- **Arrays and string builders currently always use the heap.** They do not take an allocator yet, so allocating them in an arena is not supported. Making them allocator-aware is planned.
- **No overflow checks in array growth.** Capacity arithmetic can wrap for absurd sizes. This only matters if sizes come from untrusted input.
- **The heap allocator supports alignment up to 16** and returns `NULL` for larger requests (arenas support any power of two).
- No `malloc`/`free` override macros yet; use a custom allocator for the allocator-aware parts.
- Not thread-safe: nothing is shared between calls, but an allocator or container must not be used from several threads at once.
- Not yet provided: hash maps, a mark-and-rewind (temporary scope) for arenas, array element deletion, a `qry_str_dup` that copies a view into an allocator.

## Building and tests

There is nothing to build. To compile the examples:

```sh
cc -std=c99 -Wall -Wextra -pedantic quarry_examples.c -o examples
```

During development the code has been checked with GCC under `-std=c99 -Wall -Wextra -pedantic`, with AddressSanitizer and UndefinedBehaviorSanitizer, and with `-DNDEBUG`. Other compilers and platforms have not been tested yet.
