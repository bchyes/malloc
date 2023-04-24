/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  Blocks are never coalesced or reused.  The size of
 * a block is found at the first aligned word before the block (we need
 * it for realloc).
 *
 * This code is correct and blazingly fast, but very bad usage-wise since
 * it never frees anything.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))

#define WSIZE 4
#define DSIZE 8

#define max(x, y) ((x) > (y) ? (x) : (y))
#define PUT(addr, val) (*((unsigned int *) (addr)) = val)
#define PACK(size, alloc) ((size) | (alloc))
#define GETSIZE(addr) ((*((unsigned int *) (addr))) & ~0x7) //"addr" should be enclosed in brackets
#define GETALLOC(addr) ((*((unsigned int *) (addr))) & 0x1)
#define HEADER(addr) (addr - WSIZE) 
#define FOOTER(addr) (addr + GETSIZE(HEADER(addr)) - DSIZE)
#define PREVBLOCK(addr) (addr - GETSIZE(HEADER(addr) - WSIZE))
#define NEXTBLOCK(addr) (addr + GETSIZE(HEADER(addr)))

#define EXTENDSIZE (1<<8) /* bytes */
#define MINBLOCKSIZE 16

#define SETPREV(addr, prev) (*((unsigned int *) (addr)) = (unsigned int)((long) prev - (long) mem_heap_lo())) //type and size should be careful!!!!
#define SETNEXT(addr, next) (*((unsigned int *) (addr) + 1) = (unsigned int)((long) next - (long) mem_heap_lo())) //pointer plus 1 equals to plus sizeof the type!! So we just need to plus 1 but not WSIZE
#define GETPREV(addr) ((long)(*((unsigned int *) (addr))) + (long) mem_heap_lo())
#define GETNEXT(addr) ((long)(*(((unsigned int *) (addr)) + 1)) + (long) mem_heap_lo())


static char* heap_listp; //use to denote the first block
//static char* prev_listp; //use for denote the prev find block for next fit
static char* free_list_head;
unsigned int *test;

/*
 * insert_to_free_list - Called when we free a block and insert to the beginning of the block
 */
unsigned int* get_free_list_head(int size){
    long i = 0;
    if (size > 4096) i = 8;
    else if (size <= 32) i = 0;
    else if (size <= 64) i = 1;
    else if (size <= 128) i = 2;
    else if (size <= 256) i = 3;
    else if (size <= 512) i = 4;
    else if (size <= 1024) i = 5;
    else if (size <= 2048) i = 6;
    else if (size <= 4096) i = 7;
    return (unsigned int*)(mem_heap_lo() + i * WSIZE);
}

void insert_to_free_list(char *bp){
    if (bp == NULL) return;
    SETPREV(bp, 0);
    SETNEXT(bp, 0); //We need to set 0 because it recently can be part of not free block!!
    unsigned int* now_list_head = get_free_list_head(GETSIZE(HEADER(bp)));// forget Header
    if (!(*now_list_head)){
        PUT(now_list_head, (unsigned int)((long)bp - (long)mem_heap_lo()));
        return;
    }
    unsigned int* next = (unsigned int*)(*now_list_head + mem_heap_lo());
    SETNEXT(bp, (unsigned int)((long) next));
    SETPREV(next, (unsigned int)((long) bp));
    PUT(now_list_head, (unsigned int)((long)bp - (long)mem_heap_lo())); //!
}

/*
 * remove_from_free_list - Called when we use a free block
 */
void remove_from_free_list(char *bp){
    if (bp == NULL) return;
    unsigned int* now_list_head = get_free_list_head(GETSIZE(HEADER(bp)));
    char *prev = (char *)(long)GETPREV(bp); // not unsigned int ?
    char *next = (char *)(long)GETNEXT(bp);
    SETPREV(bp, 0); //We need to this before we find prev and next !!!!
    SETNEXT(bp, 0);
    if (prev == mem_heap_lo() && next == mem_heap_lo()){
        PUT(now_list_head, 0);
    } else if (prev != mem_heap_lo() && next == mem_heap_lo()){
        SETNEXT(prev, 0);
    } else if (prev == mem_heap_lo() && next != mem_heap_lo()){
        SETPREV(next, 0);
        PUT(now_list_head, (unsigned int)((long)next - (long)mem_heap_lo()));
    } else {
        SETNEXT(prev, (unsigned int)((long) next)); //!
        SETPREV(next, (unsigned int)((long) prev));
    }
}

