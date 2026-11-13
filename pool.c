#include "pool.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Stored inside the first few bytes of a block when it's free */
typedef struct FreeNode {
    struct FreeNode *next;
} FreeNode;

/* Helper struct to figure out max alignment needed on this system */
struct AlignProbe {
    char c;
    union {
        long long ll;
        long double ld;
        void *p;
        void (*fp)(void);
    } u;
};

#define POOL_ALIGNMENT offsetof(struct AlignProbe, u)

struct MemPool {
    unsigned char *memory; /* big contiguous chunk split into blocks */
    uint8_t *used_bitmap;  /* 1 bit per block: 1 = in use, 0 = free */
    FreeNode *free_list;   /* head of the free list */
    size_t requested_size; /* original block_size passed to pool_create */
    size_t aligned_size;   /* padded size per block */
    size_t total_blocks;   /* total number of blocks */
    size_t used_blocks;    /* how many blocks are currently allocated */
    size_t total_bytes;    /* total byte size of `memory` */
};

static inline bool bitmap_is_set(const uint8_t *bitmap, size_t idx) {
    return (bitmap[idx / 8] & (uint8_t)(1U << (idx % 8))) != 0;
}

static inline void bitmap_set(uint8_t *bitmap, size_t idx) {
    bitmap[idx / 8] = (uint8_t)(bitmap[idx / 8] | (uint8_t)(1U << (idx % 8)));
}

static inline void bitmap_clear(uint8_t *bitmap, size_t idx) {
    bitmap[idx / 8] = (uint8_t)(bitmap[idx / 8] & (uint8_t)~(1U << (idx % 8)));
}

MemPool *pool_create(size_t block_size, size_t block_count) {
    if (block_size == 0 || block_count == 0) {
        return NULL;
    }

    /* Every free block needs enough room to store a FreeNode pointer */
    size_t min_size = (block_size < sizeof(FreeNode)) ? sizeof(FreeNode) : block_size;
    size_t align = POOL_ALIGNMENT;

    if (min_size > SIZE_MAX - (align - 1)) {
        return NULL;
    }
    size_t aligned_size = ((min_size + align - 1) / align) * align;

    if (block_count > SIZE_MAX / aligned_size) {
        return NULL;
    }
    size_t total_bytes = block_count * aligned_size;
    size_t bitmap_bytes = (block_count / 8) + ((block_count % 8 != 0) ? 1 : 0);

    MemPool *pool = malloc(sizeof(MemPool));
    if (pool == NULL) {
        return NULL;
    }

    pool->memory = malloc(total_bytes);
    if (pool->memory == NULL) {
        free(pool);
        return NULL;
    }

    pool->used_bitmap = calloc(bitmap_bytes, 1);
    if (pool->used_bitmap == NULL) {
        free(pool->memory);
        free(pool);
        return NULL;
    }

    pool->requested_size = block_size;
    pool->aligned_size = aligned_size;
    pool->total_blocks = block_count;
    pool->used_blocks = 0;
    pool->total_bytes = total_bytes;

    /* Link all blocks together in order to build the initial free list */
    for (size_t i = 0; i < block_count; ++i) {
        FreeNode *curr = (FreeNode *)(pool->memory + (i * aligned_size));
        curr->next = (i + 1 < block_count)
                         ? (FreeNode *)(pool->memory + ((i + 1) * aligned_size))
                         : NULL;
    }
    pool->free_list = (FreeNode *)pool->memory;

    return pool;
}

void *pool_alloc(MemPool *pool) {
    if (pool == NULL || pool->free_list == NULL) {
        return NULL;
    }

    FreeNode *block = pool->free_list;
    pool->free_list = block->next;

    uintptr_t offset = (uintptr_t)block - (uintptr_t)pool->memory;
    size_t idx = (size_t)(offset / pool->aligned_size);
    bitmap_set(pool->used_bitmap, idx);
    pool->used_blocks++;

    return (void *)block;
}

bool pool_free(MemPool *pool, void *ptr) {
    if (pool == NULL || ptr == NULL) {
        return false;
    }

    uintptr_t base = (uintptr_t)pool->memory;
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t end = base + pool->total_bytes;

    /* Check if ptr is actually inside our pool's memory chunk */
    if (addr < base || addr >= end) {
        return false;
    }

    /* Check if ptr sits right at the start of a block (not in the middle) */
    uintptr_t offset = addr - base;
    if (offset % pool->aligned_size != 0) {
        return false;
    }

    /* Make sure this block isn't already free (catches double-frees) */
    size_t idx = (size_t)(offset / pool->aligned_size);
    if (!bitmap_is_set(pool->used_bitmap, idx)) {
        return false;
    }

    bitmap_clear(pool->used_bitmap, idx);

    FreeNode *node = (FreeNode *)ptr;
    node->next = pool->free_list;
    pool->free_list = node;
    pool->used_blocks--;

    return true;
}

bool pool_get_stats(const MemPool *pool, PoolStats *stats) {
    if (pool == NULL || stats == NULL) {
        return false;
    }

    stats->requested_block_size = pool->requested_size;
    stats->aligned_block_size = pool->aligned_size;
    stats->total_blocks = pool->total_blocks;
    stats->used_blocks = pool->used_blocks;
    stats->free_blocks = pool->total_blocks - pool->used_blocks;
    return true;
}

void pool_stats(const MemPool *pool) {
    if (pool == NULL) {
        return;
    }

    PoolStats stats;
    pool_get_stats(pool, &stats);

    printf("MemPool Stats:\n"
           "  Block size (requested / aligned): %zu / %zu bytes\n"
           "  Total blocks: %zu\n"
           "  Used blocks:  %zu\n"
           "  Free blocks:  %zu\n",
           stats.requested_block_size,
           stats.aligned_block_size,
           stats.total_blocks,
           stats.used_blocks,
           stats.free_blocks);
}

void pool_destroy(MemPool *pool) {
    if (pool == NULL) {
        return;
    }

    free(pool->used_bitmap);
    free(pool->memory);
    free(pool);
}
