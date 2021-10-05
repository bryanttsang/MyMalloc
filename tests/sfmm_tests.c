#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include "debug.h"
#include "sfmm.h"
#define TEST_TIMEOUT 15

/*
 * Assert the total number of free blocks of a specified size.
 * If size == 0, then assert the total number of all free blocks.
 */
void assert_free_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	while(bp != &sf_free_list_heads[i]) {
	    if(size == 0 || size == (bp->header & ~0xf)) {
		cnt++;
	    }
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of free blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of free blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

Test(sfmm_basecode_suite, malloc_an_int, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz = 4;
	int *x = sf_malloc(sz);

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");

	assert_free_block_count(0, 1);
	assert_free_block_count(8112, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + 8192 == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_basecode_suite, malloc_four_pages, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;

	// We want to allocate up to exactly four pages, so there has to be space
	// for the header, footer, and the link pointers.
	void *x = sf_malloc(32704);
	cr_assert_not_null(x, "x is NULL!");
	assert_free_block_count(0, 0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
}

Test(sfmm_basecode_suite, malloc_too_large, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	void *x = sf_malloc(524288);

	cr_assert_null(x, "x is not NULL!");
	assert_free_block_count(0, 1);
	assert_free_block_count(131024, 1);
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sfmm_basecode_suite, free_no_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_x = 8, sz_y = 200, sz_z = 1;
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);

	assert_free_block_count(0, 2);
	assert_free_block_count(208, 1);
	assert_free_block_count(7872, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, free_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_w = 8, sz_x = 200, sz_y = 300, sz_z = 4;
	/* void *w = */ sf_malloc(sz_w);
	void *x = sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);
	sf_free(x);

	assert_free_block_count(0, 2);
	assert_free_block_count(528, 1);
	assert_free_block_count(7552, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, freelist, .timeout = TEST_TIMEOUT) {
        size_t sz_u = 200, sz_v = 300, sz_w = 200, sz_x = 500, sz_y = 200, sz_z = 700;
	void *u = sf_malloc(sz_u);
	/* void *v = */ sf_malloc(sz_v);
	void *w = sf_malloc(sz_w);
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(u);
	sf_free(w);
	sf_free(y);

	assert_free_block_count(0, 4);
	assert_free_block_count(208, 3);
	assert_free_block_count(5968, 1);

	// First block in list should be the most recently freed block.
	int i = 3;
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	cr_assert_eq(bp, (char *)y - 8,
		     "Wrong first block in free list %d: (found=%p, exp=%p)",
                     i, bp, (char *)y - 8);
}

Test(sfmm_basecode_suite, realloc_larger_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int), sz_y = 10, sz_x1 = sizeof(int) * 20;
	void *x = sf_malloc(sz_x);
	/* void *y = */ sf_malloc(sz_y);
	x = sf_realloc(x, sz_x1);

	cr_assert_not_null(x, "x is NULL!");
	sf_block *bp = (sf_block *)((char *)x - 8);
	cr_assert(bp->header & 0x1, "Allocated bit is not set!");
	cr_assert((bp->header & ~0xf) == 96,
		  "Realloc'ed block size (0x%ld) not what was expected (0x%ld)!",
		  bp->header & ~0xf, 96);

	assert_free_block_count(0, 2);
	assert_free_block_count(32, 1);
	assert_free_block_count(7984, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_splinter, .timeout = TEST_TIMEOUT) {
        size_t sz_x = 80, sz_y = 64;
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");
	cr_assert(x == y, "Payload addresses are different!");

	sf_block *bp = (sf_block *)((char *)x - 8);
	cr_assert(bp->header & 0x1, "Allocated bit is not set!");
	cr_assert((bp->header & ~0xf) == 96, "Block size not what was expected!");

	// There should be only one free block.
	assert_free_block_count(0, 1);
	assert_free_block_count(8048, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_free_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = 64, sz_y = 8;
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");

	sf_block *bp = (sf_block *)((char *)x - 8);
	cr_assert(bp->header & 0x1, "Allocated bit is not set!");
	cr_assert((bp->header & ~0xf) == 32, "Realloc'ed block size not what was expected!");

	// After realloc'ing x, we can return a block of size 48
	// to the freelist.  This block will go into the main freelist and be coalesced.
	assert_free_block_count(0, 1);
	assert_free_block_count(8112, 1);
}
//############################################
//STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
//DO NOT DELETE THESE COMMENTS
//############################################

Test(sfmm_basecode_suite, realloc_free, .timeout = TEST_TIMEOUT) {
	size_t sz = 1;
	void* p = sf_malloc(sz);
	
	cr_assert_not_null(p, "p is NULL!");

	p = sf_realloc(p, 0);

	assert_free_block_count(0, 1);
}

Test(sfmm_basecode_suite, memalign_simple, .timeout = TEST_TIMEOUT) {
	size_t sz = 1;
	size_t aln = 64;
	void* p = sf_memalign(sz, aln);
	
	cr_assert_not_null(p, "p is NULL!");

	sf_block* b = (sf_block *)((char *)p - 8);
	cr_assert(b->header & 0x1, "Allocated bit is not set!");
	cr_assert((b->header & ~0xf) == 32, "Realloc'ed block size not what was expected!");

	assert_free_block_count(8112, 1);
}

Test(sfmm_basecode_suite, memalign_split_front, .timeout = TEST_TIMEOUT) {
	size_t sz = 32;
	void* p = sf_malloc(sz);
	
	cr_assert_not_null(p, "p == NULL!");
	sf_block* pb = (sf_block *)((char *)p - 8);

	cr_assert(pb->header & 0x1, "Allocated bit is not set!");
	cr_assert((pb->header & ~0xf) == 48, "Alloc'ed block size not what was expected!");


	void* pm = sf_memalign(sz, sz);
	cr_assert_not_null(pm, "pm == NULL!");
	sf_block* pmb = (sf_block *)((char *)pm - 8);

	cr_assert(pmb->header & 0x1, "Allocated bit is not set!");
	cr_assert((pmb->header & ~0xf) == 64, "Alloc'ed block size not what was expected!");

	assert_free_block_count(48, 1);   // front split
	assert_free_block_count(7984, 1); // wilderness
}

Test(sfmm_basecode_suite, memalign_split_back, .timeout = TEST_TIMEOUT) {
	char* p1 = sf_malloc(1); p1+=0;
	char* p2 = sf_malloc(999);
	char* p3 = sf_malloc(1); p3+=0;
	sf_free(p2);
	
	void* pm = sf_memalign(444, 32);
	cr_assert_not_null(pm, "pm == NULL!");
	sf_block* pmb = (sf_block *)(pm - 8);

	cr_assert(pmb->header & 0x1, "Allocated bit is not set!");
	cr_assert((pmb->header & ~0xf) == 464, "Alloc'ed block size not what was expected!");

	assert_free_block_count(544, 1);  // back split
	assert_free_block_count(7072, 1); // wilderness
}

Test(sfmm_basecode_suite, memalign_split_both, .timeout = TEST_TIMEOUT) {
	char* p1 = sf_malloc(1); p1+=0;
	char* p2 = sf_malloc(999);
	char* p3 = sf_malloc(1); p3+=0;
	sf_free(p2);
	
	void* pm = sf_memalign(444, 128);
	cr_assert_not_null(pm, "pm == NULL!");
	sf_block* pmb = (sf_block *)(pm - 8);

	cr_assert(pmb->header & 0x1, "Allocated bit is not set!");
	cr_assert((pmb->header & ~0xf) == 464, "Alloc'ed block size not what was expected!");

	assert_free_block_count(32, 1);   // front split
	assert_free_block_count(512, 1);  // back split
	assert_free_block_count(7072, 1); // wilderness
}
