/**
 * @file memory_pool.hpp
 * @brief High-performance memory pool allocator
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * Provides fast allocation for temporary objects:
 * - O(1) bump-pointer allocation
 * - Bulk deallocation via reset()
 * - Thread-local pools for OpenMP compatibility
 * - STL-compatible allocator adapter
 *
 * ## Performance Characteristics
 * - Allocation: O(1) amortized
 * - Deallocation: O(1) per object (no-op until reset)
 * - Reset: O(n) where n is number of blocks
 *
 * ## Usage Pattern
 * @code
 * MemoryPool pool(1024 * 1024);  // 1 MB pool
 * PoolResetGuard guard(pool);    // Auto-reset on scope exit
 *
 * // Fast allocations
 * double* data = pool.allocate<double>(1000);
 * MyStruct* obj = pool.create<MyStruct>(args...);
 *
 * // All memory freed when guard goes out of scope
 * @endcode
 *
 * @see ThreadLocalPool for OpenMP-compatible usage
 * @see PoolAllocator for STL container integration
 */

#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 4 OPTIMIZATION #6: Memory Pool Allocator
// ═══════════════════════════════════════════════════════════════════════════
//
// Provides fast allocation for temporary objects by pre-allocating large blocks
// and serving allocations from the pool. Supports bulk deallocation via reset().
//
// Features:
// - Fast allocation: O(1) bump pointer allocation
// - No individual deallocation overhead: reset() frees everything at once
// - Thread-local pools: safe for OpenMP parallelization
// - STL-compatible allocator adapter
//
// Expected improvement: 1.2-1.5× reduction in allocation overhead
// ═══════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>
#include <new>

// ─────────────────────────────────────────────────────────────────────────────
// Platform-specific aligned allocation
// ─────────────────────────────────────────────────────────────────────────────

namespace pool_detail {

#if defined(_MSC_VER)
// Windows: use _aligned_malloc/_aligned_free
inline void* aligned_alloc_impl(size_t alignment, size_t size) {
    return _aligned_malloc(size, alignment);
}

inline void aligned_free_impl(void* ptr) {
    _aligned_free(ptr);
}
#else
// POSIX: use std::aligned_alloc
inline void* aligned_alloc_impl(size_t alignment, size_t size) {
    // std::aligned_alloc requires size to be multiple of alignment
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    return std::aligned_alloc(alignment, aligned_size);
}

inline void aligned_free_impl(void* ptr) {
    std::free(ptr);
}
#endif

} // namespace pool_detail

// ─────────────────────────────────────────────────────────────────────────────
// Memory Pool Implementation
// ─────────────────────────────────────────────────────────────────────────────

class MemoryPool {
public:
    static constexpr size_t DEFAULT_BLOCK_SIZE = 1024 * 1024;  // 1 MB
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    explicit MemoryPool(size_t blockSize = DEFAULT_BLOCK_SIZE)
        : blockSize_(blockSize), currentOffset_(0), totalAllocated_(0)
    {
        allocateNewBlock();
    }
    
    ~MemoryPool() {
        for (auto* block : blocks_) {
            pool_detail::aligned_free_impl(block);
        }
    }
    
    // Non-copyable
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    // Movable
    MemoryPool(MemoryPool&& other) noexcept
        : blocks_(std::move(other.blocks_)),
          blockSize_(other.blockSize_),
          currentOffset_(other.currentOffset_),
          totalAllocated_(other.totalAllocated_)
    {
        other.blocks_.clear();
        other.currentOffset_ = 0;
        other.totalAllocated_ = 0;
    }
    
    MemoryPool& operator=(MemoryPool&& other) noexcept {
        if (this != &other) {
            // Free existing blocks
            for (auto* block : blocks_) {
                pool_detail::aligned_free_impl(block);
            }
            
            blocks_ = std::move(other.blocks_);
            blockSize_ = other.blockSize_;
            currentOffset_ = other.currentOffset_;
            totalAllocated_ = other.totalAllocated_;
            
            other.blocks_.clear();
            other.currentOffset_ = 0;
            other.totalAllocated_ = 0;
        }
        return *this;
    }
    
    // Allocate raw bytes with specified alignment
    void* allocateRaw(size_t bytes, size_t alignment = alignof(std::max_align_t)) {
        if (bytes == 0) return nullptr;
        
        // Align current offset
        size_t alignedOffset = (currentOffset_ + alignment - 1) & ~(alignment - 1);
        
        // Check if fits in current block
        if (alignedOffset + bytes > blockSize_) {
            // Need new block
            if (bytes > blockSize_) {
                // Oversized allocation: create dedicated block
                void* block = pool_detail::aligned_alloc_impl(CACHE_LINE_SIZE, bytes);
                if (!block) throw std::bad_alloc();
                blocks_.push_back(block);
                totalAllocated_ += bytes;
                return block;
            }
            
            allocateNewBlock();
            alignedOffset = 0;
        }
        
        void* ptr = static_cast<uint8_t*>(blocks_.back()) + alignedOffset;
        currentOffset_ = alignedOffset + bytes;
        totalAllocated_ += bytes;
        
        return ptr;
    }
    
