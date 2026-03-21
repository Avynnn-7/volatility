/**
 * @file optimization_hints.hpp
 * @brief Cross-platform performance optimization macros
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * Provides portable compiler hints for performance optimization:
 * - Branch prediction (VOL_LIKELY, VOL_UNLIKELY)
 * - Data prefetching (VOL_PREFETCH_READ, VOL_PREFETCH_WRITE)
 * - Cache alignment (VOL_CACHE_ALIGNED)
 * - Loop optimization (VOL_LOOP_VECTORIZE, VOL_LOOP_UNROLL)
 * - Force/prevent inlining (VOL_FORCE_INLINE, VOL_NOINLINE)
 *
 * ## Compiler Support
 * - MSVC: Full support via intrinsics and pragmas
 * - GCC/Clang: Full support via __builtin_* intrinsics
 * - Other: Graceful fallback to no-op macros
 *
 * ## Example Usage
 * @code
 * // Branch prediction
 * if (VOL_LIKELY(ptr != nullptr)) {
 *     process(ptr);
 * }
 *
 * // Cache alignment
 * struct VOL_CACHE_ALIGNED AlignedData {
 *     double values[8];
 * };
 *
 * // Force inline hot path
 * VOL_FORCE_INLINE double fastCompute(double x) {
 *     return x * x;
 * }
 * @endcode
 */

#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 4 OPTIMIZATION #7: Branch Prediction and Cache Optimization Macros
// ═══════════════════════════════════════════════════════════════════════════
//
// Provides cross-platform macros for:
// - Branch prediction hints (LIKELY/UNLIKELY)
// - Data prefetching
// - Cache line alignment
//
// Expected improvement: 1.1-1.2× in hot paths
// ═══════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Branch Prediction Hints
// ─────────────────────────────────────────────────────────────────────────────

// Use these in conditional statements where the outcome is predictable
// Example: if (VOL_LIKELY(ptr != nullptr)) { ... }

#if defined(__cplusplus) && __cplusplus >= 202002L
    // C++20: Use standard attributes
    #define VOL_LIKELY(x)   (x) [[likely]]
    #define VOL_UNLIKELY(x) (x) [[unlikely]]
#elif defined(_MSC_VER)
    // MSVC: No direct equivalent, but these help documentation
    // MSVC's PGO (Profile-Guided Optimization) handles this better
    #define VOL_LIKELY(x)   (x)
    #define VOL_UNLIKELY(x) (x)
#elif defined(__GNUC__) || defined(__clang__)
    // GCC/Clang: Use __builtin_expect
    #define VOL_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define VOL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    // Fallback: No-op
    #define VOL_LIKELY(x)   (x)
    #define VOL_UNLIKELY(x) (x)
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Data Prefetching
// ─────────────────────────────────────────────────────────────────────────────

// Prefetch data into cache before use
// Use in loops where you know what data will be needed next
//
// Locality hints:
//   0 = Temporal data (keep in all cache levels)
//   1 = Temporal data (keep in L2 and L3)
//   2 = Temporal data (keep only in L3)
//   3 = Non-temporal data (one-time access)

#if defined(_MSC_VER)
    #include <intrin.h>
    // MSVC prefetch (note: uses mm_prefetch intrinsic)
    #define VOL_PREFETCH_READ(addr)  _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
    #define VOL_PREFETCH_WRITE(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
    #define VOL_PREFETCH_NTA(addr)   _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_NTA)
#elif defined(__GNUC__) || defined(__clang__)
    // GCC/Clang prefetch
    // __builtin_prefetch(addr, rw, locality)
    //   rw: 0 = read, 1 = write
    //   locality: 0-3 (3 = keep in all caches)
    #define VOL_PREFETCH_READ(addr)  __builtin_prefetch((addr), 0, 3)
    #define VOL_PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
    #define VOL_PREFETCH_NTA(addr)   __builtin_prefetch((addr), 0, 0)
#else
    // Fallback: No-op
    #define VOL_PREFETCH_READ(addr)  ((void)(addr))
    #define VOL_PREFETCH_WRITE(addr) ((void)(addr))
    #define VOL_PREFETCH_NTA(addr)   ((void)(addr))
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Cache Line Alignment
// ─────────────────────────────────────────────────────────────────────────────

