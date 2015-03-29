/**
 * Mock machine echo server implementation.
 */

// XXX
#include <stdio.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <uv.h>

#include "rs__scp.h"

#include "mock_machine.h"

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
 * Unpack the sequence number from a packet.
 */
#define MM__SEQ_NUM(p) (((sdp_scp_header_t *)(p))->seq_num)

/**
 * Unpack the sequence number from a packet.
 */
#define MM__CMD_RC(p) (((sdp_scp_header_t *)(p))->cmd_rc)

/**
 * Unpack the response delay from a packet according to the definitions at the
 * top of the headder file.
 */
#define MM__DELAY(p) ((((sdp_scp_header_t *)(p))->dest_addr >> 8) & 0xFF)

/**
 * Unpack the number of transmission attempts from a packet according to the
 * definitions at the top of the headder file.
 */
#define MM__N_TRIES(p) ((((sdp_scp_header_t *)(p))->dest_addr >> 0) & 0xFF)

/**
 * Unpack the number of transmission attempts from a packet according to the
 * definitions at the top of the headder file.
 */
#define MM__N_DUPLICATES(p) \
	((((sdp_scp_header_t *)(p))->dest_port_cpu >> 0) & 0x1F)

/**
 * Unpack the read/write ID from a read/write packet according to the
 * definitions at the top of the headder file.
 */
#define MM__RW_ID(p) ((((sdp_scp_header_t *)(p))->arg1 >> 10) & 0x3F)

/**
 * Unpack the offset from a read/write packet according to the definitions at
 * the top of the headder file.
 */
#define MM__RW_ADDR(p) ((((sdp_scp_header_t *)(p))->arg1 >> 0) & 0x3FF)

/**
 * Unpack the length from a read/write packet according to the definitions at
 * the top of the headder file.
 */
#define MM__RW_LENGTH(p) (((sdp_scp_header_t *)(p))->arg2)

/**
 * Unpack the number of correctly-responded-to requests from a read/write packet
 * according to the definitions at the top of the headder file.
 */
#define MM__RW_N_RESP_BEFORE_ERROR(p) \
	((((sdp_scp_header_t *)(p))->arg1 >> 16) & 0xFF)

/**
 * Unpack the number of instantly-responded-to requests from a read/write packet
 * according to the definitions at the top of the headder file.
 */
#define MM__RW_N_RESP_BEFORE_SLOW(p) \
	((((sdp_scp_header_t *)(p))->arg1 >> 24) & 0xFF)


/**
 * A struct which holds the state required to transmit a packet.
 */
typedef struct {
	// The associated response
	mm_resp_t *resp;
	
	// The UDP send request itself (will be set to point to this struct)
	uv_udp_send_t udp_send_req;
	
	// The buffer object defining the data to send
	uv_buf_t buf;
} mm__udp_send_t;


/**
 * Internal function: Allocate a new response handler
 */
static mm_resp_t *mm__new_resp(mm_t *mm, mm_req_t *req);


/**
 * Internal function: data arrival allocation callback.
 *
 * Allocates a tempoary buffer to place the data in before it is copied.
 */
static void mm__recv_alloc_cb(uv_handle_t *handle,
                              size_t suggested_size,
                              uv_buf_t *buf);


/**
 * Internal function: data arrival callback.
 *
 * Deals with the incoming packet.
 */
static void mm__recv_cb(uv_udp_t *handle,
                        ssize_t nread,
                        const uv_buf_t *buf,
                        const struct sockaddr *addr,
                        unsigned flags);


/**
 * Internal function: timer callback.
 *
 * Generate a response in a malloc'd buffer and schedule it to be sent an
 * appropriate number of times.
 */
static void mm__timer_cb(uv_timer_t *handle);


/**
 * Internal function: data transmitted callback.
 *
 * Free the memory in which the response packet was buffered. Clear the sending
 * flag and call free if free was waiting on this callback to occur.
 */
