/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"

#define S(h) (((h) >> 4) << 4) // sz
#define P(h) (((h) >> 1) & 1)  // pal
#define A(h) ((h) & 1)         // al
#define PA(h) ((h) & 3)        // pal al

int initall(size_t size) {
    // wilderness
    if (sf_mem_grow() == NULL)
        return -1;

    // prologue
    char* ptr = (char*)sf_mem_start();
    *((sf_header*)(ptr+8)) = 32+1;  // prologue header
    *((sf_footer*)(ptr+32)) = 32+1; // prologue footer

    // new page
    ptr += 32;
    *((sf_header*)(ptr+8)) = 8192-32-16+2; // new block header
    *((sf_footer*)(ptr+8144)) = 8144+2;    // new block footer
    sf_block* wild = (sf_block*)(ptr+8); // wilderness block

    ptr = (char*)(sf_mem_end());
    *((sf_header*)(ptr-8)) = 1; // epilogue header

    // free list
    for (int i = 0; i < NUM_FREE_LISTS; i++) {
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
    }

    // insert wilderness block
    sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev = wild;
    sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = wild;
    wild->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];
    wild->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS-1];

    return 0;
}

int belonglist(size_t size) {
    int list = 6;
    for (int i = 0, m = 1; i < NUM_FREE_LISTS-2; i++, m*=2) {
        if (size <= 32*m) {
            list = i;
            break;
        }
    }
    return list;
}

void removeblock(sf_block* block) {
    block->body.links.prev->body.links.next = block->body.links.next;
    block->body.links.next->body.links.prev = block->body.links.prev;
}

void insertblock(sf_block* block, int list) {
    block->body.links.prev = &sf_free_list_heads[list];
    block->body.links.next = sf_free_list_heads[list].body.links.next;
    sf_free_list_heads[list].body.links.next->body.links.prev = block;
    sf_free_list_heads[list].body.links.next = block;
}

void *sf_malloc(size_t size) {
    // initialize
    if (sf_free_list_heads[0].body.links.next == NULL && initall(size) == -1)
        return NULL;

    if (size == 0)
        return NULL;
    
    // find block size including header and padding
    size_t blocksize = -24;
    if (size > blocksize)
        size = blocksize;
    blocksize = ((size+8+15)/16)*16;
    if (blocksize < 32)
        blocksize = 32;

    // find which list the block belongs to
    int list = belonglist(blocksize);

    // find a block from free lists to alloc
    sf_block* found = NULL;
    size_t sz;
    for (int i = list; i < NUM_FREE_LISTS; i++) {
        sf_block* tmp = &(sf_free_list_heads[i]);
        while(tmp->body.links.next != &(sf_free_list_heads[i])) {
            tmp = tmp->body.links.next;
            sz = S(tmp->header);
            if ((sz >= blocksize) && (A(tmp->header) == 0)) {
                found = tmp;
                i = NUM_FREE_LISTS;
                break;
            }
        }
    }

    // if no found block
    if (found == NULL) {
        while (1) {
            // expand wilderness
            sf_header epi = *((sf_header*)(((char*)sf_mem_end())-8));
            char* ptr = (char*)sf_mem_grow();
            if (ptr == NULL)
                return NULL;
            
            // process the new mem grow
            sf_block* wild = sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next;
            if (wild == &sf_free_list_heads[NUM_FREE_LISTS-1]) { // new wilderness
                wild = (sf_block*)(ptr-8);
                wild->header = 8192+PA(epi);                 // header
                *((sf_footer*)(ptr+8192-16)) = wild->header; // footer
                insertblock(wild, NUM_FREE_LISTS-1);
            } else { // coalesce with existing wilderness
                wild->header += 8192;                                  // new header
                ptr = (char*)wild;
                *((sf_footer*)(ptr+S(wild->header)-8)) = wild->header; // new footer
            }

            // new epilogue
            ptr = (char*)(sf_mem_end());
            *((sf_header*)(ptr-8)) = 1; // epilogue header

            // found block
            if (wild->header >= blocksize) {
                found = wild;
                break;
            }
        }
    }

    // split found block
    sz = S(found->header); 
    if (sz-32 >= blocksize) {
        char* tmp;
        found->header = (blocksize+PA(found->header)) | 1; // new block header
        tmp = ((char*)found)+blocksize;   // remainder
        sz -= blocksize;                  // remainder size = sz-blocksize
        *((sf_header*)tmp) = sz+2;        // remainder header
        *((sf_footer*)(tmp+sz-8)) = sz+2; // remainder footer

        // disconnect from free list
        removeblock(found);
        if (tmp+sz+8 == (char*)sf_mem_end()) {
            list = 7; // remainder == wilderness
        } else if (sz > 32*32) {
            list = 6;
        } else {
            for (int i = 0, m = 1; i < NUM_FREE_LISTS-2; i++, m*=2) {
                if (sz <= 32*m) {
                    list = i;
                    break;
                }
            }
        }
        // insert remain back to free list
        sf_block* remain = (sf_block*)tmp;
        insertblock(remain, list);
    } else {
        char* proceed;
        found->header |= 1; // al = 1
        proceed = ((char*)found)+S(found->header);
        *((sf_header*)(proceed)) |= 2; // proceed->header.pal = 1
        sz = *((sf_header*)(proceed)); // sz = proceed->header
        if (A(sz) == 0)                // proceed->footer = header
            *((sf_footer*)(proceed+S(sz)-8)) = sz;

        // disconnect from free list
        removeblock(found);
    }
    return (void*)(&(found->body));
}

