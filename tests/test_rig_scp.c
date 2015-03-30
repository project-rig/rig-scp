/**
 * Test the main control logic of Rig SCP.
 *
 * The test fixture sets up an SCP connection (conn) to a mock machine (mm).
 * Additionally callback functions send_scp_cb and rw_cb are provided which
 * take as user data send_scp_cb_data_t and rw_cb_data_t structs respectively.
 * These callbacks log arguments to the last callback made and also the number
 * of calls. To run the libuv event loop until a set of callback functions have
 * been call, use wait_for_cb to register the *cb_data_t structs and then
 * wait_for_all_cb to run the event loop until all callbacks have been called.
 */

#include <stdio.h>

#include <sys/socket.h>

#include <check.h>

#include "tests.h"
#include "mock_machine.h"

#include "rs.h"


/******************************************************************************
 * Test parameters
 ******************************************************************************/

// Fudge factor for timing tests (ms)
#define FUDGE 50

// Timeout (ms) for SCP connection in tests
#define TIMEOUT 100

// Number of transmission attempts in tests
#define N_TRIES 3

// Number of simultaneous outstanding requests in tests
#define N_OUTSTANDING 2


/******************************************************************************
 * Test state variables
 ******************************************************************************/

// Libuv event loop used in tests
static uv_loop_t *loop = NULL;

// Mock machine to use in all tests.
static mm_t *mm = NULL;

// Address the mock machine has opened up
static struct sockaddr_storage conn_addr;

// Rig SCP connection to the mock machine.
static rs_conn_t *conn = NULL;

// A handle for the uv event loop 'prepare' callback. This callback runs once
// per loop iteration and is used to terminate the loop once a certain set of
// test callbacks has been called. The data field of this handle will point to a
// linked list of pointers to test callbacks.
static uv_prepare_t prepare_handle;


/******************************************************************************
 * SCP Packet deconstruction macros
 ******************************************************************************/

/**
 * Format of an SDP and SCP packet header.
 */
typedef struct {
	// SDP headder
	uint8_t flags;
	uint8_t tag;
	uint8_t dest_port_cpu;
	uint8_t srce_port_cpu;
	uint16_t dest_addr;
	uint16_t srce_addr;
	
	// SCP headder
	uint16_t cmd_rc;
	uint16_t seq_num;
	uint32_t arg1;
	uint32_t arg2;
	uint32_t arg3;
} sdp_scp_header_t;

/**
 * Unpack the address of a read or write packet.
 */
#define UNPACK_RW_ADDR(p) (((sdp_scp_header_t *)(p))->arg1)

/**
 * Unpack the length of a read or write packet.
 */
#define UNPACK_RW_LENGTH(p) (((sdp_scp_header_t *)(p))->arg2)

/**
 * Unpack the "type" of a read or write packet.
 */
#define UNPACK_RW_TYPE(p) (((sdp_scp_header_t *)(p))->arg3)


/******************************************************************************
 * Prepare handle
 ******************************************************************************/

/**
 * The base type of callback data values used by the test callbacks. This base
 * type records the number of calls and forms a linked list which is initialised
 * as a list of things that the prepare handle will wait for before stopping the
 * event loop.
 */
struct cb_data;
typedef struct cb_data cb_data_t;
struct cb_data {
	// Pointer to next cb_data_t or NULL if end of the list
	cb_data_t *next;
	
	// Number of times the callback has been called
	unsigned int n_calls;
};


/**
 * Callback for event loop "prepare". If all registered events to watch have
 * been called, terminate the loop.
 */
void
prepare_cb(uv_prepare_t *handle)
{
	cb_data_t *cb_data = (cb_data_t *)handle->data;
	
	// Don't stop the loop if any callbacks have not been called
	while (cb_data) {
		if (!cb_data->n_calls)
			return;
		cb_data = cb_data->next;
	}
	
	// Stop the loop since all callbacks have been called!
	uv_stop(handle->loop);
}


/**
 * Set up everything required by the prepare callback.
 */
int
prepare_init(uv_loop_t *loop)
{
	// Initialise the linked list of pending callbacks.
	prepare_handle.data = NULL;
	
	// Initialise the handle
	if (uv_prepare_init(loop, &prepare_handle))
		return 1;
	
	return 0;
}


void
prepare_close_cb(uv_handle_t *handle)
{
	// Do nothing...
}


/**
 * Clean up everything required by the prepare callback.
 */
void
prepare_free(void)
{
	uv_close((uv_handle_t *)&prepare_handle, prepare_close_cb);
}


/**
 * Run the libuv event loop until all callbacks registered using wait_for_cb
 * have been called at least once. Returns zero on success.
 */
