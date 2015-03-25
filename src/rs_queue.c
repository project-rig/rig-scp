#include <stdlib.h>
#include <stdbool.h>

#include <rs_queue.h>
#include <rs_scp.h>

/**
 * The size to use for the initial allocation of queue entries.
 */
#define RS_Q_FIRST_BLOCK_SIZE 8


/**
 * A linked list of blocks of memory allocated to storing rs__q_entry_t.
 */
struct rs__q_block;
typedef struct rs__q_block rs__q_block_t;
struct rs__q_block {
	// Pointer to the allocated block
	rs__q_entry_t *block;
	
	// Size of the block (in entries)
	size_t size;
	
	// Pointer to the next allocated block or NULL if no more blocks are
	// allocated.
	rs__q_block_t *next;
};


struct rs__q {
	// Pointers to the head and tail of the queue. Items are inserted into the
	// head of the queue and taken from the tail. If tail points to an empty
	// entry, the queue is empty. The head must always point to an empty entry.
	// When an item is inserted the new head becomes head->next. If the new head
	// is not empty, the queue must be enlarged. The tail points at the next entry
	// to remove and when an item is removed, the new tail becomes tail->next. If
	// the tail is empty, head == tail and the queue is empty.
	rs__q_entry_t *head;
	rs__q_entry_t *tail;
	
	// A linked list of blocks of memory allocated to support the queue
	rs__q_block_t *blocks;
};


/**
 * Initialise the entries of a block of queue entries as a linked list.
 * Also marks the entry as empty.
 * Doesn't initialise the last "next" pointer.
 */
void
rs__q_block_init(rs__q_entry_t *entries, size_t n_entries) {
	size_t i;
	for (i = 0; i < n_entries; i++) {
		entries[i].empty = true;
		
		if (i < n_entries - 1)
			entries[i].next = &(entries[i + 1]);
	}
}


rs__q_t *
rs__q_init(void)
{
	rs__q_t *q = malloc(sizeof(rs__q_t));
	if (!q) return NULL;
	
	// Allocate the initial block of queue entries
	q->blocks = malloc(sizeof(rs__q_block_t));
	if (!q->blocks) {
		free(q);
		return NULL;
	}
	q->blocks->next = NULL;
	q->blocks->size = RS_Q_FIRST_BLOCK_SIZE;
	q->blocks->block = calloc(RS_Q_FIRST_BLOCK_SIZE, sizeof(rs__q_entry_t));
	if (!q->blocks->block) {
		free(q->blocks);
		free(q);
		return NULL;
	}
	
	// Initialise the block as a closed loop
	rs__q_block_init(q->blocks->block, q->blocks->size);
	q->blocks->block[q->blocks->size - 1].next = &(q->blocks->block[0]);
	
	// Set the head and tail to point to the same (empty) entries
	q->head = q->blocks->block;
	q->tail = q->blocks->block;
	
	return q;
}


rs__q_entry_t *
rs__q_insert(rs__q_t *q)
{
	// Allocate more buffer space if the queue would become full upon inserting
	// this item
	if (!q->head->next->empty) {
		// Allocate storage for the new block, making the new allocation twice as
		// large as the last one.
		rs__q_block_t *new_block = malloc(sizeof(rs__q_block_t));
		if (!new_block) return NULL;
		new_block->size = q->blocks->size * 2;
		new_block->block = calloc(new_block->size, sizeof(rs__q_entry_t));
		if (!new_block->block) {
			free(new_block);
			return;
		}
		
		// Initialise the new block and insert it into the queue's linked list
		rs__q_block_init(new_block->block, new_block->size);
		new_block->block[new_block->size - 1].next = q->head->next;
		q->head->next = &(new_block->block[0]);
		
		// Insert the new block into to the linked list of blocks
		new_block->next = q->blocks;
		q->blocks = new_block;
	}
	
	// Advance the head of the queue and return the entry which was previously at
	// the head.
	rs__q_entry_t *entry = q->head;
	entry->empty = false;
	q->head = q->head->next;
	return entry;
}


rs__q_entry_t *
rs__q_remove(rs__q_t *q)
{
	rs__q_entry_t *entry = q->tail;
	
	// Advance the tail of the queue and return the entry if it is not empty.
	if (!entry->empty) {
		entry->empty = true;
		q->tail = q->tail->next;
		return entry;
	} else {
		return NULL;
	}
}


rs__q_entry_t *
rs__q_peek(rs__q_t *q)
{
	if (!q->tail->empty) {
		return q->tail;
	} else {
		return NULL;
	}
}


void
rs__q_free(rs__q_t *q)
{
	// Free the queue blocks
	while (q->blocks) {
		rs__q_block_t *block = q->blocks;
		free(block->block);
		q->blocks = block->next;
		free(block);
	}
	
	// Free the queue structure
	free(q);
}
