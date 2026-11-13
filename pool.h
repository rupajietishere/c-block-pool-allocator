#ifndef POOL_H
#define POOL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle for the memory pool (actual struct is hidden inside pool.c) */
typedef struct MemPool MemPool;

/* Holds current pool numbers/sizes - mainly used in tests to check counts */
typedef struct PoolStats {
    size_t requested_block_size; /* size the user asked for */
    size_t aligned_block_size;   /* actual block size after padding/alignment */
    size_t total_blocks;         /* total blocks in the pool */
    size_t used_blocks;          /* blocks currently in use */
    size_t free_blocks;          /* blocks still free */
} PoolStats;

/*
 * Creates a pool with `block_count` blocks of at least `block_size` bytes each.
 * Allocates one big chunk via malloc() and pads block size for alignment and
 * the internal free list.
 * Returns NULL if size/count is 0, on size overflow, or if malloc fails.
 */
MemPool *pool_create(size_t block_size, size_t block_count);

/*
 * Pops and returns one free block in O(1) time.
 * The block is properly aligned, though its contents are uninitialized.
 * Returns NULL if `pool` is NULL or if no blocks are left.
 */
void *pool_alloc(MemPool *pool);

/*
 * Puts a previously allocated block back into the pool in O(1) time.
 * Checks for NULL, out-of-bounds pointers, misaligned pointers, and double-frees.
 * Returns true if freed, or false if anything was invalid.
 */
bool pool_free(MemPool *pool, void *ptr);

/*
 * Fills `stats` with the pool's current stats.
 * Returns false if `pool` or `stats` is NULL.
 */
bool pool_get_stats(const MemPool *pool, PoolStats *stats);

/*
 * Prints the pool stats to stdout (does nothing if `pool` is NULL).
 */
void pool_stats(const MemPool *pool);

/*
 * Frees all heap memory used by the pool (safe to call with NULL).
 * Any pointers previously returned by pool_alloc() shouldn't be used after this.
 */
void pool_destroy(MemPool *pool);

#ifdef __cplusplus
}
#endif

#endif /* POOL_H */