int
wait_for_all_cb(void)
{
	// Register the prepare callback
	if (uv_prepare_start(&prepare_handle, prepare_cb))
		return -1;
	
	// Run until stopped
	if (uv_run(loop, UV_RUN_DEFAULT) < 0)
		return -1;
	
	// Unregister the callback
	if (uv_prepare_stop(&prepare_handle))
		return -1;
	
	// Check that all callbacks have actually ocurred (return an error if any
	// haven't)
	cb_data_t *cb_data = (cb_data_t *)prepare_handle.data;
	while (cb_data) {
		if (!cb_data->n_calls)
			return -1;
		cb_data = cb_data->next;
	}
	
	// Clear the linked list of registered callbacks
	prepare_handle.data = NULL;
	
	return 0;
}


/**
 * Require the supplied callback to get called before wait_for_all_cb returns.
 *
 * Resets the call count of the supplied callback.
 *
 * Do not call multiple times on the same callback data!
 */
void
wait_for_cb(cb_data_t *cb_data)
{
	cb_data->n_calls = 0;
	cb_data->next = (cb_data_t *)prepare_handle.data;
	prepare_handle.data = (void *)cb_data;
}

/******************************************************************************
 * Test callbacks
 ******************************************************************************/

/**
 * A data structure to hold details of callbacks from the rig scp library's
 * rs_send_scp function. To be passed as data to the ready-made callback
 * send_scp_cb.
 */
struct send_scp_cb_data;
typedef struct send_scp_cb_data send_scp_cb_data_t;
struct send_scp_cb_data {
	cb_data_t generic_info;
	
	// Store a copy of the arguments supplied
	rs_conn_t *conn;
	int error;
	uint16_t cmd_rc;
	unsigned int n_args;
	uint32_t arg1;
	uint32_t arg2;
	uint32_t arg3;
	uv_buf_t data;
};


void
send_scp_cb(rs_conn_t *conn,
            int error,
            uint16_t cmd_rc,
            unsigned int n_args,
            uint32_t arg1,
            uint32_t arg2,
            uint32_t arg3,
            uv_buf_t data,
            void *cb_data)
{
	send_scp_cb_data_t *d = (send_scp_cb_data_t *)cb_data;
	d->conn = conn;
	d->error = error;
	d->cmd_rc = cmd_rc;
	d->n_args = n_args;
	d->arg1 = arg1;
	d->arg2 = arg2;
	d->arg3 = arg3;
	d->data = data;
	
	d->generic_info.n_calls++;
}


/**
 * A data structure to hold details of callbacks from the rig scp library's
 * rs_read/rs_write functions. To be passed as data to the ready-made callback
 * rw_cb.
 */
struct rw_cb_data;
typedef struct rw_cb_data rw_cb_data_t;
struct rw_cb_data {
	cb_data_t generic_info;
	
	// Store a copy of the arguments supplied
	rs_conn_t *conn;
	int error;
	uint16_t cmd_rc;
	uv_buf_t data;
};


void
rw_cb(rs_conn_t *conn,
      int error,
      uint16_t cmd_rc,
      uv_buf_t data,
      void *cb_data)
{
	rw_cb_data_t *d = (rw_cb_data_t *)cb_data;
	d->conn = conn;
	d->error = error;
	d->cmd_rc = cmd_rc;
	d->data = data;
	
	d->generic_info.n_calls++;
}


/******************************************************************************
 * Test fixture setup/teardown
 ******************************************************************************/

static void setup(void) {
	// Get the libuv event loop
	loop = uv_default_loop();
	ck_assert(loop);
	
	// Set up the prepare handle
	ck_assert(!prepare_init(loop));
	
	// Set up the mock machine and get its address
	mm = mm_init(loop);
	ck_assert(mm);
	int namelen = sizeof(struct sockaddr_storage);
	mm_getsockname(mm, (struct sockaddr *)&conn_addr, &namelen);
	
	// Connect the client to it
	conn = rs_init(loop,
	               (struct sockaddr *)&conn_addr,
	               MM_SCP_DATA_LENGTH,
	               TIMEOUT,
	               N_TRIES,
	               N_OUTSTANDING);
	ck_assert(conn);
}


static void teardown(void) {
	// No need for a callback since this will be the last thing in the event loop
	// and thus the loop will end itself.
	rs_free(conn, NULL, NULL);
	conn = NULL;
	
	mm_free(mm);
	mm = NULL;
	
	prepare_free();
	
	// Wait for everything to clear;
	uv_run(loop, UV_RUN_DEFAULT);
	ck_assert(!uv_loop_alive(loop));
	
	loop = NULL;
}


/******************************************************************************
 * Tests
 ******************************************************************************/


/**
 * Make sure that memory is cleaned up and freeing succeeds when nothing
 * happens.
 */
START_TEST (test_empty)
{
	// Do nothing (everything is setup/torndown in test fixture).
}
END_TEST


/**
 * Make sure that a single SCP command can be sent and received with varying
 * numbers of arguments.
 */
