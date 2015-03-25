#include <stdlib.h>
#include <stdbool.h>

#include <rs_queue.h>
#include <rs_scp.h>

/**
 * Calculate the pointer to the rs__q_entry_t in the given block at the
 * specified index.
 *
 * @param rs__q_t *q
 * @param rs__q_block_t *blk
 * @param int entry
 * @returns rs__q_entry_t *
 */
#define BLOCK_ENTRY(q, blk, entry) \
	((rs__q_entry_t *)((blk->block) + ((entry) * ((q)->data_size))))

/**
 * Initialise the entries of a block of queue entries as a linked list.
 * Also marks the entry as empty.
 * Doesn't initialise the last "next" pointer.
 */
void
rs__q_block_init(rs__q_t *q, rs__q_block_t *block) {
	size_t i;
	for (i = 0; i < block->size; i++) {
		BLOCK_ENTRY(q, block, i)->empty = true;
		
		if (i < block->size - 1)
			BLOCK_ENTRY(q, block, i)->next = BLOCK_ENTRY(q, block, i + 1);
	}
}


rs__q_t *
rs__q_init(size_t data_size)
{
	rs__q_t *q = malloc(sizeof(rs__q_t));
	if (!q) return NULL;
	
	q->data_size = data_size;
	
	// Allocate the initial block of queue entries
	q->blocks = malloc(sizeof(rs__q_block_t));
	if (!q->blocks) {
		free(q);
		return NULL;
	}
	q->blocks->next = NULL;
	q->blocks->size = RS__Q_FIRST_BLOCK_SIZE;
	q->blocks->block = calloc(RS__Q_FIRST_BLOCK_SIZE, q->data_size);
	if (!q->blocks->block) {
		free(q->blocks);
		free(q);
		return NULL;
	}
	
	// Initialise the block as a closed loop
	rs__q_block_init(q, q->blocks);
	BLOCK_ENTRY(q, q->blocks, q->blocks->size - 1)->next =
		BLOCK_ENTRY(q, q->blocks, 0);
	
	// Set the head and tail to point to the same (empty) entries
	q->head = (rs__q_entry_t *)q->blocks->block;
	q->tail = (rs__q_entry_t *)q->blocks->block;
	
	return q;
}


void *
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
		new_block->block = calloc(new_block->size, q->data_size);
		if (!new_block->block) {
			free(new_block);
			return;
		}
		
		// Initialise the new block and insert it into the queue's linked list
		rs__q_block_init(q, new_block);
		BLOCK_ENTRY(q, new_block, new_block->size - 1)->next = q->head->next;
		q->head->next = (rs__q_entry_t *)new_block->block;
		
		// Insert the new block into to the linked list of blocks
		new_block->next = q->blocks;
		q->blocks = new_block;
	}
	
	// Advance the head of the queue and return the entry which was previously at
	// the head.
	rs__q_entry_t *entry = q->head;
	entry->empty = false;
	q->head = q->head->next;
	return (void *)entry;
}


void *
rs__q_remove(rs__q_t *q)
{
	rs__q_entry_t *entry = q->tail;
	
	// Advance the tail of the queue and return the entry if it is not empty.
	if (!entry->empty) {
		entry->empty = true;
		q->tail = q->tail->next;
		return (void *)entry;
	} else {
		return NULL;
	}
}


void *
rs__q_peek(rs__q_t *q)
{
	if (!q->tail->empty) {
		return (void *)q->tail;
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
