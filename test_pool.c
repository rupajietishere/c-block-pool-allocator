#include "pool.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL: %s:%d: assertion failed: %s\n",            \
                    __FILE__, __LINE__, #cond);                               \
            exit(EXIT_FAILURE);                                               \
        }                                                                     \
    } while (0)

typedef struct {
    uint64_t id;
    double value;
    char tag[16];
} Record;

static void test_invalid_inputs_and_null_safety(void) {
    PoolStats stats;
    int dummy = 42;

    TEST_ASSERT(pool_create(0, 10) == NULL);
    TEST_ASSERT(pool_create(16, 0) == NULL);
    TEST_ASSERT(pool_create(SIZE_MAX, 2) == NULL);

    TEST_ASSERT(pool_alloc(NULL) == NULL);
    TEST_ASSERT(pool_free(NULL, &dummy) == false);
    TEST_ASSERT(pool_get_stats(NULL, &stats) == false);

    /* Should be safe no-ops */
    pool_stats(NULL);
    pool_destroy(NULL);

    MemPool *pool = pool_create(32, 4);
    TEST_ASSERT(pool != NULL);
    TEST_ASSERT(pool_free(pool, NULL) == false);
    TEST_ASSERT(pool_get_stats(pool, NULL) == false);
    pool_destroy(pool);

    printf("[PASS] test_invalid_inputs_and_null_safety\n");
}

static void test_normal_alloc_and_free(void) {
    const size_t count = 8;
    MemPool *pool = pool_create(sizeof(Record), count);
    TEST_ASSERT(pool != NULL);

    PoolStats stats;
    TEST_ASSERT(pool_get_stats(pool, &stats));
    TEST_ASSERT(stats.requested_block_size == sizeof(Record));
    TEST_ASSERT(stats.aligned_block_size >= sizeof(Record));
    TEST_ASSERT(stats.total_blocks == count);
    TEST_ASSERT(stats.used_blocks == 0);
    TEST_ASSERT(stats.free_blocks == count);

    Record *items[8];
    for (size_t i = 0; i < count; ++i) {
        items[i] = (Record *)pool_alloc(pool);
        TEST_ASSERT(items[i] != NULL);
        items[i]->id = (uint64_t)(i + 100);
        items[i]->value = (double)i * 1.5;
        snprintf(items[i]->tag, sizeof(items[i]->tag), "item_%zu", i);
    }

    TEST_ASSERT(pool_get_stats(pool, &stats));
    TEST_ASSERT(stats.used_blocks == count);
    TEST_ASSERT(stats.free_blocks == 0);

    /* Make sure writing to one block didn't mess up neighboring blocks */
    for (size_t i = 0; i < count; ++i) {
        char expected[16];
        snprintf(expected, sizeof(expected), "item_%zu", i);
        TEST_ASSERT(items[i]->id == (uint64_t)(i + 100));
        TEST_ASSERT(items[i]->value == (double)i * 1.5);
        TEST_ASSERT(strcmp(items[i]->tag, expected) == 0);
    }

    /* Free in reverse order */
    for (size_t i = count; i > 0; --i) {
        TEST_ASSERT(pool_free(pool, items[i - 1]) == true);
    }

    TEST_ASSERT(pool_get_stats(pool, &stats));
    TEST_ASSERT(stats.used_blocks == 0);
    TEST_ASSERT(stats.free_blocks == count);

    pool_destroy(pool);
    printf("[PASS] test_normal_alloc_and_free\n");
}

static void test_small_block_padding(void) {
    /* Ask for 1-byte blocks to check that the pool pads them for FreeNode & alignment */
    MemPool *pool = pool_create(1, 5);
    TEST_ASSERT(pool != NULL);

    PoolStats stats;
    TEST_ASSERT(pool_get_stats(pool, &stats));
    TEST_ASSERT(stats.requested_block_size == 1);
    TEST_ASSERT(stats.aligned_block_size >= sizeof(void *));

    void *ptrs[5];
    for (size_t i = 0; i < 5; ++i) {
        ptrs[i] = pool_alloc(pool);
        TEST_ASSERT(ptrs[i] != NULL);
        memset(ptrs[i], (int)('A' + i), 1);
    }

    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT(*(unsigned char *)ptrs[i] == (unsigned char)('A' + i));
        TEST_ASSERT(pool_free(pool, ptrs[i]) == true);
    }

    pool_destroy(pool);
    printf("[PASS] test_small_block_padding\n");
}

static void test_exhaustion_and_reuse(void) {
    const size_t count = 4;
    MemPool *pool = pool_create(64, count);
    TEST_ASSERT(pool != NULL);

    void *blocks[4];
    for (size_t i = 0; i < count; ++i) {
        blocks[i] = pool_alloc(pool);
        TEST_ASSERT(blocks[i] != NULL);
    }

    /* Pool should be full now */
    TEST_ASSERT(pool_alloc(pool) == NULL);
    TEST_ASSERT(pool_alloc(pool) == NULL);

    /* Free one block and check that we can grab it right back */
    TEST_ASSERT(pool_free(pool, blocks[2]) == true);
    void *reused = pool_alloc(pool);
    TEST_ASSERT(reused == blocks[2]);

    /* Still full */
    TEST_ASSERT(pool_alloc(pool) == NULL);

    /* Free everything and make sure we can allocate all blocks again */
    blocks[2] = reused;
    for (size_t i = 0; i < count; ++i) {
        TEST_ASSERT(pool_free(pool, blocks[i]) == true);
    }

    for (size_t i = 0; i < count; ++i) {
        blocks[i] = pool_alloc(pool);
        TEST_ASSERT(blocks[i] != NULL);
    }
    TEST_ASSERT(pool_alloc(pool) == NULL);

    for (size_t i = 0; i < count; ++i) {
        TEST_ASSERT(pool_free(pool, blocks[i]) == true);
    }

    pool_destroy(pool);
    printf("[PASS] test_exhaustion_and_reuse\n");
}