START_TEST (test_single_scp)
{
	unsigned int n_args = _i;
	
	// Create a callback which we'll wait on for a reply
	send_scp_cb_data_t cb_data;
	wait_for_cb((cb_data_t *)&cb_data);
	
	// Create a simple payload to include with the packet
	const char data_in[] = "Hello, world!";
	char data_buf[] = "Hello, world!";
	uv_buf_t data;
	data.base = (void *)data_buf;
	data.len = 13;
	
	// Send the packet
	ck_assert(!rs_send_scp(conn,
	                       (1 << 8) | 1, // Respond after 1 msec and one attempt
	                       0, // Send no duplicates
	                       0, // An arbitrary cmd_rc
	                       n_args, n_args,
	                       0x11121314, 0x21222324, 0x31323334,
	                       data,
	                       data.len,
	                       send_scp_cb, &cb_data));
	
	// Wait for a reply
	ck_assert(!wait_for_all_cb());
	
	// Check that the response came back once
	ck_assert_uint_eq(cb_data.generic_info.n_calls, 1);
	
	// Check the arguments match what was sent
	ck_assert(cb_data.conn == conn);
	ck_assert(!cb_data.error);
	ck_assert_uint_eq(cb_data.cmd_rc, 0);
	ck_assert_uint_eq(cb_data.n_args, n_args);
	if (n_args >= 1) ck_assert_uint_eq(cb_data.arg1, 0x11121314);
	if (n_args >= 2) ck_assert_uint_eq(cb_data.arg2, 0x21222324);
	if (n_args >= 3) ck_assert_uint_eq(cb_data.arg3, 0x31323334);
	ck_assert(cb_data.data.base == data.base);
	ck_assert(cb_data.data.len == data.len);
	ck_assert(memcmp(cb_data.data.base, data_in, data.len) == 0);
	
	// Check that only one request was sent to the mock machine and that it was as
	// expected.
	mm_req_t *req = mm_get_req(mm, 0);
	ck_assert(req);
	ck_assert(mm->reqs == req);
	ck_assert(req->next == NULL);
	ck_assert_uint_eq(req->n_changes, 1);
	ck_assert_uint_eq(req->n_tries, 1);
	ck_assert_uint_eq(req->buf.len, RS__SIZEOF_SCP_PACKET(n_args, data.len));
}
END_TEST


/**
 * Make sure packet timeout works on a single packet.
 */
START_TEST (test_single_scp_timeout)
{
	// Create a callback which we'll wait on for a reply
	send_scp_cb_data_t cb_data;
	wait_for_cb((cb_data_t *)&cb_data);
	
	// Create an empty payload
	uv_buf_t data;
	data.base = NULL;
	data.len = 0;
	
	// Send the packet
	ck_assert(!rs_send_scp(conn,
	                       (0 << 8) | 0, // Never respond
	                       0, // Send no duplicates
	                       0, // An arbitrary cmd_rc
	                       0, 0, 0, 0, 0, // No arguments
	                       data,
	                       data.len,
	                       send_scp_cb, &cb_data));
	
	// Wait for a reply
	uv_update_time(loop);
	uint64_t time_before = uv_now(loop);
	ck_assert(!wait_for_all_cb());
	uint64_t time_after = uv_now(loop);
	
	// Check that long enough for the timeout ocurred
	ck_assert_int_ge(time_after - time_before, N_TRIES * TIMEOUT);
	
	// Check that the response came back once
	ck_assert_uint_eq(cb_data.generic_info.n_calls, 1);
	
	// Check that the packet failed
	ck_assert(cb_data.conn == conn);
	ck_assert(cb_data.error == RS_ETIMEOUT);
	ck_assert(cb_data.data.base == data.base);
	ck_assert(cb_data.data.len == data.len);
	
	// Check that multiple requests with the same sequence number were sent to the
	// mock machine and that it was as expected.
	mm_req_t *req = mm_get_req(mm, 0);
	ck_assert(req);
	ck_assert(mm->reqs == req);
	ck_assert(req->next == NULL);
	ck_assert_uint_eq(req->n_changes, 1);
	ck_assert_uint_eq(req->n_tries, N_TRIES);
	ck_assert_uint_eq(req->buf.len, RS__SIZEOF_SCP_PACKET(0, 0));
}
END_TEST


/**
 * Make sure packet retransmission works.
 */