static void mm__send_cb(uv_udp_send_t *send_req, int status);


/**
 * Internal function: Echo a generic packet.
 */
static void mm__pack_response_generic(mm_t *mm, mm_req_t *req, mm_resp_t *resp,
                                      uv_buf_t *buf);


/**
 * Internal function: Read some data back.
 */
static void mm__pack_response_read(mm_t *mm, mm_req_t *req, mm_resp_t *resp,
                                   uv_buf_t *buf);


/**
 * Internal function: Write some data back.
 */
static void mm__pack_response_write(mm_t *mm, mm_req_t *req, mm_resp_t *resp,
                                    uv_buf_t *buf);


mm_t *
mm_init(uv_loop_t *loop)
{
	mm_t *mm = malloc(sizeof(mm_t));
	if (!mm) abort();
	
	mm->free = false;
	mm->loop = loop;
	
	// Open an arbitrary local UDP socket for listening
	mm->udp_handle.data = (void *)mm;
	if (uv_udp_init(mm->loop, &(mm->udp_handle))) abort();
	if (uv_ip4_addr("0.0.0.0", 0, &(mm->addr))) abort();
	if (uv_udp_bind(&(mm->udp_handle),
	                (struct sockaddr *)&(mm->addr),
	                UV_UDP_REUSEADDR)) abort();
	if (uv_udp_recv_start(&(mm->udp_handle),
	                      mm__recv_alloc_cb,
	                      mm__recv_cb)) abort();
	mm->udp_handle_closed = false;
	
	// Initialise linked lists
	mm->reqs = NULL;
	mm->rws = NULL;
	
	return mm;
}


void
mm_getsockname(mm_t *mm, struct sockaddr *name, int *namelen)
{
	uv_udp_getsockname(&(mm->udp_handle), name, namelen);
}


mm_req_t *
mm_get_req(mm_t *mm, uint16_t seq_num)
{
	// Try to find an existing request
	mm_req_t *req = mm->reqs;
	while (req) {
		if (req->seq_num == seq_num) {
			return req;
		}
		req = req->next;
	}
	
	// Make a new one
	req = malloc(sizeof(mm_req_t));
	if (!req) abort();
	
	req->mm = mm;
	req->seq_num = seq_num;
	req->buf.base = (void *)req->packet;
	req->buf.len = 0;
	req->n_changes = 0;
	req->n_tries = 0;
	req->resps = NULL;
	
	// Insert into the linked list
	req->next = mm->reqs;
	mm->reqs = req;
	return req;
}


mm_rw_t *
mm_get_rw(mm_t *mm, unsigned int id)
{
	// Try to find an existing request
	mm_rw_t *rw = mm->rws;
	while (rw) {
		if (rw->id == id)
			return rw;
		rw = rw->next;
	}
	
	// Make a new one
	rw = malloc(sizeof(mm_rw_t));
	if (!rw) abort();
	
	rw->id = id;
	
	size_t i;
	for (i = 0; i < MM_MAX_RW; i++) {
		rw->read_count[i] = 0;
		rw->write_count[i] = 0;
	}
	rw->n_responses_sent = 0;
	
	// Insert into the linked list
	rw->next = mm->rws;
	mm->rws = rw;
	
	return rw;
}


static mm_resp_t *
mm__new_resp(mm_t *mm, mm_req_t *req)
{
	mm_resp_t *resp = malloc(sizeof(mm_resp_t));
	if (!resp) abort();
	
	resp->req = req;
	
	uv_timer_init(mm->loop, &(resp->timer_handle));
	resp->timer_handle_closed = false;
	resp->udp_send_active = 0;
	
	// Set up timer handle pointer to the response object
	resp->timer_handle.data = (void *)resp;
	
	// Insert into linked list
	resp->next = req->resps;
	req->resps = resp;
	
	return resp;
}


