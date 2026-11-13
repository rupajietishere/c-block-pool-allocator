#ifndef POOL_H
#define POOL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque handle representing a fixed-size block memory pool.
 * The internal layout is hidden inside pool.c to preserve encapsulation.
 */
typedef struct MemPool MemPool;

/**
 * Snapshot of current pool usage and configuration metrics.
 * Useful for programmatic verification in unit tests.
 */
typedef struct PoolStats {
    size_t requested_block_size; /* Block size originally requested by caller */
    size_t aligned_block_size;   /* Actual internal stride per block in bytes */
    size_t total_blocks;         /* Total capacity of the pool in blocks       */
    size_t used_blocks;          /* Number of currently allocated blocks       */
    size_t free_blocks;          /* Number of currently available blocks       */
} PoolStats;

/**
 * @brief Creates and initializes a fixed-size block memory pool.
 *
 * Allocates one contiguous chunk of memory capable of holding `block_count`
 * blocks of at least `block_size` bytes each. Internally pads block stride for
 * proper pointer/scalar alignment and free-list management.
 *
 * @param block_size   Size of each block in bytes (must be > 0).
 * @param block_count  Total number of blocks in the pool (must be > 0).
 * @return Pointer to the initialized MemPool, or NULL on invalid arguments,
 *         size overflow, or heap allocation failure.
 */
MemPool *pool_create(size_t block_size, size_t block_count);

/**
 * @brief Allocates a single fixed-size block from the pool in O(1) time.
 *
 * Returned memory is suitably aligned for standard scalar and pointer types,
 * and its contents are uninitialized.
 *
 * @param pool  Pointer to an initialized MemPool.
 * @return Pointer to an available block, or NULL if `pool` is NULL or the
 *         pool is exhausted.
 */
void *pool_alloc(MemPool *pool);

/**
 * @brief Returns a previously allocated block back to the pool in O(1) time.
 *
 * Performs bounds checking, alignment checking, and double-free detection.
 *
 * @param pool  Pointer to an initialized MemPool.
 * @param ptr   Pointer previously returned by pool_alloc() on this same pool.
 * @return true if the block was successfully freed; false if `pool` is NULL,
 *         `ptr` is NULL, `ptr` is out of bounds, `ptr` is misaligned, or the
 *         block has already been freed (double-free protection).
 */
bool pool_free(MemPool *pool, void *ptr);

/**
 * @brief Retrieves current pool usage statistics into a caller-provided struct.
 *
 * @param pool   Pointer to an initialized MemPool.
 * @param stats  Output pointer where statistics will be written.
 * @return true on success, or false if `pool` or `stats` is NULL.
 */
bool pool_get_stats(const MemPool *pool, PoolStats *stats);

/**
 * @brief Prints formatted pool usage statistics to stdout.
 *
 * Safe no-op if `pool` is NULL.
 *
 * @param pool  Pointer to an initialized MemPool.
 */
void pool_stats(const MemPool *pool);

/**
 * @brief Destroys the memory pool and frees all underlying heap memory.
 *
 * Safe no-op if `pool` is NULL. All pointers previously allocated from `pool`
 * become invalid after this call.
 *
 * @param pool  Pointer to the MemPool to destroy (may be NULL).
 */
void pool_destroy(MemPool *pool);

#ifdef __cplusplus
}
#endif

#endif /* POOL_H */