START_TEST (test_single_scp_retransmit)
{
	// Create a callback which we'll wait on for a reply
	send_scp_cb_data_t cb_data;
	wait_for_cb((cb_data_t *)&cb_data);
	
	// Create an empty payload
	uv_buf_t data;
	data.base = NULL;
	data.len = 0;
	
	// Send the packet
	ck_assert(!rs_send_scp(conn,
	                       (1 << 8) | N_TRIES, // Respond after 1 msec and after
	                                           // the maximum number of tries
	                                           // before failure
	                       0, // Send no duplicates
	                       0, // An arbitrary cmd_rc
	                       0, 0, 0, 0, 0, // No arguments
	                       data,
	                       data.len,
	                       send_scp_cb, &cb_data));
	
	// Wait for a reply
	uv_update_time(loop);
	uint64_t time_before = uv_now(loop);
	ck_assert(!wait_for_all_cb());
	uint64_t time_after = uv_now(loop);
	
	// Check that long enough for the attempts ocurred
	ck_assert_int_ge(time_after - time_before, (N_TRIES - 1) * TIMEOUT);
	
	// Check that the response came back once
	ck_assert_uint_eq(cb_data.generic_info.n_calls, 1);
	
	// Check that the packet eventually came back intact
	ck_assert(cb_data.conn == conn);
	ck_assert(!cb_data.error);
	ck_assert_uint_eq(cb_data.cmd_rc, 0);
	ck_assert_uint_eq(cb_data.n_args, 0);
	ck_assert(cb_data.data.base == data.base);
	ck_assert(cb_data.data.len == data.len);
	
	// Check that multiple requests with the same sequence number were sent to the
	// mock machine and that it was as expected.
	mm_req_t *req = mm_get_req(mm, 0);
	ck_assert(req);
	ck_assert(mm->reqs == req);
	ck_assert(req->next == NULL);
	ck_assert_uint_eq(req->n_changes, 1);
	ck_assert_uint_eq(req->n_tries, N_TRIES);
	ck_assert_uint_eq(req->buf.len, RS__SIZEOF_SCP_PACKET(0, 0));
}
END_TEST


/**
 * Make sure that a single-packet read command can be sent and received.
 */
START_TEST (test_single_packet_read)
{
	const size_t offset = _i;
	size_t i;
	
	// Set up some fake data to read back
	mm_rw_t *rw = mm_get_rw(mm, 0);
	for (i = 0; i < MM_SCP_DATA_LENGTH; i++) {
		rw->data[offset + i] = (unsigned char)i;
	}
	
	// Create a callback which we'll wait on for a reply
	rw_cb_data_t cb_data;
	wait_for_cb((cb_data_t *)&cb_data);
	
	// Set a buffer to hold the read data
	unsigned char data_buf[MM_SCP_DATA_LENGTH];
	uv_buf_t data;
	data.base = (void *)data_buf;
	data.len = MM_SCP_DATA_LENGTH;
	
	uint32_t addr = (offset |  // Start at the given offset
	                 0u<<10 |  // The RW ID
	                 255u<<16 | // No errors
	                 255u<<24); // Respond to all the same speed
	
	// Send the packet
	ck_assert(!rs_read(conn,
	                   (1 << 8) | 1, // Respond after 1 msec and one attempt
	                   0, // Send no duplicates
	                   addr,
	                   data,
	                   rw_cb, &cb_data));
	
	// Wait for a reply
	ck_assert(!wait_for_all_cb());
	
	// Check that the response came back once
	ck_assert_uint_eq(cb_data.generic_info.n_calls, 1);
	
	// Check one request was made in total
	ck_assert_uint_eq(rw->n_responses_sent, 1);
	
	// Check that only the expected data was read, and it was read once.
	for (i = 0; i < MM_MAX_RW; i++) {
		if (i >= offset && i < offset + MM_SCP_DATA_LENGTH)
			ck_assert_uint_eq(rw->read_count[i], 1);
		else
			ck_assert_uint_eq(rw->read_count[i], 0);
		
		ck_assert_uint_eq(rw->write_count[i], 0);
	}
	
	// Check the data read is as expected
	ck_assert(cb_data.conn == conn);
	ck_assert(!cb_data.error);
	ck_assert(cb_data.data.base == data.base);
	ck_assert(cb_data.data.len == data.len);
	ck_assert(memcmp(cb_data.data.base, rw->data + offset, data.len) == 0);
	
	// Check that only one request was sent to the mock machine and that it was as
	// expected.
	mm_req_t *req = mm_get_req(mm, 0);
	ck_assert(req);
	ck_assert(mm->reqs == req);
	ck_assert(req->next == NULL);
	ck_assert_uint_eq(req->n_changes, 1);
	ck_assert_uint_eq(req->n_tries, 1);
	ck_assert_uint_eq(UNPACK_RW_ADDR(req->buf.base), addr);
	ck_assert_uint_eq(UNPACK_RW_LENGTH(req->buf.base), MM_SCP_DATA_LENGTH);
	ck_assert_uint_eq(UNPACK_RW_TYPE(req->buf.base),
	                  rs__scp_rw_type(offset, MM_SCP_DATA_LENGTH));
}
END_TEST


/**
 * Make sure that a single-packet write command can be sent and received.
 */