static void
mm__recv_alloc_cb(uv_handle_t *handle,
                  size_t suggested_size,
                  uv_buf_t *buf)
{
	// Allocate the required size for the arriving packet.
	buf->base = malloc(suggested_size);
	if (!buf->base) abort();
	buf->len = suggested_size;
}


static void
mm__recv_cb(uv_udp_t *handle,
            ssize_t nread,
            const uv_buf_t *buf,
            const struct sockaddr *addr,
            unsigned flags)
{
	if (nread == 0 && addr == NULL) {
		// No more data to come, free the buffer and terminate now
		free(buf->base);
		return;
	} else if (nread < 0 || (nread == 0 && addr != NULL)) {
		// An error ocurred
		abort();
	}
	
	mm_t *mm = (mm_t *)handle->data;
	mm_req_t *req = mm_get_req(mm, MM__SEQ_NUM(buf->base + 2));
	
	// Sanity check that the arriving packet (less its padding) isn't too large
	// for the buffers allocated
	if (nread - 2 > sizeof(req->packet)) abort();
	
	// See if the packet changed since the last attempt
	if (req->buf.len != nread - 2 ||
	    memcmp(req->buf.base, buf->base + 2, nread - 2)) {
		// If so, copy it in
		req->n_changes++;
		memcpy(req->buf.base, buf->base + 2, nread - 2);
		req->buf.len = nread - 2;
	}
	
	req->n_tries++;
	
	// What should the delay before the response be
	uint64_t delay = MM__DELAY(req->buf.base);
	
	// On which numbered transmision attempt will the request succeed
	unsigned int n_tries = MM__N_TRIES(req->buf.base);
	
	// Special case for read/writes
	bool is_rw = MM__CMD_RC(req->buf.base) == RS__SCP_CMD_READ ||
	             MM__CMD_RC(req->buf.base) == RS__SCP_CMD_WRITE;
	if (is_rw) {
		mm_rw_t *rw = mm_get_rw(mm, MM__RW_ID(req->buf.base));
		// Start responding slowly only after a specified number of attempts
		if (MM__RW_N_RESP_BEFORE_SLOW(req->buf.base) != 255 &&
		    rw->n_responses_sent < MM__RW_N_RESP_BEFORE_SLOW(req->buf.base)) {
			n_tries = 1;
		}
	}
	
	// Send the packet if the response is due on this cycle
	if (req->n_tries == n_tries) {
		mm_resp_t *resp = mm__new_resp(mm, req);
		
		// Copy the return address to the request
		memcpy(&(resp->addr), addr, sizeof(struct sockaddr));
		
		// Set up the timer to dispatch the response atfer the given delay
		if (uv_timer_start(&(resp->timer_handle), mm__timer_cb, delay, 0)) abort();
	}
	
	// Free the buffer into which the packet arrived
	free(buf->base);
}


static void
mm__timer_cb(uv_timer_t *timer_handle)
{
	mm_resp_t *resp = (mm_resp_t *)timer_handle->data;
	mm_req_t *req = resp->req;
	mm_t *mm = req->mm;
	
	uv_buf_t buf;
	buf.len = 0;
	
	// Genetate the appropriate packet in response
	switch (MM__CMD_RC(req->buf.base)) {
		case RS__SCP_CMD_READ:
			mm__pack_response_read(mm, req, resp, &buf);
			break;
		
		case RS__SCP_CMD_WRITE:
			mm__pack_response_write(mm, req, resp, &buf);
			break;
		
		default:
			mm__pack_response_generic(mm, req, resp, &buf);
			break;
	}
	
	// Send the packet and all its duplicates
	int i;
	for (i = 0; i < MM__N_DUPLICATES(req->buf.base) + 1; i++) {
		// Reserve memory for the UDP send state
		mm__udp_send_t *send = malloc(sizeof(mm__udp_send_t));
		if (!send) abort();
		send->resp = resp;
		send->udp_send_req.data = (void *)send;
		
		// Copy the response packet for this request
		if (buf.len) {
			send->buf.base = malloc(buf.len);
			if (!send->buf.base) abort();
			memcpy(send->buf.base, buf.base, buf.len);
		}
		send->buf.len = buf.len;
		
		// Send the request
		resp->udp_send_active++;
		
		if (uv_udp_send(&(send->udp_send_req), &(mm->udp_handle),
		                &(send->buf), 1, &(resp->addr), mm__send_cb)) abort();
	}
	
	// Free the original copy of the response packet
	free(buf.base);
}