// Modern x86 cache line size is 64 bytes
constexpr size_t VOL_CACHE_LINE_SIZE = 64;

// Align structs/arrays to cache line boundaries to prevent false sharing
// Example: struct alignas(VOL_CACHELINE_ALIGN) MyStruct { ... };

#if defined(_MSC_VER)
    #define VOL_CACHELINE_ALIGN __declspec(align(64))
#elif defined(__GNUC__) || defined(__clang__)
    #define VOL_CACHELINE_ALIGN __attribute__((aligned(64)))
#else
    #define VOL_CACHELINE_ALIGN
#endif

// C++11 alignas version (preferred)
#define VOL_CACHE_ALIGNED alignas(VOL_CACHE_LINE_SIZE)

// ─────────────────────────────────────────────────────────────────────────────
// Loop Optimization Hints
// ─────────────────────────────────────────────────────────────────────────────

// Hint to compiler that loop iterations are independent (enables vectorization)
#if defined(_MSC_VER)
    // MSVC: Use #pragma loop(hint_parallel(N))
    #define VOL_LOOP_VECTORIZE __pragma(loop(hint_parallel(4)))
    #define VOL_LOOP_UNROLL(n) __pragma(loop(unroll_count(n)))
#elif defined(__clang__)
    // Clang pragmas
    #define VOL_LOOP_VECTORIZE _Pragma("clang loop vectorize(enable)")
    #define VOL_LOOP_UNROLL(n) _Pragma("clang loop unroll_count(" #n ")")
#elif defined(__GNUC__)
    // GCC pragmas (GCC 8+)
    #define VOL_LOOP_VECTORIZE _Pragma("GCC ivdep")
    #define VOL_LOOP_UNROLL(n) _Pragma("GCC unroll " #n)
#else
    #define VOL_LOOP_VECTORIZE
    #define VOL_LOOP_UNROLL(n)
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Restrict Pointer Hint
// ─────────────────────────────────────────────────────────────────────────────

// Indicate that pointers don't alias (enables more aggressive optimization)
#if defined(_MSC_VER)
    #define VOL_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
    #define VOL_RESTRICT __restrict__
#else
    #define VOL_RESTRICT
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Force Inline
// ─────────────────────────────────────────────────────────────────────────────

// Force function inlining for critical hot paths
#if defined(_MSC_VER)
    #define VOL_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define VOL_FORCE_INLINE __attribute__((always_inline)) inline
#else
    #define VOL_FORCE_INLINE inline
#endif

// ─────────────────────────────────────────────────────────────────────────────
// No Inline
// ─────────────────────────────────────────────────────────────────────────────

// Prevent inlining (useful for error handling paths to reduce code bloat)
#if defined(_MSC_VER)
    #define VOL_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define VOL_NOINLINE __attribute__((noinline))
#else
    #define VOL_NOINLINE
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Hot/Cold Function Attributes
// ─────────────────────────────────────────────────────────────────────────────

// Mark frequently called functions (optimizer may place them together)
#if defined(__GNUC__) || defined(__clang__)
    #define VOL_HOT  __attribute__((hot))
    #define VOL_COLD __attribute__((cold))
#else
    #define VOL_HOT
    #define VOL_COLD
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Branch-Free Min/Max
// ─────────────────────────────────────────────────────────────────────────────

namespace vol_opt {

// These rely on compiler optimization to use conditional moves (CMOV)
// instead of branches, which is more predictable

template<typename T>
VOL_FORCE_INLINE T branchfree_min(T a, T b) {
    return (a < b) ? a : b;
}

template<typename T>
VOL_FORCE_INLINE T branchfree_max(T a, T b) {
    return (a > b) ? a : b;
}

// Clamp value to range without branches
template<typename T>
VOL_FORCE_INLINE T branchfree_clamp(T val, T lo, T hi) {
    return branchfree_min(branchfree_max(val, lo), hi);
}

// Sign function without branches
template<typename T>
VOL_FORCE_INLINE int branchfree_sign(T val) {
    return (T(0) < val) - (val < T(0));
}

} // namespace vol_opt

// ═══════════════════════════════════════════════════════════════════════════
// END PHASE 4 OPTIMIZATION #7
// ═══════════════════════════════════════════════════════════════════════════