START_TEST (test_single_packet_write)
{
	const size_t offset = _i;
	size_t i;
	
	// Get a reference to the memory block we're going to write to
	mm_rw_t *rw = mm_get_rw(mm, 0);
	
	// Create a callback which we'll wait on for a reply
	rw_cb_data_t cb_data;
	wait_for_cb((cb_data_t *)&cb_data);
	
	// Set a buffer with some dummy data to write
	unsigned char data_buf[MM_SCP_DATA_LENGTH];
	for (i = 0; i < MM_SCP_DATA_LENGTH; i++)
		data_buf[i] = (unsigned char)i;
	uv_buf_t data;
	data.base = (void *)data_buf;
	data.len = MM_SCP_DATA_LENGTH;
	
	// Send the packet
	uint32_t addr = (offset |  // Start at the given offset
	                 0u<<10 |  // The RW ID
	                 255u<<16 | // No errors
	                 255u<<24); // Respond to all the same speed
	ck_assert(!rs_write(conn,
	                    (1 << 8) | 1, // Respond after 1 msec and one attempt
	                    0, // Send no duplicates
	                    addr,
	                    data,
	                    rw_cb, &cb_data));
	
	// Wait for a reply
	ck_assert(!wait_for_all_cb());
	
	// Check that the response came back once
	ck_assert_uint_eq(cb_data.generic_info.n_calls, 1);
	
	// Check one request was made in total
	ck_assert_uint_eq(rw->n_responses_sent, 1);
	
	// Check that only the expected data was written, and it was written once.
	for (i = 0; i < MM_MAX_RW; i++) {
		if (i >= offset && i < offset + MM_SCP_DATA_LENGTH)
			ck_assert_uint_eq(rw->write_count[i], 1);
		else
			ck_assert_uint_eq(rw->write_count[i], 0);
		
		ck_assert_uint_eq(rw->read_count[i], 0);
	}
	
	// Check the data written was correct
	ck_assert(memcmp(rw->data + offset, data_buf, data.len) == 0);
	
	// Check the response is as expected
	ck_assert(cb_data.conn == conn);
	ck_assert(!cb_data.error);
	ck_assert(cb_data.data.base == data.base);
	ck_assert(cb_data.data.len == data.len);
	
	// Check that only one request was sent to the mock machine and that it was as
	// expected.
	mm_req_t *req = mm_get_req(mm, 0);
	ck_assert(req);
	ck_assert(mm->reqs == req);
	ck_assert(req->next == NULL);
	ck_assert_uint_eq(req->n_changes, 1);
	ck_assert_uint_eq(req->n_tries, 1);
	ck_assert_uint_eq(UNPACK_RW_ADDR(req->buf.base), addr);
	ck_assert_uint_eq(UNPACK_RW_LENGTH(req->buf.base), MM_SCP_DATA_LENGTH);
	ck_assert_uint_eq(UNPACK_RW_TYPE(req->buf.base),
	                  rs__scp_rw_type(offset, MM_SCP_DATA_LENGTH));
}
END_TEST


/**
 * Make sure that if several packets are sent at once they are carried out in
 * parallel. Also checks that duplicate response packets are ignored.
 */
START_TEST (test_multiple_scp)
{
	// Number parallel rounds worth of packets to send
	const unsigned int n_rounds = 3;
	
	// Number of packets to send at once
	const unsigned int n_packets = N_OUTSTANDING * n_rounds;
	
	unsigned int i;
	
	// Create an empty payload
	uv_buf_t data;
	data.base = NULL;
	data.len = 0;
	
	// Create a set of callbacks which we'll wait on for replies
	send_scp_cb_data_t cb_data[n_packets];
	for (i = 0; i < n_packets; i++)
		wait_for_cb((cb_data_t *)&(cb_data[i]));
	
	// Send lots of packets at once
	for (i = 0; i < n_packets; i++)
		ck_assert(!rs_send_scp(conn,
		                       (TIMEOUT/2 << 8) | 1, // Respond after half the
		                                             // timeout.
		                       3, // Send some duplicates
		                       0, // An arbitrary cmd_rc
		                       1, 1, i, 0, 0, // The packet num as an argument
		                       data,
		                       data.len,
		                       send_scp_cb, &(cb_data[i])));
	
	// Wait for a reply
	uv_update_time(loop);
	uint64_t time_before = uv_now(loop);
	ck_assert(!wait_for_all_cb());
	uint64_t time_after = uv_now(loop);
	
	// Check that the time required was such that it would only be possible of
	// multiple commands were sent in parallel.
	ck_assert_int_lt(time_after - time_before, (TIMEOUT/2) * n_rounds + FUDGE);
	
	for (i = 0; i < n_packets; i++) {
		// Check that the responses came back once
		ck_assert_uint_eq(cb_data[i].generic_info.n_calls, 1);
		
		// Check that the packet didn't fail and has the right argument
		ck_assert(cb_data[i].conn == conn);
		ck_assert(!cb_data[i].error);
		ck_assert_uint_eq(cb_data[i].cmd_rc, 0);
		ck_assert_uint_eq(cb_data[i].n_args, 1);
		ck_assert_uint_eq(cb_data[i].arg1, i);
		ck_assert(cb_data[i].data.base == data.base);
		ck_assert(cb_data[i].data.len == data.len);
		
		// Check that one request the relevent sequence number was sent to the mock
		// machine and that it was as expected.
		mm_req_t *req = mm_get_req(mm, i);
		ck_assert(req);
		ck_assert_uint_eq(req->n_changes, 1);
		ck_assert_uint_eq(req->n_tries, 1);
		ck_assert_uint_eq(req->buf.len, RS__SIZEOF_SCP_PACKET(1, 0));
	}
}
END_TEST