static void
mm__pack_response_generic(mm_t *mm, mm_req_t *req, mm_resp_t *resp,
                          uv_buf_t *buf)
{
	// Make a copy of the packet to be returned verbatim with two padding bytes
	// added.
	buf->base = malloc(req->buf.len + 2);
	if (!buf->base) abort();
	memset(buf->base, 0, 2);
	memcpy(buf->base + 2, req->buf.base, req->buf.len);
	buf->len = req->buf.len + 2;
}


static void
mm__pack_response_read(mm_t *mm, mm_req_t *req, mm_resp_t *resp,
                       uv_buf_t *buf)
{
	void *p = req->buf.base;
	
	// Crash if the request goes out of memory
	if (MM__RW_ADDR(p) + MM__RW_LENGTH(p) > MM_MAX_RW)
		abort();
	
	mm_rw_t *rw = mm_get_rw(mm, MM__RW_ID(req->buf.base));
	
	// Generate a response packet, initially based on the request with the
	// arguments stripped out
	buf->base = malloc(2 + RS__SIZEOF_SCP_PACKET(0, MM__RW_LENGTH(p)));
	if (!buf->base) abort();
	buf->len = RS__SIZEOF_SCP_PACKET(0, MM__RW_LENGTH(p)) + 2;
	memset(buf->base, 0, 2);
	memcpy(buf->base + 2, req->buf.base, RS__SIZEOF_SCP_PACKET(0, 0));
	
	// Report failure as required
	if (MM__RW_N_RESP_BEFORE_ERROR(p) == 255 ||
	    rw->n_responses_sent != MM__RW_N_RESP_BEFORE_ERROR(p))
		MM__CMD_RC(buf->base + 2) = RS__SCP_CMD_OK;
	else
		MM__CMD_RC(buf->base + 2) = 0;
	
	// Copy the requested data into the response payload
	memcpy(((char *)buf->base) + 2 + RS__SIZEOF_SCP_PACKET(0, 0),
	       rw->data + MM__RW_ADDR(p),
	       MM__RW_LENGTH(p));
	
	// Update counters
	size_t addr;
	for (addr = MM__RW_ADDR(p);
	     addr - MM__RW_ADDR(p) < MM__RW_LENGTH(p);
	     addr++)
		if (rw->read_count[addr] < 255)
			rw->read_count[addr]++;
	
	// Count the number of responses dealt with
	rw->n_responses_sent++;
}


static void
mm__pack_response_write(mm_t *mm, mm_req_t *req, mm_resp_t *resp,
                        uv_buf_t *buf)
{
	void *p = req->buf.base;
	
	// Crash if the write length is longer than the packet received or longer than
	// memory
	if (MM__RW_ADDR(p) + MM__RW_LENGTH(p) > MM_MAX_RW ||
	    MM__RW_LENGTH(p) > req->buf.len - RS__SIZEOF_SCP_PACKET(3, 0))
		abort();
	
	mm_rw_t *rw = mm_get_rw(mm, MM__RW_ID(req->buf.base));
	
	// Generate a response packet, initially based on the request with the
	// arguments stripped out (including 2 bytes padding)
	buf->base = malloc(RS__SIZEOF_SCP_PACKET(0, 0) + 2);
	if (!buf->base) abort();
	buf->len = RS__SIZEOF_SCP_PACKET(0, 0) + 2;
	memset(buf->base, 0, 2);
	memcpy(buf->base + 2, req->buf.base, RS__SIZEOF_SCP_PACKET(0, 0));
	
	// Report failiure as required
	if (MM__RW_N_RESP_BEFORE_ERROR(p) == 255 ||
	    rw->n_responses_sent != MM__RW_N_RESP_BEFORE_ERROR(p))
		MM__CMD_RC(buf->base + 2) = RS__SCP_CMD_OK;
	else
		MM__CMD_RC(buf->base + 2) = 0;
	
	// Copy the supplied data into the 'memory'
	memcpy(rw->data + MM__RW_ADDR(p),
	       ((char *)p) + RS__SIZEOF_SCP_PACKET(3, 0),
	       MM__RW_LENGTH(p));
	
	// Update counters
	size_t addr;
	for (addr = MM__RW_ADDR(p);
	     addr - MM__RW_ADDR(p) < MM__RW_LENGTH(p);
	     addr++)
		if (rw->write_count[addr] < 255)
			rw->write_count[addr]++;
	
	// Count the number of responses dealt with
	rw->n_responses_sent++;
}


