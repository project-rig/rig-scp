/**
 * Internal functions for request queue processing.
 */

#include <sys/socket.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <uv.h>

#include <rs.h>
#include <rs__internal.h>
#include <rs__scp.h>


void
rs__process_queued_scp_packet(rs_conn_t *conn,
                              rs__req_t *req,
                              rs__outstanding_t *os)
{
	os->active = true;
	os->type = RS__REQ_SCP_PACKET;
	os->seq_num = conn->next_seq_num++;
	os->n_tries = 0;
	
	// Keep a pointer to the location to store the response
	os->data.scp_packet.n_args_recv = req->data.scp_packet.n_args_recv;
	os->data.scp_packet.data = req->data.scp_packet.data;
	os->data.scp_packet.data_max_len = req->data.scp_packet.data_max_len;
	
	// Record the callback (and its data)
	os->data.scp_packet.cb = req->data.scp_packet.cb;
	os->cb_data = req->cb_data;
	
	// Pack the packet ready for transmission
	rs__pack_scp_packet(&(os->packet),
	                    conn->scp_data_length,
	                    req->dest_addr,
	                    req->dest_cpu,
	                    req->data.scp_packet.cmd_rc,
	                    os->seq_num,
	                    req->data.scp_packet.n_args_send,
	                    req->data.scp_packet.arg1,
	                    req->data.scp_packet.arg2,
	                    req->data.scp_packet.arg3,
	                    req->data.scp_packet.data);
}


bool
rs__process_queued_rw(rs_conn_t *conn,
                      rs__req_t *req,
                      rs__outstanding_t *os)
{
	os->active = true;
	os->type = req->type;
	os->seq_num = conn->next_seq_num++;
	os->data.rw.id = req->data.rw.id;
	os->n_tries = 0;
	
	// Slice off a chunk of the data as large as will fit in a packet
	uint32_t address = req->data.rw.address;
	os->data.rw.orig_data = req->data.rw.orig_data;
	os->data.rw.data = req->data.rw.data;
	os->data.rw.data.len = MIN(os->data.rw.data.len, conn->scp_data_length);
	
	// Update the request accordingly
	req->data.rw.address += os->data.rw.data.len;
	req->data.rw.data.base += os->data.rw.data.len;
	req->data.rw.data.len -= os->data.rw.data.len;
	
	// Record the callback (and its data)
	os->data.rw.cb = req->data.rw.cb;
	os->cb_data = req->cb_data;
	
	// Work out the type of read/write request based on the address and length
	uint32_t req_type = rs__scp_rw_type(address, os->data.rw.data.len);
	
	// Pack the packet ready for transmission
	if (os->type == RS__REQ_READ) {
		uv_buf_t empty;
		empty.len = 0;
		rs__pack_scp_packet(&(os->packet),
		                    conn->scp_data_length,
		                    req->dest_addr,
		                    req->dest_cpu,
		                    RS__SCP_CMD_READ,
		                    os->seq_num,
		                    3,
		                    address,
		                    os->data.rw.data.len,
		                    req_type,
		                    empty);
	} else {
		rs__pack_scp_packet(&(os->packet),
		                    conn->scp_data_length,
		                    req->dest_addr,
		                    req->dest_cpu,
		                    RS__SCP_CMD_WRITE,
		                    os->seq_num,
		                    3,
		                    address,
		                    os->data.rw.data.len,
		                    req_type,
		                    os->data.rw.data);
	}
	
	// The last packet has been sent if the remaining data is empty
	return req->data.rw.data.len <= 0;
}


void
rs__process_request_queue(rs_conn_t *conn)
{
	int i;
	
	// Process as many packets as possible before running out
	while (1) {
		// Find a free outstanding channel
		rs__outstanding_t *os = NULL;
		for (i = 0; i < conn->n_outstanding; i++) {
			if (!conn->outstanding[i].active) {
				os = &(conn->outstanding[i]);
				break;
			}
		}
		
		// Find a request to send
		rs__req_t *req = (rs__req_t *)rs__q_peek(conn->request_queue);
		
		// Stop if there is no available channel or request
		if (!os || !req)
			return;
		
		// Place the request int the outstanding channel
		switch (req->type) {
			case RS__REQ_SCP_PACKET:
				rs__process_queued_scp_packet(conn, req, os);
				rs__q_remove(conn->request_queue);
				break;
				
			case RS__REQ_READ:
			case RS__REQ_WRITE:
				if (rs__process_queued_rw(conn, req, os))
					rs__q_remove(conn->request_queue);
				break;
		}
		
		// Transmit the packet
		rs__attempt_transmission(conn, os);
	}
}