/**
 * Make sure that a multi-packet read command can be sent and received. Also
 * checks that duplicate response packets are ignored.
 */
START_TEST (test_multiple_packet_read)
{
	// Offset for the data in memory
	const size_t offset = 10;
	
	// Number parallel rounds worth of packets to send
	const unsigned int n_rounds = 3;
	
	// Number of packets to send
	const size_t n_packets = n_rounds * N_OUTSTANDING;
	
	// Length of the read request selected to require the specified number of
	// rounds, with the last round being half-length
	const size_t length = (MM_SCP_DATA_LENGTH * n_packets)
	                      - MM_SCP_DATA_LENGTH / 2;
	
	size_t i;
	
	// Set up some fake data to read back
	mm_rw_t *rw = mm_get_rw(mm, 0);
	for (i = 0; i < length; i++) {
		rw->data[offset + i] = (unsigned char)i;
	}
	
	// Create a callback which we'll wait on for a reply
	rw_cb_data_t cb_data;
	wait_for_cb((cb_data_t *)&cb_data);
	
	// Set a buffer to hold the read data
	unsigned char data_buf[length];
	uv_buf_t data;
	data.base = (void *)data_buf;
	data.len = length;
	
	uint32_t addr = (offset |  // Start at the given offset
	                 0u<<10 |  // The RW ID
	                 255u<<16 | // No errors
	                 255u<<24); // Respond to all the same speed
	
	// Send the packet
	ck_assert(!rs_read(conn,
	                   (TIMEOUT/2 << 8) | 1, // Respond after half the timeout
	                   3, // Send some duplicates
	                   addr,
	                   data,
	                   rw_cb, &cb_data));
	
	// Wait for a reply
	uv_update_time(loop);
	uint64_t time_before = uv_now(loop);
	ck_assert(!wait_for_all_cb());
	uint64_t time_after = uv_now(loop);
	
	// Check that the time required was such that it would only be possible of
	// multiple commands were sent in parallel.
	ck_assert_int_lt(time_after - time_before, (TIMEOUT/2) * n_rounds + FUDGE);
	
	// Check that the response came back once
	ck_assert_uint_eq(cb_data.generic_info.n_calls, 1);
	
	// Check the right number of requests were sent
	ck_assert_uint_eq(rw->n_responses_sent, n_packets);
	
	// Check that only the expected data was read, and it was read once.
	for (i = 0; i < MM_MAX_RW; i++) {
		if (i >= offset && i < offset + length)
			ck_assert_uint_eq(rw->read_count[i], 1);
		else
			ck_assert_uint_eq(rw->read_count[i], 0);
		
		ck_assert_uint_eq(rw->write_count[i], 0);
	}
	
	// Check the data read is as expected
	ck_assert(cb_data.conn == conn);
	ck_assert(!cb_data.error);
	ck_assert(cb_data.data.base == data.base);
	ck_assert(cb_data.data.len == data.len);
	ck_assert(memcmp(cb_data.data.base, rw->data + offset, data.len) == 0);
}
END_TEST


/**
 * Make sure that a multi-packet write command can be sent and received. Also
 * checks that duplicate response packets are ignored.
 */
START_TEST (test_multiple_packet_write)
{
	// Offset for the data in memory
	const size_t offset = 10;
	
	// Number parallel rounds worth of packets to send
	const unsigned int n_rounds = 3;
	
	// Number of packets to send
	const size_t n_packets = n_rounds * N_OUTSTANDING;
	
	// Length of the read request selected to require the specified number of
	// rounds, with the last round being half-length
	const size_t length = (MM_SCP_DATA_LENGTH * n_packets)
	                      - MM_SCP_DATA_LENGTH / 2;
	
	size_t i;
	
	// Get a reference to the memory block we're going to write to
	mm_rw_t *rw = mm_get_rw(mm, 0);
	
	// Create a callback which we'll wait on for a reply
	rw_cb_data_t cb_data;
	wait_for_cb((cb_data_t *)&cb_data);
	
	// Set a buffer with some dummy data to write
	unsigned char data_buf[length];
	for (i = 0; i < length; i++)
		data_buf[i] = (unsigned char)i;
	uv_buf_t data;
	data.base = (void *)data_buf;
	data.len = length;
	
	// Send the packet
	uint32_t addr = (offset |  // Start at the given offset
	                 0u<<10 |  // The RW ID
	                 255u<<16 | // No errors
	                 255u<<24); // Respond to all the same speed
	ck_assert(!rs_write(conn,
	                    (TIMEOUT/2 << 8) | 1, // Respond after half the timeout
	                    3, // Send some duplicates
	                    addr,
	                    data,
	                    rw_cb, &cb_data));
	
	// Wait for a reply
	uv_update_time(loop);
	uint64_t time_before = uv_now(loop);
	ck_assert(!wait_for_all_cb());
	uint64_t time_after = uv_now(loop);
	
	// Check that the time required was such that it would only be possible of
	// multiple commands were sent in parallel.
	ck_assert_int_lt(time_after - time_before, (TIMEOUT/2) * n_rounds + FUDGE);
	
	// Check that the response came back once
	ck_assert_uint_eq(cb_data.generic_info.n_calls, 1);
	
	// Check the right number of packets were used
	ck_assert_uint_eq(rw->n_responses_sent, n_packets);
	
	// Check that only the expected data was written, and it was written once.
	for (i = 0; i < MM_MAX_RW; i++) {
		if (i >= offset && i < offset + length)
			ck_assert_uint_eq(rw->write_count[i], 1);
		else
			ck_assert_uint_eq(rw->write_count[i], 0);
		
		ck_assert_uint_eq(rw->read_count[i], 0);
	}
	
	// Check the data written was correct
	ck_assert(memcmp(rw->data + offset, data_buf, data.len) == 0);
	
	// Check the response is as expected
	ck_assert(cb_data.conn == conn);
	ck_assert(!cb_data.error);
	ck_assert(cb_data.data.base == data.base);
	ck_assert(cb_data.data.len == data.len);
}
END_TEST


