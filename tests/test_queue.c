/**
 * Test suite for Rig SCP's request/response queue.
 */

#include <check.h>

#include "tests.h"

#include "rs_queue.h"

static rs__q_t *q = NULL;

static void setup(void) {
	q = rs__q_init();
	ck_assert(q);
}


static void teardown(void) {
	rs__q_free(q);
	q = NULL;
}


START_TEST (test_empty)
{
	// Make sure the queue doesn't peek or remove anything when empty
	ck_assert(rs__q_peek(q) == NULL);
	ck_assert(rs__q_remove(q) == NULL);
	ck_assert(rs__q_peek(q) == NULL);
	ck_assert(rs__q_remove(q) == NULL);
}
END_TEST


START_TEST (test_single_insertion)
{
	// Make sure things can be inserted and removed repeatedly
	const int num = 100;
	int i;
	for (i = 0; i < num; i++) {
		rs__q_entry_t *e = rs__q_insert(q);
		ck_assert(e);
		
		ck_assert(rs__q_peek(q) == e);
		ck_assert(rs__q_remove(q) == e);
		
		ck_assert(rs__q_peek(q) == NULL);
		ck_assert(rs__q_remove(q) == NULL);
	}
	
	// Make sure that only one block was allocated!
	ck_assert(q->blocks);
	ck_assert(q->blocks->next == NULL);
}
END_TEST


START_TEST (test_buffer_growth)
{
	// Make sure things can be inserted and removed in sequence
	
	int i;
	
	// Insert a number of items which shouldn't grow the buffer
	for (i = 0; i < RS__Q_FIRST_BLOCK_SIZE - 1; i++) {
		rs__q_entry_t *e = rs__q_insert(q);
		ck_assert(e);
		e->type = RS__Q_PACKET;
		e->data.packet.cmd_rc = i;
	}
	
	// Make sure that only one block was allocated!
	ck_assert(q->blocks);
	ck_assert(q->blocks->next == NULL);
	
	// Insert another item which should grow the buffer
	rs__q_entry_t *e = rs__q_insert(q);
	ck_assert(e);
	e->type = RS__Q_PACKET;
	e->data.packet.cmd_rc = i++;
	
	// A new block should now have been allocated
	ck_assert(q->blocks);
	ck_assert(q->blocks->next);
	
	// Removing things should come out in order
	for (i = 0; i < RS__Q_FIRST_BLOCK_SIZE; i++) {
		rs__q_entry_t *e = rs__q_peek(q);
		ck_assert(e);
		ck_assert(e->type == RS__Q_PACKET);
		ck_assert(e->data.packet.cmd_rc == i);
		ck_assert(rs__q_remove(q) == e);
	}
	
	// Nothing should be left
	ck_assert(rs__q_peek(q) == NULL);
	ck_assert(rs__q_remove(q) == NULL);
}
END_TEST


START_TEST (test_varying_size)
{
	// Go through several cycles of insertions and removals of varying sizes.
	
	const int num = 10;
	int i;
	int j;
	
	// The ID of the next thing to insert/the next thing due to remove
	int insert_id = 0;
	int remove_id = 0;
	
	// Insert a number of items which shouldn't grow the buffer
	for (i = 0; i < num; i++) {
		// Insert num-i packets
		for (j = 0; j < num - i - 1; j++) {
			rs__q_entry_t *e = rs__q_insert(q);
			ck_assert(e);
			e->type = RS__Q_PACKET;
			e->data.packet.cmd_rc = insert_id++;
		}
		
		// Remove i packets
		for (j = 0; j < i; j++) {
			rs__q_entry_t *e = rs__q_peek(q);
			ck_assert(e);
			ck_assert(e->type == RS__Q_PACKET);
			ck_assert(e->data.packet.cmd_rc == remove_id++);
			ck_assert(rs__q_remove(q) == e);
		}
	}
	
	// Nothing should be left
	ck_assert(rs__q_peek(q) == NULL);
	ck_assert(rs__q_remove(q) == NULL);
}
END_TEST


Suite *
make_queue_suite(void)
{
	Suite *s = suite_create("queue");
	
	// Add tests to the test case
	TCase *tc_core = tcase_create("Core");
	tcase_add_checked_fixture(tc_core, setup, teardown);
	tcase_add_test(tc_core, test_empty);
	tcase_add_test(tc_core, test_single_insertion);
	tcase_add_test(tc_core, test_buffer_growth);
	tcase_add_test(tc_core, test_varying_size);
	
	// Add each test case to the suite
	suite_add_tcase(s, tc_core);
	
	return s;
}