static void
mm__send_cb(uv_udp_send_t *send_req, int status)
{
	mm__udp_send_t *send = (mm__udp_send_t *)send_req->data;
	mm_resp_t *resp = send->resp;
	mm_t *mm = resp->req->mm;
	
	resp->udp_send_active--;
	
	// Free the send buffer (and request handler)
	free(send->buf.base);
	free(send);
	
	// Complete the free if required
	if (mm->free)
		mm_free(mm);
}


/**
 * Callback when a timer's handle has been closed.
 */
void
mm__timer_handle_close_cb(uv_handle_t *handle)
{
	mm_resp_t *resp = (mm_resp_t *)handle->data;
	resp->timer_handle_closed = true;
	mm_free(resp->req->mm);
}


/**
 * Callback when the UDP port's handle has been closed.
 */
void
mm__udp_handle_close_cb(uv_handle_t *handle)
{
	mm_t *mm = (mm_t *)handle->data;
	mm->udp_handle_closed = true;
	mm_free(mm);
}


void
mm_free(mm_t *mm)
{
	mm_req_t *req;
	mm_resp_t *resp;
	mm_rw_t *rw;
	
	mm->free = true;
	
	// See if any requests are still outstanding (and cancel and close all timers
	// while we're at it)
	bool some_outstanding = false;
	req = mm->reqs;
	while (req) {
		resp = req->resps;
		while (resp) {
			uv_timer_stop(&(resp->timer_handle));
			if (!uv_is_closing((uv_handle_t *)&resp->timer_handle))
				uv_close((uv_handle_t *)&resp->timer_handle, mm__timer_handle_close_cb);
			if (resp->udp_send_active)
				some_outstanding = true;
			if (!resp->timer_handle_closed)
				some_outstanding = true;
			resp = resp->next;
		}
		req = req->next;
	}
	
	// Close the UDP handle
	if (!uv_is_closing((uv_handle_t *)&mm->udp_handle))
		uv_close((uv_handle_t *)&mm->udp_handle, mm__udp_handle_close_cb);
	if (!mm->udp_handle_closed)
		some_outstanding = true;
	
	// If any requests are outstanding we can't free anything so must let them
	// re-call this function when they see the free flag.
	if (some_outstanding) {
		return;
	} else {
		// FREE ALL THE THINGS!
		uv_udp_recv_stop(&(mm->udp_handle));
		
		// Free requests and responses
		req = mm->reqs;
		while (req) {
			// Free responses
			resp = req->resps;
			while (resp) {
				mm_resp_t *resp_next = resp->next;
				free(resp);
				resp = resp_next;
			}
			
			// Finally, free request
			mm_req_t *req_next = req->next;
			free(req);
			req = req_next;
		}
		
		// Free read/write blocks
		rw = mm->rws;
		while (rw) {
			mm_rw_t *rw_next = rw->next;
			free(rw);
			rw = rw_next;
		}
		
		// Finally, free the model
		free(mm);
		return;
	}
}
