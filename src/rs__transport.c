/**
 * Internal functions which implement the main SCP transport logic.
 */

#include <sys/socket.h>

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <uv.h>

#include <rs.h>
#include <rs__internal.h>
#include <rs__scp.h>


void
rs__attempt_transmission(rs_conn_t *conn, rs__outstanding_t *os)
{
	// Don't do anything if the channel has been cancelled. Any required callbacks
	// have already been dealt with.
	if (!os->active)
		return;
	
	if (++os->n_tries <= conn->n_tries) {
		// Attempt to transmit
		if (uv_udp_send(&(os->send_req),
		                &(conn->udp_handle),
		                &(os->packet), 1,
		                conn->addr,
		                rs__udp_send_cb)) {
			// Transmission failiure: clean up
			rs__cancel_outstanding(conn, os);
			return;
		}
		os->send_req_active = true;
	} else {
		// Maximum number of attempts made, fail and clean up.
		rs__cancel_outstanding(conn, os);
	}
}


void
rs__timer_cb(uv_timer_t *handle)
{
	rs__outstanding_t *os = (rs__outstanding_t *)handle->data;
	
	// The packet didn't arrive, attempt retransmission (which will fail if done
	// too many times)
	rs__attempt_transmission(os->conn, os);
}


void
rs__udp_send_cb(uv_udp_send_t *req, int status)
{
	rs__outstanding_t *os = (rs__outstanding_t *)req->data;
	rs_conn_t *conn = os->conn;
	
	// Record that we've recieved the callback for the request
	os->send_req_active = false;
	
	// If we were waiting on this callback before freeing, we can now re-attempt
	// the free and quit.
	if (conn->free) {
		rs_free(conn);
		return;
	}
	
	// If we were waiting on this callback before the channel could be marked as
	// inactive after cancellation, do that.
	if (os->active && os->cancelled) {
		os->active = false;
		os->cancelled = false;
		
		// Now that the channel is nolonger active, we may potentially handle new
		// requests.
		rs__process_request_queue(conn);
		return;
	}
	
	// If something went wrong, cancel the request
	if (status != 0) {
		rs__cancel_outstanding(os->conn, os);
		return;
	}
	
	// The packet has been dispatched, setup a timeout for the response
	uv_timer_start(&(os->timer_handle), rs__timer_cb, conn->timeout, 0);
}


void
rs__udp_recv_alloc_cb(uv_handle_t *handle,
                      size_t suggested_size, uv_buf_t *buf)
{
	// XXX: Just use malloc for now...
	buf->base = malloc(suggested_size);
	
	if (buf->base)
		buf->len = suggested_size;
	else
		buf->len = 0;
}


void
rs__udp_recv_cb(uv_udp_t *handle,
                ssize_t nread, const uv_buf_t *buf,
                const struct sockaddr *addr,
                unsigned int flags)
{
	rs_conn_t *conn = (rs_conn_t *)(handle->data);
	
	int i;
	
	// Ignore anything which isn't long enough to be an SCP packet
	if (nread >= RS__SIZEOF_SCP_PACKET(0, 0)) {
		// Check to see if a packet with this sequence number is outstanding (if
		// not, the packet is ignored too)
		uint16_t seq_num = rs__unpack_scp_packet_seq_num(*buf);
		for (i = 0; i < conn->n_outstanding; i++) {
			rs__outstanding_t *os = conn->outstanding + i;
			if (os->active && os->seq_num == seq_num) {
				uv_buf_t buf_ = *buf;
				buf_.len = nread;
				rs__process_response(conn, os, &buf_);
				break;
			}
		}
	}
	
	// Free receive buffer
	if (buf->base)
		free(buf->base);
}


void
rs__process_response_scp_packet(rs_conn_t *conn, rs__outstanding_t *os,
                                uv_buf_t *buf)
{
	// Unpack the packet
	uint16_t cmd_rc;
	uint16_t seq_num;
	uint32_t arg1;
	uint32_t arg2;
	uint32_t arg3;
	uv_buf_t data;
	rs__unpack_scp_packet(*buf,
	                      &cmd_rc,
	                      &seq_num,
	                      os->data.scp_packet.n_args_recv,
	                      &arg1, &arg2, &arg3,
	                      &data);
	
	// Copy the data into the user supplied buffer
	size_t data_len = MIN(os->data.scp_packet.data_max_len, data.len);
	memcpy(os->data.scp_packet.data.base, data.base, data_len);
	os->data.scp_packet.data.len = data_len;
	
	// Call the user's callback
	os->data.scp_packet.cb(conn, false,
	                       cmd_rc,
	                       os->data.scp_packet.n_args_recv,
	                       arg1, arg2, arg3,
	                       os->data.scp_packet.data,
	                       os->cb_data);
}


void
rs__process_response_rw(rs_conn_t *conn, rs__outstanding_t *os,
                        uv_buf_t *buf)
{
	int i;
	
	// Unpack the packet
	uint16_t cmd_rc;
	uint16_t seq_num;
	uv_buf_t data;
	rs__unpack_scp_packet(*buf,
	                      &cmd_rc,
	                      &seq_num,
	                      0, NULL, NULL, NULL,
	                      &data);
	
	// Check the response was OK and fail if not
	if (cmd_rc != RS__SCP_CMD_OK) {
		rs__cancel_outstanding(conn, os);
		return;
	}
	
	// If reading, copy the received data into the user supplied buffer
	if (os->type == RS__REQ_READ) {
		size_t data_len = MIN(os->data.rw.data.len, data.len);
		memcpy(os->data.rw.data.base, data.base, data_len);
		os->data.rw.data.len = data_len;  // Not actually used anywhere
	}
	
	// Determine if this is the last outstanding command in the request.
	bool last_outstanding = true;
	// Look for any outstanding commands which are part of this request which are
	// still active.
	for (i = 0; i < conn->n_outstanding; i++) {
		if (conn->outstanding[i].active &&
		    conn->outstanding[i].type == os->type &&
		    conn->outstanding[i].data.rw.id == os->data.rw.id)
			last_outstanding = false;
	}
	// Check to see if this command relates to the command at the head of the
	// request queue.
	rs__req_t *req = (rs__req_t *)rs__q_peek(conn->request_queue);
	if (req && req->type == os->type && req->data.rw.id == os->data.rw.id)
		last_outstanding = false;
	
	// If this was the last outstanding command, call the users callback.
	if (last_outstanding) {
		os->data.rw.cb(conn, false,
		               cmd_rc,
		               os->data.rw.orig_data,
		               os->cb_data);
	}
}


void
rs__process_response(rs_conn_t *conn, rs__outstanding_t *os, uv_buf_t *buf)
{
	// Stop the timeout timer
	uv_timer_stop(&(os->timer_handle));
	
	// Deal with the packet depending on its type
	switch (os->type) {
		case RS__REQ_SCP_PACKET:
			rs__process_response_scp_packet(conn, os, buf);
			break;
		
		case RS__REQ_READ:
		case RS__REQ_WRITE:
			rs__process_response_rw(conn, os, buf);
			break;
	}
	
	// Mark this outstanding channel as inactive again and trigger queue
	// processing since we just freed up an outstanding channel.
	os->active = false;
	rs__process_request_queue(conn);
}