/**
 * Make sure that when multiple outstanding channels are available, a single
 * blocked packet can't block the rest.
 */
START_TEST (test_non_obstructing)
{
	// Number of packets to send at once (the first of which will be blocked).
	// This number is chosen such that the blocked packet will take as long to
	// timeout as the rest take to go through the remaining unblocked channels.
	const unsigned int n_packets = (N_TRIES*2*(N_OUTSTANDING - 1)) + 1;
	
	unsigned int i;
	
	// Create an empty payload
	uv_buf_t data;
	data.base = NULL;
	data.len = 0;
	
	// Create a set of callbacks which we'll wait on for replies
	send_scp_cb_data_t cb_data[n_packets];
	for (i = 0; i < n_packets; i++)
		wait_for_cb((cb_data_t *)&(cb_data[i]));
	
	// Send lots of packets at once
	for (i = 0; i < n_packets; i++)
		ck_assert(!rs_send_scp(conn,
		                       (TIMEOUT/2 << 8) | (i!=0), // Respond after half the
		                                                  // timeout except for the
		                                                  // first packet which will
		                                                  // never respond.
		                       0, // Send no duplicates
		                       0, // An arbitrary cmd_rc
		                       1, 1, i, 0, 0, // The packet num as an argument
		                       data,
		                       data.len,
		                       send_scp_cb, &(cb_data[i])));
	
	// Wait for a reply
	uv_update_time(loop);
	uint64_t time_before = uv_now(loop);
	ck_assert(!wait_for_all_cb());
	uint64_t time_after = uv_now(loop);
	
	// Check that the time required was such that it would only be possible for
	// all packets to have got through if the blocked packet didn't block everyone
	// else.
	ck_assert_int_lt(time_after - time_before, TIMEOUT*N_TRIES + FUDGE);
	
	for (i = 0; i < n_packets; i++) {
		// Check that the responses came back once
		ck_assert_uint_eq(cb_data[i].generic_info.n_calls, 1);
		
		// Check that the packet didn't fail and has the right argument
		ck_assert(cb_data[i].conn == conn);
		if (i == 0) {
			ck_assert(cb_data[i].error == RS_ETIMEOUT);
		} else {
			ck_assert(!cb_data[i].error);
			ck_assert_uint_eq(cb_data[i].cmd_rc, 0);
			ck_assert_uint_eq(cb_data[i].n_args, 1);
			ck_assert_uint_eq(cb_data[i].arg1, i);
		}
		ck_assert(cb_data[i].data.base == data.base);
		ck_assert(cb_data[i].data.len == data.len);
	}
}
END_TEST


/**
 * Make sure that a read command fails if things time out
 */
