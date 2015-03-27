/**
 * A fake echo-server style implementation of a SpiNNaker machine for use in
 * tests.
 *
 * This echo server opens a local UDP socket and bounces back incoming requests
 * in the following manner:
 * * In general:
 *   * Bits 15:8 of dest_addr give the response latency in msec
 *   * Bits 7:0 of dest_addr give the number of attempts which must be made before
 *     a response is sent. If zero, never respond.
 *   * Bits 4:0 of dest_port_cpu give the number of duplicate responses to send
 * * For CMD_READ and CMD_WRITE:
 *   * Bits 15:10 of the address gives a unique identifier to the read/write and
 *     is used count incoming read/write requests related to the same command.
 *   * Bits 23:16 of the address gives the number of successful requests to
 *     read/write before returning a single error or 255 to return no errors.
 *   * Bits 31:24 of the address gives the number of successful requests to
 *     read/write instantly before delaying by dest_addr[7:0] attempts or 255
 *     to always return after dest_addr[7:0] attempts.
 *
 * This code is a joy of hideously inefficient data structures and linear
 * searches since its performance is truly irellevent.
 */

#ifndef MOCK_MACHINE_H
#define MOCK_MACHINE_H

#include <sys/socket.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <uv.h>

#include "rs__scp.h"


/**
 * The data field length supported by the mock machine.
 */
#define MM_SCP_DATA_LENGTH 32

/**
 * Maximum total read/write size supported by the block.
 */
#define MM_MAX_RW 1024


struct mm_resp;
typedef struct mm_resp mm_resp_t;

struct mm_req;
typedef struct mm_req mm_req_t;

struct mm_rw;
typedef struct mm_rw mm_rw_t;

struct mm;
typedef struct mm mm_t;


/**
 * Data structure which holds the state of a response being returned to the
 * sender. A simple linked list.
 */

struct mm_resp {
	// The request associated with this response.
	mm_req_t *req;
	
	// A timer handle to the timer used to delay sending of the response
	uv_timer_t timer_handle;
	
	// A flag indicating that the timer handle has been closed and that freeing
	// may occur.
	bool timer_handle_closed;
	
	// The address to send the response back to
	struct sockaddr addr;
	
	// The data to be sent
	uv_buf_t buf;
	
	// A counter incremented when a send has been initiated and decremented when the
	// callback has send occurred.
	unsigned int udp_send_active;
	
	// Link to next mm_resp_t or NULL if the end of the list.
	mm_resp_t *next;
};


/**
 * Data structure which holds the state of outstanding requests, a simple linked
 * list.
 */
struct mm_req {
	// The machine associated with the request
	mm_t *mm;
	
	// Sequence number of the request
	uint16_t seq_num;
	
	// Data buffer to store incoming packets
	char packet[RS__SIZEOF_SCP_PACKET(3, MM_SCP_DATA_LENGTH)];
	uv_buf_t buf;
	
	// Count how many times the packet value received was different from the
	// previous packet (in general this should be 1)
	unsigned int n_changes;
	
	// How many attempts have been made with this sequence number?
	unsigned int n_tries;
	
	// Linked list of response structures.
	mm_resp_t *resps;
	
	// Link to next mm_req_t or NULL if the end of the list.
	mm_req_t *next;
};


/**
 * Data structure which holds the state of a group of read/write requests, a
 * simple linked list with one entry per read/write block.
 */
struct mm_rw {
	// The identifier for this read/write request (address[15:10])
	unsigned int id;
	
	// Data buffer which backs the read/write request
	char data[MM_MAX_RW];
	
	// Saturating counts of the number of reads/writes to each byte in the buffer
	uint8_t read_count[MM_MAX_RW];
	uint8_t write_count[MM_MAX_RW];
	
	// How many read/write request packets have been responded to associated with
	// this read/write
	unsigned int n_responses_sent;
	
	// Link to next or NULL at the end of the list
	mm_rw_t *next;
};


/**
 * Data structure holding the state of the mock machine.
 */
struct mm {
	// Flag which is set to true when this structure should be freed. Checked by
	// outstanding write request callbacks to re-attempt freeing if their liveness
	// prevented freeing from completing.
	bool free;
	
	// The libuv event loop in use
	uv_loop_t *loop;
	
	// The address of the scoket being opened
	struct sockaddr_in addr;
	
	// The socket handle (data field points at this struct)
	uv_udp_t udp_handle;
	
	// A flag which indicates the udp_handle has been closed (and thus freeing may
	// occur)
	bool udp_handle_closed;
	
	// Linked list of packets that have arrived
	mm_req_t *reqs;
	
	// Linked list of read/write block requests
	mm_rw_t *rws;
};


/**
 * Allocate memory for the mock machine and open a port. Aborts on failure.
 */
mm_t *mm_init(uv_loop_t *loop);


/**
 * Get the UDP address bound. Wrapper around uv_udp_getsockname.
 */
void mm_getsockname(mm_t *mm, struct sockaddr *name, int *namelen);


/**
 * Get the request information for the specified sequence number. Allocates a
 * new mm_req_t if one with a matching sequence number is not found. Aborts on
 * failure.
 */
mm_req_t *mm_get_req(mm_t *mm, uint16_t seq_num);


/**
 * Get the read/write request information for the specified sequence number.
 * Allocates a new mm_rw_t if one with a matching ID is not found. Aborts on
 * failure.
 */
mm_rw_t *mm_get_rw(mm_t *mm, unsigned int id);


/**
 * Free resources used by the mock machine.
 *
 * Note: this command may actually free resources at some point in the future.
 * Specifically, any sent packets must complete their sending which requires
 * running the libuv event loop.
 */
void mm_free(mm_t *mm);


#endif