static void test_double_free_protection(void) {
    MemPool *pool = pool_create(32, 3);
    TEST_ASSERT(pool != NULL);

    void *p1 = pool_alloc(pool);
    void *p2 = pool_alloc(pool);
    TEST_ASSERT(p1 != NULL && p2 != NULL);

    /* First free should work */
    TEST_ASSERT(pool_free(pool, p1) == true);
    /* Second free of the same pointer should fail without messing up stats */
    TEST_ASSERT(pool_free(pool, p1) == false);

    PoolStats stats;
    TEST_ASSERT(pool_get_stats(pool, &stats));
    TEST_ASSERT(stats.used_blocks == 1);
    TEST_ASSERT(stats.free_blocks == 2);

    /* Grab it again and test freeing once more */
    void *p3 = pool_alloc(pool);
    TEST_ASSERT(p3 == p1);
    TEST_ASSERT(pool_free(pool, p3) == true);
    TEST_ASSERT(pool_free(pool, p3) == false);
    TEST_ASSERT(pool_free(pool, p2) == true);

    pool_destroy(pool);
    printf("[PASS] test_double_free_protection\n");
}

static void test_bounds_and_alignment_validation(void) {
    MemPool *pool1 = pool_create(32, 4);
    MemPool *pool2 = pool_create(32, 4);
    TEST_ASSERT(pool1 != NULL && pool2 != NULL);

    void *p1 = pool_alloc(pool1);
    void *p2 = pool_alloc(pool2);
    TEST_ASSERT(p1 != NULL && p2 != NULL);

    /* Pointer on the stack(out of bounds) */
    int stack_var = 123;
    TEST_ASSERT(pool_free(pool1, &stack_var) == false);

    /* Pointer from regular malloc (out of bounds) */
    void *foreign_heap = malloc(32);
    TEST_ASSERT(foreign_heap != NULL);
    TEST_ASSERT(pool_free(pool1, foreign_heap) == false);
    free(foreign_heap);

    /* Pointer from another pool (out of bounds for pool1) */
    TEST_ASSERT(pool_free(pool1, p2) == false);

    /* Pointer shifted by 1-byte inside a block (misaligned) */
    unsigned char *misaligned = (unsigned char *)p1 + 1;
    TEST_ASSERT(pool_free(pool1, misaligned) == false);

    /* Valid frees should still work */
    TEST_ASSERT(pool_free(pool1, p1) == true);
    TEST_ASSERT(pool_free(pool2, p2) == true);

    pool_destroy(pool1);
    pool_destroy(pool2);
    printf("[PASS] test_bounds_and_alignment_validation\n");
}

static void test_stress_cycles_and_stats_print(void) {
    const size_t cap = 64;
    MemPool *pool = pool_create(48, cap);
    TEST_ASSERT(pool != NULL);

    void *ptrs[64];
    for (int round = 0; round < 50; ++round) {
        for (size_t i = 0; i < cap; ++i) {
            ptrs[i] = pool_alloc(pool);
            TEST_ASSERT(ptrs[i] != NULL);
            memset(ptrs[i], round & 0xFF, 48);
        }
        TEST_ASSERT(pool_alloc(pool) == NULL);

        /* Free every even block, allocate them back, then free everything */
	for (size_t i = 0; i < cap; i += 2) {
            TEST_ASSERT(pool_free(pool, ptrs[i]) == true);
        }
        for (size_t i = 0; i < cap; i += 2) {
            ptrs[i] = pool_alloc(pool);
            TEST_ASSERT(ptrs[i] != NULL);
        }
        for (size_t i = 0; i < cap; ++i) {
            TEST_ASSERT(pool_free(pool, ptrs[i]) == true);
        }
    }

    PoolStats stats;
    TEST_ASSERT(pool_get_stats(pool, &stats));
    TEST_ASSERT(stats.used_blocks == 0);
    TEST_ASSERT(stats.free_blocks == cap);

    printf("\n--- Final Pool Stats Output ---\n");
    pool_stats(pool);
    printf("-------------------------------\n");

    pool_destroy(pool);
    printf("[PASS] test_stress_cycles_and_stats_print\n");
}

int main(void) {
    test_invalid_inputs_and_null_safety();
    test_normal_alloc_and_free();
    test_small_block_padding();
    test_exhaustion_and_reuse();
    test_double_free_protection();
    test_bounds_and_alignment_validation();
    test_stress_cycles_and_stats_print();

    printf("\nAll unit tests passed successfully!\n");
    return EXIT_SUCCESS;
}