START_TEST (test_read_timeout)
{
	// Offset for the data in memory
	const size_t offset = 10;
	
	// Number parallel rounds worth of packets to send
	const unsigned int n_rounds = 5;
	
	// Number of packets to send
	const size_t n_packets = n_rounds * N_OUTSTANDING;
	
	// Length of the read request selected to require the specified number of
	// rounds, with the last round being half-length
	const size_t length = (MM_SCP_DATA_LENGTH * n_packets)
	                      - MM_SCP_DATA_LENGTH / 2;
	
	size_t i;
	
	// Set up some fake data to read back
	mm_rw_t *rw = mm_get_rw(mm, 0);
	for (i = 0; i < length; i++) {
		rw->data[offset + i] = (unsigned char)i;
	}
	
	// Create a callback which we'll wait on for a reply
	rw_cb_data_t cb_data;
	wait_for_cb((cb_data_t *)&cb_data);
	
	// Set a buffer to hold the read data
	unsigned char data_buf[length];
	uv_buf_t data;
	data.base = (void *)data_buf;
	data.len = length;
	
	uint32_t addr = (offset |  // Start at the given offset
	                 0u<<10 |  // The RW ID
	                 255u<<16 | // No errors
	                 3u<<24);  // Start timing out on the 4th packet
	
	// Send the packet
	ck_assert(!rs_read(conn,
	                   (0 << 8) | 0, // Never reply (after 3rd packet)
	                   0, // Send no duplicates
	                   addr,
	                   data,
	                   rw_cb, &cb_data));
	
	// Wait for a reply
	uv_update_time(loop);
	uint64_t time_before = uv_now(loop);
	ck_assert(!wait_for_all_cb());
	uint64_t time_after = uv_now(loop);
	
	// Check that the time required was such that most of the read must have
	// been cancelled.
	ck_assert_int_lt(time_after - time_before, TIMEOUT * N_TRIES + FUDGE);
	
	// Check that the response came back once
	ck_assert_uint_eq(cb_data.generic_info.n_calls, 1);
	
	// Check the right number of responses were sent
	ck_assert_uint_eq(rw->n_responses_sent, 4);
	
	// Check the read failed
	ck_assert(cb_data.conn == conn);
	ck_assert(cb_data.error == RS_ETIMEOUT);
	ck_assert(cb_data.data.base == data.base);
	ck_assert(cb_data.data.len == data.len);
}
END_TEST


/**
 * Make sure that a read command fails if an error is returned on one of the
 * packets.
 */
START_TEST (test_read_fail)
{
	// Offset for the data in memory
	const size_t offset = 10;
	
	// Number parallel rounds worth of packets to send
	const unsigned int n_rounds = 5;
	
	// Number of packets to send
	const size_t n_packets = n_rounds * N_OUTSTANDING;
	
	// Length of the read request selected to require the specified number of
	// rounds, with the last round being half-length
	const size_t length = (MM_SCP_DATA_LENGTH * n_packets)
	                      - MM_SCP_DATA_LENGTH / 2;
	
	size_t i;
	
	// Set up some fake data to read back
	mm_rw_t *rw = mm_get_rw(mm, 0);
	for (i = 0; i < length; i++) {
		rw->data[offset + i] = (unsigned char)i;
	}
	
	// Create a callback which we'll wait on for a reply
	rw_cb_data_t cb_data;
	wait_for_cb((cb_data_t *)&cb_data);
	
	// Set a buffer to hold the read data
	unsigned char data_buf[length];
	uv_buf_t data;
	data.base = (void *)data_buf;
	data.len = length;
	
	uint32_t addr = (offset |  // Start at the given offset
	                 0u<<10 |  // The RW ID
	                 3u<<16 |  // Return an error on the 4th reply
	                 255u<<24); // Respond to all the same speed
	
	// Send the packet
	ck_assert(!rs_read(conn,
	                   (0 << 8) | 1, // Always reply instantly
	                   0, // Send no duplicates
	                   addr,
	                   data,
	                   rw_cb, &cb_data));
	
	// Wait for a reply
	uv_update_time(loop);
	uint64_t time_before = uv_now(loop);
	ck_assert(!wait_for_all_cb());
	uint64_t time_after = uv_now(loop);
	
	// Check that the time required was such that most of the read must have
	// been cancelled.
	ck_assert_int_lt(time_after - time_before, FUDGE);
	
	// Check that the response came back once
	ck_assert_uint_eq(cb_data.generic_info.n_calls, 1);
	
	// Check the right number of responses were sent
	ck_assert_uint_eq(rw->n_responses_sent, 4);
	
	// Check the read failed
	ck_assert(cb_data.conn == conn);
	ck_assert(cb_data.error == RS_EBAD_RC);
	ck_assert(cb_data.cmd_rc == 0);
	ck_assert(cb_data.data.base == data.base);
	ck_assert(cb_data.data.len == data.len);
}
END_TEST



Suite *
make_rig_scp_suite(void)
{
	Suite *s = suite_create("rig_scp");
	
	// Add tests to the test case
	TCase *tc_core = tcase_create("Core");
	tcase_add_checked_fixture(tc_core, setup, teardown);
	tcase_add_test(tc_core, test_empty);
	tcase_add_loop_test(tc_core, test_single_scp, 0, 4);
	tcase_add_test(tc_core, test_single_scp_timeout);
	tcase_add_test(tc_core, test_single_scp_retransmit);
	tcase_add_loop_test(tc_core, test_single_packet_read, 0, 4);
	tcase_add_loop_test(tc_core, test_single_packet_write, 0, 4);
	tcase_add_test(tc_core, test_multiple_scp);
	tcase_add_test(tc_core, test_multiple_packet_read);
	tcase_add_test(tc_core, test_multiple_packet_write);
	tcase_add_test(tc_core, test_non_obstructing);
	tcase_add_test(tc_core, test_read_timeout);
	tcase_add_test(tc_core, test_read_fail);
	
	
	// Add each test case to the suite
	suite_add_tcase(s, tc_core);
	
	return s;
}

