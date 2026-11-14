/* SPDX-License-Identifier: AGPL-3.0-or-later
 * macOS: interposes the C allocator in every image of the test process (the plugin's too) and
 * counts allocations while va_alloc_on is set. dyld applies the __interpose tuples of any dylib
 * the process loads; the main executable's own are ignored, hence a library. operator new calls
 * malloc, so it is counted. */
#include <stdlib.h>
#include <stddef.h>
volatile long va_alloc_count = 0;
volatile int va_alloc_on = 0;
#define NOTE() do { if (va_alloc_on) va_alloc_count = va_alloc_count + 1; } while (0)
static void* i_malloc(size_t s) { NOTE(); return malloc(s); }
static void* i_calloc(size_t a, size_t b) { NOTE(); return calloc(a, b); }
static void* i_realloc(void* p, size_t s) { NOTE(); return realloc(p, s); }
static void* i_valloc(size_t s) { NOTE(); return valloc(s); }
static int i_posix_memalign(void** o, size_t a, size_t s) { NOTE(); return posix_memalign(o, a, s); }
#define INTERPOSE(mine, orig) __attribute__((used)) static struct { const void* a; const void* b; } t_##orig \
    __attribute__((section("__DATA,__interpose"))) = {(const void*)mine, (const void*)orig};
INTERPOSE(i_malloc, malloc) INTERPOSE(i_calloc, calloc) INTERPOSE(i_realloc, realloc)
INTERPOSE(i_valloc, valloc) INTERPOSE(i_posix_memalign, posix_memalign)