/*
 * coalesce - Called when we try to merge the prev block and next block.
 */
static char* coalesce(char *bp){
    int prev_alloc = GETALLOC(HEADER(PREVBLOCK(bp)));
    int next_alloc = GETALLOC(HEADER(NEXTBLOCK(bp)));
    if (prev_alloc && !next_alloc){ //I write wrong condition first
        remove_from_free_list(NEXTBLOCK(bp));
        int size = GETSIZE(HEADER(bp)) + GETSIZE(HEADER(NEXTBLOCK(bp)));
        PUT(FOOTER(NEXTBLOCK(bp)), PACK(size, 0)); //We must modify FOOTER first!!
        PUT(HEADER(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc){
        remove_from_free_list(PREVBLOCK(bp));
        int size = GETSIZE(HEADER(bp)) + GETSIZE(HEADER(PREVBLOCK(bp)));
        PUT(HEADER(PREVBLOCK(bp)), PACK(size, 0));
        PUT(FOOTER(bp), PACK(size, 0));
        bp = PREVBLOCK(bp); //We must return the coalesce block!!
    } else if (!prev_alloc && !next_alloc){
        remove_from_free_list(PREVBLOCK(bp));
        remove_from_free_list(NEXTBLOCK(bp));
        int size = GETSIZE(HEADER(bp)) + GETSIZE(HEADER(NEXTBLOCK(bp))) + GETSIZE(HEADER(PREVBLOCK(bp)));
        PUT(FOOTER(NEXTBLOCK(bp)), PACK(size, 0));
        PUT(HEADER(PREVBLOCK(bp)), PACK(size, 0));
        bp = PREVBLOCK(bp);
    }
    insert_to_free_list(bp);
    return bp;
}

/*
 * extend_heap - Called when heap has no space.
 */
static char* extend_heap(size_t extend_size){
    char *bp;
    if ((bp = mem_sbrk(extend_size)) == (void *)-1)
        return NULL;
    PUT(HEADER(bp), PACK(extend_size, 0));
    PUT(FOOTER(bp), PACK(extend_size, 0));
    SETPREV(bp, 0);
    SETNEXT(bp, 0);
    PUT(HEADER(NEXTBLOCK(bp)), PACK(0, 1));
    return coalesce(bp);
}

/*
 * mm_init - Called when a new trace starts.
 * Initialize a preamble block and an end block as a boundary
 * Use Implicit free list
 */
int mm_init(void){
    if ((heap_listp = mem_sbrk(12 * WSIZE)) == (void *)-1)
        return -1;
    free_list_head = NULL; //We need to do this else we can just do 1 time but not 12 times
    PUT(heap_listp, 0);           //block size <= 32   
    PUT(heap_listp + (1 * WSIZE), 0); //block size <= 64
    PUT(heap_listp + (2 * WSIZE), 0); //block size <= 128
    PUT(heap_listp + (3 * WSIZE), 0); //block size <= 256
    PUT(heap_listp + (4 * WSIZE), 0); //block size <= 512
    PUT(heap_listp + (5 * WSIZE), 0); //block size <= 1024
    PUT(heap_listp + (6 * WSIZE), 0); //block size <= 2048
    PUT(heap_listp + (7 * WSIZE), 0); //block size <= 4096
    PUT(heap_listp + (8 * WSIZE), 0); //block size > 4096
    PUT(heap_listp + 9 * WSIZE, PACK(DSIZE,1)); //header of the prologue block
    PUT(heap_listp + 10 * WSIZE, PACK(DSIZE,1)); // footer of the prologue block
    PUT(heap_listp + 11 * WSIZE, PACK(0,1)); //header of the epilogue block
    heap_listp += (12 * WSIZE);
    if (extend_heap(EXTENDSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * find_fit - Find a space that is free.
 * Use Next fit
 */
static char* find_fit(size_t size){
    for (unsigned int* now_list_head = get_free_list_head(size); now_list_head != (unsigned int *)(heap_listp - 3 * WSIZE); now_list_head = now_list_head + 1){
        for (char *bp = (char *)(*now_list_head + mem_heap_lo());bp != mem_heap_lo();bp = (char *)((long)GETNEXT(bp))){
            if (GETSIZE(HEADER(bp)) >= size){
                return bp;
            }
        }
    }
    return NULL;
}

/*
 * split_block - When we place a block and have a lot remaining space, then we split a new block to free.
 */
void split_block(char *bp,size_t asize){
    size_t size = GETSIZE(HEADER(bp));
    if (size - asize >= MINBLOCKSIZE){
        PUT(HEADER(bp), PACK(asize, 1));
        PUT(FOOTER(bp), PACK(asize, 1));
        PUT(HEADER(NEXTBLOCK(bp)), PACK(size - asize, 0));
        PUT(FOOTER(NEXTBLOCK(bp)), PACK(size - asize, 0));
        insert_to_free_list(NEXTBLOCK(bp));
    }
}

/*
 * place - Place a block with "size" into a space
 */
void place(char *bp,size_t asize){
    size_t size = GETSIZE(HEADER(bp));
    remove_from_free_list(bp);

    PUT(HEADER(bp), PACK(size, 1));
    PUT(FOOTER(bp), PACK(size, 1));

    split_block(bp, asize);
}

/*
 * malloc - Allocate a block by incrementing the brk pointer.
 *      Always allocate a block whose size is a multiple of the alignment.
 */
void *malloc(size_t size){
    char *bp;
    int newsize = ALIGN(size + SIZE_T_SIZE);
    if ((bp = find_fit(newsize)) != NULL){
        place(bp, newsize);
        return bp;
    } else {
        if ((bp = extend_heap(max(newsize,EXTENDSIZE))) == NULL)
            return NULL;
        place(bp, newsize);
        return bp;
    }
}

/*
 * free - We just change the alloc bit from 0 to 1 and do coalesce
 */
void free(void *ptr){
    if (ptr == NULL) return;
    if (!GETALLOC(HEADER(ptr))) return;
    size_t size = GETSIZE(HEADER(ptr));
    PUT(HEADER(ptr), PACK(size, 0));
    PUT(FOOTER(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * realloc - Change the size of the block by mallocing a new block,
 *      copying its data, and freeing the old block.  I'm too lazy
 *      to do better.
 */
void *realloc(void *oldptr, size_t size){
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        free(oldptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(oldptr == NULL) {
        return malloc(size);
    }

    newptr = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = GETSIZE(HEADER(oldptr));
    if(size < oldsize) oldsize = size;
    memcpy(newptr, oldptr, oldsize);

    /* Free the old block. */
    free(oldptr);
    return newptr;
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc (size_t nmemb, size_t size){
    size_t bytes = nmemb * size;
    void *newptr;

    newptr = malloc(bytes);
    memset(newptr, 0, bytes);

    return newptr;
}

/*
 * mm_checkheap - There are no bugs in my code, so I don't need to check.
 */
void mm_checkheap(int verbose){
    verbose = verbose;
    if (GETSIZE(HEADER(mem_heap_hi() + 1))){
        printf("checkheap error with epilogue not with size 0\n");
    }
    for (char *bp = heap_listp;GETSIZE(HEADER(bp)) != 0;bp += GETSIZE(HEADER(bp))){
        if ((GETSIZE(HEADER(bp)) != GETSIZE(FOOTER(bp))) || (GETALLOC(HEADER(bp)) != GETALLOC(FOOTER(bp)))){
            printf("checkheap error with header and footer not match at %ld\n", (long) bp);
            printf("header at %ld with size %d and alloc %d\n", (long) HEADER(bp),GETSIZE(HEADER(bp)),GETALLOC(HEADER(bp)));
            printf("FOOTER at %ld with size %d and alloc %d\n", (long) FOOTER(bp),GETSIZE(FOOTER(bp)),GETALLOC(FOOTER(bp)));
            //exit(0);
        }
        if (bp != heap_listp && !GETALLOC(HEADER(bp)) && !GETALLOC(HEADER(PREVBLOCK(bp)))){
            printf("checkheap error with two continous heaps are free at %ld %ld\n", (long) PREVBLOCK(bp), (long) bp);
        }
    }
    //printf("CheckHeap success\n");
}