void sf_free(void *ptr) {
    char* cp = (char*)ptr;
    // NULL pointer or not 16-byte aligned
    if ((ptr == NULL) || (((size_t)ptr)%16 != 0)
        || (cp > ((char*)sf_mem_end())-32)    // block >= epilogue
        || (cp < ((char*)sf_mem_start())+48)) // block <= prologue
        abort();

    // size < 32 or not multiple of 16 or not alloc'd
    sf_block* block = (sf_block*)(cp-8);
    sf_header head = block->header;
    size_t sz = S(head);
    if (sz < 32 || sz%16 != 0 || A(head) == 0
        || (cp+sz-16) > ((char*)sf_mem_end())-16) // block >= epilogue
        abort();

    // check pal
    sf_footer prevfoot = *((sf_footer*)(cp-16));
    sf_header prevhead = prevfoot ^ 1;
    if (cp-S(prevfoot) >= ((char*)sf_mem_start())+48 // check if header in valid range
        && cp-S(prevfoot) <= ((char*)sf_mem_end())-32)
        prevhead = *((sf_header*)(cp-8-S(prevfoot)));
    if ((prevhead == prevfoot && P(head) == 1              // free block
            && (char*)sf_mem_start() != cp-16-S(prevfoot)) //     that's not the prologue
        || (prevhead != prevfoot && P(head) == 0))         // used block
        abort();

    // free block
    block->header -= 1;                        // al = 0
    *((sf_footer*)(cp-16+sz)) = block->header; // footer = header
    int wild = 0; // is next block wilderness

    // coalesce with prev block
    if (P(head) == 0) {
        sf_block* prev = (sf_block*)(cp-8-S(prevfoot));
        removeblock(prev);
        prev->header += sz;
        *((sf_footer*)(cp-16+sz)) = prev->header;
        cp -= S(prevfoot); // cp = payload of coalesced block
        block = prev;
        sz = S(block->header);
    }

    // coalesce with next block
    sf_header nexthead = *((sf_header*)(cp-8+sz));
    wild = (cp-8+sz == ((char*)sf_mem_end())-8) ? 1 : 0;
    if (A(nexthead) == 0) {
        sf_block* next = (sf_block*)(cp-8+sz);
        wild = (next->body.links.next == &sf_free_list_heads[NUM_FREE_LISTS-1]) ? 1 : 0;
        removeblock(next);
        block->header += S(next->header);
        *((sf_footer*)(cp-16+S(block->header))) = block->header;
    }

    *((sf_header*)(cp-8+sz)) &= 0xFFFFFFFFFFFFFF1; // pal of next block = 0
    wild = (wild == 1) ? (NUM_FREE_LISTS-1) : belonglist(S(block->header));
    insertblock(block, wild);
}

