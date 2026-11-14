// SPDX-License-Identifier: AGPL-3.0-or-later
// Counts heap allocations made anywhere in this process while enabled, by interposing the C
// allocator (operator new reaches it too), so allocations inside the dlopen'ed plugin are caught
// whatever language it is written in.
//   macOS: dyld interposition from a small library (alloc_interpose.c) linked into the harness.
//   Linux (glibc): this executable defines malloc and friends, which the dynamic linker binds the
//   plugin's calls to; they forward to glibc's __libc_* entry points.
// Sanitizer builds bring their own allocator; there the guard is unavailable and says so.
#include <cstdint>
#include <cstdlib>
#include "host.h"

#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define VA_NO_GUARD 1
#endif
#endif
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
#define VA_NO_GUARD 1
#endif

#if !defined(__APPLE__)
namespace {
volatile bool g_on = false;
volatile long g_count = 0;
}
#endif

#if defined(VA_NO_GUARD)
bool allocGuardAvailable() { return false; }
const char* allocGuardName() { return "unavailable (sanitizer build)"; }

#elif defined(__APPLE__)
extern "C" { extern volatile long va_alloc_count; extern volatile int va_alloc_on; }  // alloc_interpose.c
bool allocGuardAvailable() { return true; }
const char* allocGuardName() { return "macOS dyld interposition"; }

#else  // Linux, glibc
extern "C" {
void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t);
void __libc_free(void*); void* __libc_memalign(size_t, size_t);
static inline void note() { if (g_on) g_count = g_count + 1; }
void* malloc(size_t n) { note(); return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { note(); return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { note(); return __libc_realloc(p, n); }
void free(void* p) { __libc_free(p); }
void* memalign(size_t a, size_t n) { note(); return __libc_memalign(a, n); }
void* aligned_alloc(size_t a, size_t n) { note(); return __libc_memalign(a, n); }
int posix_memalign(void** out, size_t a, size_t n) { note(); void* p = __libc_memalign(a, n); if (!p) return 12; *out = p; return 0; }
}
bool allocGuardAvailable() { return true; }
const char* allocGuardName() { return "glibc malloc interposition"; }
#endif

#if defined(VA_NO_GUARD)
void allocGuardEnable(bool) {}
long allocGuardCount() { return 0; }
#elif defined(__APPLE__)
void allocGuardEnable(bool on) { if (on) va_alloc_count = 0; va_alloc_on = on; }
long allocGuardCount() { return va_alloc_count; }
#else
void allocGuardEnable(bool on) { if (on) g_count = 0; g_on = on; }
long allocGuardCount() { return g_count; }
#endif