    // Allocate typed array
    template<typename T>
    T* allocate(size_t count = 1) {
        return static_cast<T*>(allocateRaw(count * sizeof(T), alignof(T)));
    }
    
    // Allocate and construct single object
    template<typename T, typename... Args>
    T* create(Args&&... args) {
        T* ptr = allocate<T>(1);
        new (ptr) T(std::forward<Args>(args)...);
        return ptr;
    }
    
    // Allocate and construct array with default construction
    template<typename T>
    T* createArray(size_t count) {
        T* ptr = allocate<T>(count);
        for (size_t i = 0; i < count; ++i) {
            new (&ptr[i]) T();
        }
        return ptr;
    }
    
    // Reset pool (deallocate all at once)
    // Note: Does NOT call destructors - use with POD types or manually destruct
    void reset() {
        // Keep first block, return others
        while (blocks_.size() > 1) {
            pool_detail::aligned_free_impl(blocks_.back());
            blocks_.pop_back();
        }
        currentOffset_ = 0;
        totalAllocated_ = 0;
    }
    
    // Statistics
    size_t totalAllocated() const {
        return totalAllocated_;
    }
    
    size_t numBlocks() const {
        return blocks_.size();
    }
    
    size_t blockSize() const {
        return blockSize_;
    }
    
    size_t currentBlockRemaining() const {
        return blockSize_ - currentOffset_;
    }

private:
    void allocateNewBlock() {
        void* block = pool_detail::aligned_alloc_impl(CACHE_LINE_SIZE, blockSize_);
        if (!block) {
            throw std::bad_alloc();
        }
        blocks_.push_back(block);
        currentOffset_ = 0;
    }
    
    std::vector<void*> blocks_;
    size_t blockSize_;
    size_t currentOffset_;
    size_t totalAllocated_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Thread-Local Pool for OpenMP Compatibility
// ─────────────────────────────────────────────────────────────────────────────

class ThreadLocalPool {
public:
    // Get the thread-local memory pool
    static MemoryPool& get() {
        thread_local MemoryPool pool(512 * 1024);  // 512 KB per thread
        return pool;
    }
    
    // Reset the current thread's pool
    static void reset() {
        get().reset();
    }
    
    // Convenience: allocate from thread-local pool
    template<typename T>
    static T* allocate(size_t count = 1) {
        return get().allocate<T>(count);
    }
    
    // Convenience: create object in thread-local pool
    template<typename T, typename... Args>
    static T* create(Args&&... args) {
        return get().create<T>(std::forward<Args>(args)...);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// STL-Compatible Allocator Adapter
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
class PoolAllocator {
public:
    using value_type = T;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::false_type;
    
    explicit PoolAllocator(MemoryPool& pool) noexcept : pool_(&pool) {}
    
    template<typename U>
    PoolAllocator(const PoolAllocator<U>& other) noexcept : pool_(other.pool_) {}
    
    T* allocate(size_t n) {
        return pool_->allocate<T>(n);
    }
    
    void deallocate(T*, size_t) noexcept {
        // No-op: pool handles bulk deallocation via reset()
    }
    
    template<typename U>
    bool operator==(const PoolAllocator<U>& other) const noexcept {
        return pool_ == other.pool_;
    }
    
    template<typename U>
    bool operator!=(const PoolAllocator<U>& other) const noexcept {
        return !(*this == other);
    }
    
    // Allow access to pool_ from different specializations
    template<typename U>
    friend class PoolAllocator;
    
private:
    MemoryPool* pool_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Scoped Pool Reset Guard
// ─────────────────────────────────────────────────────────────────────────────

// RAII guard that resets pool on scope exit
class PoolResetGuard {
public:
    explicit PoolResetGuard(MemoryPool& pool) : pool_(pool) {}
    
    ~PoolResetGuard() {
        pool_.reset();
    }
    
    // Non-copyable, non-movable
    PoolResetGuard(const PoolResetGuard&) = delete;
    PoolResetGuard& operator=(const PoolResetGuard&) = delete;
    PoolResetGuard(PoolResetGuard&&) = delete;
    PoolResetGuard& operator=(PoolResetGuard&&) = delete;
    
private:
    MemoryPool& pool_;
};

// ═══════════════════════════════════════════════════════════════════════════
// END PHASE 4 OPTIMIZATION #6
// ═══════════════════════════════════════════════════════════════════════════