void *sf_realloc(void *ptr, size_t size) {
    char* cp = (char*)ptr;
    // NULL pointer or not 16-byte aligned
    if ((ptr == NULL) || (((size_t)ptr)%16 != 0)
        || (cp > ((char*)sf_mem_end())-32)      // block >= epilogue
        || (cp < ((char*)sf_mem_start())+48)) { // block <= prologue
        sf_errno = EINVAL;
        return NULL;
    }

    // size < 32 or not multiple of 16 or not alloc'd
    sf_block* block = (sf_block*)(cp-8);
    sf_header head = block->header;
    size_t sz = S(head);
    if (sz < 32 || sz%16 != 0 || A(head) == 0
        || (cp+sz-16) > ((char*)sf_mem_end())-16) { // block >= epilogue
        sf_errno = EINVAL;
        return NULL;
    }

    // check pal
    sf_footer prevfoot = *((sf_footer*)(cp-16));
    sf_header prevhead = prevfoot ^ 1;
    if (cp-S(prevfoot) >= ((char*)sf_mem_start())+48 // check if header in valid range
        && cp-S(prevfoot) <= ((char*)sf_mem_end())-32)
        prevhead = *((sf_header*)(cp-8-S(prevfoot)));
    if ((prevhead == prevfoot && P(head) == 1              // free block
            && (char*)sf_mem_start() != cp-16-S(prevfoot)) //     that's not the prologue
        || (prevhead != prevfoot && P(head) == 0)) {       // used block
        sf_errno = EINVAL;
        return NULL;
    }

    if (size == 0) {
        sf_free(ptr);
        return NULL;
    }

    size_t newsize = -24;
    newsize = (size < newsize) ? size : newsize;
    newsize = ((newsize+8+15)/16)*16;
    newsize = (newsize < 32) ? 32 : newsize;
    sf_block* newblock;

    // reallocate to a larger size
    if (newsize > sz) {
        char* tmp = (char*)sf_malloc(size);
        if (tmp == NULL)
            return NULL;
        newblock = (sf_block*)(tmp-8);
        memcpy(tmp, ptr, sz-16); // copy payload
        sf_free(ptr); // free original block
        ptr = tmp;
    }

    // reallocate to a smaller size
    if (newsize <= sz-32 && sz >= 64) {
        newblock = block;
        newblock->header = newsize+PA(newblock->header);
        block = (sf_block*)(cp-8+newsize); // block to be freed
        block->header = sz-newsize+2;
        *((sf_footer*)(cp-16+newsize+S(block->header))) = block->header;
        sf_block* next = (sf_block*)(cp-8+sz);
        next->header = S(next->header) + A(next->header);
        int list = 0;

        // coalesce with next block
        sf_header nexthead = *((sf_header*)(cp-8+sz));
        if (A(nexthead) == 0) {
            removeblock(next);
            block->header += S(next->header);
            *((sf_footer*)(cp-16+newsize+S(block->header))) = block->header;
            list = (next->body.links.next == &sf_free_list_heads[NUM_FREE_LISTS-1]) ? 1 : 0;
        }
        
        // is block wilderness
        cp = ((char*)block)+S(block->header);
        if (cp == (char*)(sf_mem_end())-8)
            list = 1;
        list = (list == 1) ? 7 : belonglist(S(block->header));
        insertblock(block, list);
    }

    return (void*)ptr;
}

void *sf_memalign(size_t size, size_t align) {
    // align < minimum block size
    if (align < 32) {
        sf_errno = EINVAL;
        return NULL;
    }

    // align not power of 2;
    if ((align & (align-1)) != 0) {
        sf_errno = EINVAL;
        return NULL;
    }

    if (size == 0)
        return NULL;

    char* ptr = (char*)sf_malloc(size+align+32); // payload of whole block
    if (ptr == NULL)
        return NULL;
    sf_block* block = (sf_block*)(ptr-8);
    size_t totalsize = S(block->header);
    size = ((size+8+15)/16)*16;
    size = (size < 32) ? 32 : size;
    char* aligned = (char*)(((((size_t)(ptr))+align-1)/align)*align); // aligned payload
    if ((aligned-ptr < 32) && aligned != ptr)
        aligned += align;

    // free front
    if (aligned-ptr >= 32) {
        block->header = ((size_t)(aligned-ptr))+PA(block->header); // header
        block = (sf_block*)(aligned-8); // header of aligned block
        block->header = size+3; // set pal and al of aligned block to 1
        totalsize -= (aligned-ptr);
        sf_free(ptr);
    }

    int pa = PA(block->header);
    block->header = (totalsize-size < 32) ? totalsize+pa : size+pa; // header of aligned

    // free back
    if (totalsize-size >= 32) {
        aligned += size;
        sf_block* free = (sf_block*)(aligned-8);
        free->header = totalsize-size+2;               // header
        size_t sz = S(free->header);
        *((sf_footer*)(aligned-16+sz)) = free->header; // footer

        // coalesce with next block
        sf_header nexthead = *((sf_header*)(aligned-8+sz));
        int wild = (aligned-8+totalsize-size == ((char*)sf_mem_end())-8) ? 1 : 0;
        if (A(nexthead) == 0) {
            sf_block* next = (sf_block*)(aligned-8+sz);
            wild = (next->body.links.next == &sf_free_list_heads[NUM_FREE_LISTS-1]) ? 1 : 0;
            removeblock(next);
            free->header += S(next->header);
            *((sf_footer*)(aligned-16+S(free->header))) = free->header;
            *((sf_footer*)(aligned-8+S(free->header))) &= 0xFFFFFFFFFFFFFF1;
            wild = (wild == 1) ? (NUM_FREE_LISTS-1) : belonglist(S(free->header));
            insertblock(free, wild);
        }
    }

    return (void*)(((char*)block)+8);
}
