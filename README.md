# c-block-pool-allocator

A deterministic, `O(1)` fixed-size block memory pool allocator written in C11.  
It requests a single contiguous chunk of heap memory via `malloc()`, slices it into equally spaced, maximally aligned blocks, and manages allocation and deallocation using an intrusive free list paired with a lightweight allocation bitmap for `O(1)` double-free protection.

---

## Repository Structure

```text
c-block-pool-allocator/
├── pool.h          # Public API declarations, types, and documentation
├── pool.c          # Core allocator implementation (free list + bitmap)
├── test_pool.c     # Unit tests (normal alloc/free, exhaustion, double-free, bounds)
├── Makefile        # Builds with -std=c11 -Wall -Wextra -Werror -pedantic -g + Valgrind
└── README.md       # Architecture, design trade-offs, and verification output
```

---

## Architecture

```text
+-------------------------------------------------------------------+
|                           MemPool (heap)                          |
+-------------------------------------------------------------------+
| free_list ----+                                                   |
| used_bitmap --|----------------------------------+                |
| memory -------|----+                             |                |
| aligned_size  |    |                             |                |
| total_blocks  |    v                             v                |
+---------------|----|-----------------------------|----------------+
                |    |                             |
                |    |  Contiguous Chunk (malloc)  |  1-bit/block
                |    v                             v  Bitmap (calloc)
                |  +------------+------------+------------+  +-----+
                +->| Block 0    | Block 1    | Block 2    |  | 010 |
                   | [next *] --+-> [user]   | [next *] ->|  +-----+
                   |   (free)   |  (in use)  |   (free)   |
                   +------------+------------+------------+
                     ^                        ^
                     |                        |
                     +--- Free blocks link ---+ (intrusive FreeNode)
```

### How It Works
1. **Initialization (`pool_create`)**:
   - Pads `block_size` to at least `sizeof(void *)` and rounds up to portable maximum scalar/pointer alignment (`POOL_ALIGNMENT`).
   - Allocates one contiguous chunk of `block_count * aligned_size` bytes and threads every block into an intrusive singly-linked free list (`FreeNode`).
   - Allocates a 1-bit-per-block `used_bitmap` initialized to `0`.
2. **Allocation (`pool_alloc`) — `O(1)`**:
   - Pops the head block from `free_list`, marks its bit in `used_bitmap`, increments `used_blocks`, and returns the pointer.
   - Returns `NULL` immediately when the pool is exhausted.
3. **Deallocation (`pool_free`) — `O(1)`**:
   - **Bounds check**: Verifies via `uintptr_t` arithmetic that `ptr` lies within `[memory, memory + total_bytes)`.
   - **Alignment check**: Verifies `(addr - base) % aligned_size == 0` to reject interior/misaligned pointers.
   - **Double-free check**: Checks `used_bitmap` in `O(1)` time; rejects already-freed or never-allocated blocks without corrupting `free_list`.
   - Clears the block's bit and pushes it back onto the head of `free_list`.

---

## Design Trade-Offs

| Decision | Benefit | Trade-Off |
| :--- | :--- | :--- |
| **Fixed-size blocks** | Zero external fragmentation; predictable `O(1)` alloc/free. | Callers storing smaller payloads incur internal fragmentation. |
| **Intrusive free list** | Zero extra per-block memory overhead for linked-list pointers. | Minimum internal block stride is clamped to `sizeof(void *)` and aligned. |
| **1-bit allocation bitmap** | Enables `O(1)` double-free detection without scanning the free list. | Uses `ceil(block_count / 8)` extra bytes of metadata. |
| **Fixed pool capacity** | Deterministic memory footprint with no hidden OS reallocations. | Pool returns `NULL` on exhaustion rather than growing dynamically. |
| **Single-threaded design** | No lock contention or mutex overhead on every alloc/free. | Concurrent callers must synchronize externally or use one pool per thread. |

---

## Usage Example

```c
#include "pool.h"
#include <stdio.h>

typedef struct {
    int id;
    double score;
} Item;

int main(void) {
    MemPool *pool = pool_create(sizeof(Item), 16);
    if (!pool) {
        return 1;
    }

    Item *a = (Item *)pool_alloc(pool);
    if (a) {
        a->id = 1;
        a->score = 99.5;
    }

    pool_stats(pool);

    pool_free(pool, a);
    pool_destroy(pool);
    return 0;
}
```

---

## Building, Testing & Memory Verification

### Prerequisites
- `gcc` (C11 support)
- `make`
- `valgrind` (for leak & memory-error checking)

### Commands
```bash
make test       # Compile with strict warnings and run unit tests
make valgrind   # Run unit tests under Valgrind Memcheck
make asan       # Run with AddressSanitizer & UndefinedBehaviorSanitizer
make clean      # Remove build artifacts
```

### Verified Test & Valgrind Output
```text
$ make valgrind
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./test_pool
==22842== Memcheck, a memory error detector
==22842== Copyright (C) 2002-2024, and GNU GPL'd, by Julian Seward et al.
==22842== Using Valgrind-3.26.0 and LibVEX; rerun with -h for copyright info
==22842== Command: ./test_pool
==22842== 
[PASS] test_invalid_inputs_and_null_safety
[PASS] test_normal_alloc_and_free
[PASS] test_small_block_padding
[PASS] test_exhaustion_and_reuse
[PASS] test_double_free_protection
[PASS] test_bounds_and_alignment_validation

--- Final Pool Stats Output ---
MemPool Stats:
  Block size (requested / aligned): 48 / 48 bytes
  Total blocks: 64
  Used blocks:  0
  Free blocks:  64
-------------------------------
[PASS] test_stress_cycles_and_stats_print

All unit tests passed successfully!
==22842== 
==22842== HEAP SUMMARY:
==22842==     in use at exit: 0 bytes in 0 blocks
==22842==   total heap usage: 26 allocs, 26 frees, 5,727 bytes allocated
==22842== 
==22842== All heap blocks were freed -- no leaks are possible
==22842== 
==22842== For lists of detected and suppressed errors, rerun with: -s
==22842== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```
