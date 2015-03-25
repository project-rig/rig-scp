/**
 * An growable FIFO queue.
 *
 * This queue is designed to hold an ordered queue of user-defined structs. To
 * achieve this, users must define their structs to contain an rs__q_entry_t as
 * their first element. This value will be used internally to manage the queue
 * and should not be used directly by users.
 */

#ifndef RS_QUEUE_H
#define RS_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <uv.h>

#include <rs_scp.h>

/**
 * The size to use for the initial allocation of queue entries.
 */
#define RS__Q_FIRST_BLOCK_SIZE 8


/**
 * An entry in a queue.
 */
struct rs__q_entry;
typedef struct rs__q_entry rs__q_entry_t;
struct rs__q_entry {
	// Is this entry empty?
	bool empty;
	
	// Pointers forming a linked-list between entries in the queue.
	rs__q_entry_t *next;
};

/**
 * A linked list of blocks of memory allocated to storing rs__q_entry_t. These
 * blocks are immediately followed by a user-defined block of data.
 */
struct rs__q_block;
typedef struct rs__q_block rs__q_block_t;
struct rs__q_block {
	// Pointer to the allocated block
	void *block;
	
	// Size of the block (in entries)
	size_t size;
	
	// Pointer to the next allocated block or NULL if no more blocks are
	// allocated.
	rs__q_block_t *next;
};

/**
 * Data type which represents the queue.
 */
typedef struct rs__q {
	// Size of each entry in the queue (including the size of the rs__q_entry_t).
	size_t data_size;
	
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
} rs__q_t;


/**
 * Allocate a new queue in memory.
 *
 * @param data_size Size of the data blocks to be contained in the queue. Users
 *                  must include a rs__q_entry_t as the first entry in each data
 *                  block.
 * @returns a pointer to a newly allocated queue structure or NULL on failure.
 * This structure must be freed using rs__q_free.
 */
rs__q_t *rs__q_init(size_t data_size);


/**
 * Attempt to insert an entry into the queue.
 *
 * Returns a pointer to an entry in the queue or NULL on failure.
 */
void *rs__q_insert(rs__q_t *q);


/**
 * Attempt to remove an entry into the queue.
 *
 * Returns a pointer to an entry in the queue or NULL if the queue is empty.
 */
void *rs__q_remove(rs__q_t *q);


/**
 * Get a pointer to the next entry to be removed from the queue (without
 * removing it) or NULL if empty.
 */
void *rs__q_peek(rs__q_t *q);


/**
 * Free all memory associated with a queue.
 */
void rs__q_free(rs__q_t *q);

#endif
