/**
 * Internal functions which process received responses.
 */

#include <sys/socket.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <uv.h>

#include <rs.h>
#include <rs__internal.h>
#include <rs__scp.h>


void
rs__process_response_scp_packet(rs_conn_t *conn, rs__outstanding_t *os,
                                uv_buf_t buf)
{
	// Unpack the packet
	uint16_t cmd_rc;
	uint16_t seq_num;
	unsigned int n_args = os->data.scp_packet.n_args_recv;
	uint32_t arg1;
	uint32_t arg2;
	uint32_t arg3;
	uv_buf_t data;
	rs__unpack_scp_packet(buf,
	                      &cmd_rc,
	                      &seq_num,
	                      &n_args,
	                      &arg1, &arg2, &arg3,
	                      &data);
	
	// Copy the data into the user supplied buffer
	size_t data_len = MIN(os->data.scp_packet.data_max_len, data.len);
	memcpy(os->data.scp_packet.data.base, data.base, data_len);
	os->data.scp_packet.data.len = data_len;
	
	// Call the user's callback
	os->data.scp_packet.cb(conn, false,
	                       cmd_rc,
	                       n_args,
	                       arg1, arg2, arg3,
	                       os->data.scp_packet.data,
	                       os->cb_data);
}


void
rs__process_response_rw(rs_conn_t *conn, rs__outstanding_t *os,
                        uv_buf_t buf)
{
	int i;
	
	// Unpack the packet
	unsigned int n_args = 0;
	uint16_t cmd_rc;
	uint16_t seq_num;
	uv_buf_t data;
	rs__unpack_scp_packet(buf,
	                      &cmd_rc,
	                      &seq_num,
	                      &n_args, NULL, NULL, NULL,
	                      &data);
	
	// Check the response was OK and fail if not
	if (cmd_rc != RS__SCP_CMD_OK) {
		rs__cancel_outstanding(conn, os, cmd_rc);
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
	// Look for any *other* outstanding commands which are part of this request
	// which are still active.
	for (i = 0; i < conn->n_outstanding; i++) {
		if (&(conn->outstanding[i]) != os &&
		    conn->outstanding[i].active &&
		    conn->outstanding[i].type == os->type &&
		    conn->outstanding[i].data.rw.id == os->data.rw.id) {
			last_outstanding = false;
		}
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
rs__process_response(rs_conn_t *conn, rs__outstanding_t *os, uv_buf_t buf)
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
