/**
 * Publicly defined functions.
 */


#include <sys/socket.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <uv.h>

#include <rs.h>
#include <rs__internal.h>
#include <rs__scp.h>


rs_conn_t *
rs_init(uv_loop_t *loop,
        const struct sockaddr *addr,
        size_t scp_data_length,
        uint64_t timeout,
        unsigned int n_tries,
        unsigned int n_outstanding)
{
	rs_conn_t *conn = malloc(sizeof(rs_conn_t));
	if (!conn) return NULL;
	
	// Store arguments
	conn->loop = loop;
	conn->addr = addr;
	conn->scp_data_length = scp_data_length;
	conn->timeout = timeout;
	conn->n_tries = n_tries;
	conn->n_outstanding = n_outstanding;
	
	// Clear the 'free' flag since we don't wish to free the strucutre
	// immediately!
	conn->free = false;
	conn->free_cb = NULL;
	
	// Initialise counters
	conn->next_seq_num = 0;
	conn->next_rw_id = 0;
	
	// Initialise the socket
	if (uv_udp_init(conn->loop, &(conn->udp_handle))) {
		// Socket init failed!
		free(conn);
		return NULL;
	}
	conn->udp_handle_closed = false;
	
	// Pass a pointer to the SCP connection whenever UDP data arrives
	conn->udp_handle.data = (void *)conn;
	
	// Start listening for incoming packets
	if (uv_udp_recv_start(&(conn->udp_handle),
	                      rs__udp_recv_alloc_cb,
	                      rs__udp_recv_cb)) {
		// Listening failed
		// XXX: Doesn't close UDP handle before freeing!
		free(conn);
		return NULL;
	}
	
	
	// Create a queue for requests to be placed in
	conn->request_queue = rs__q_init(sizeof(rs__req_t));
	if (!conn->request_queue) {
		// Queue allocation failed!
		// XXX: Doesn't close UDP handle before freeing!
		free(conn);
		return NULL;
	}
	
	// Set up the outstanding slots
	conn->outstanding = calloc(conn->n_outstanding, sizeof(rs__outstanding_t));
	if (!conn->outstanding) {
		rs__q_free(conn->request_queue);
		// XXX: Doesn't close UDP handle before freeing!
		free(conn);
		return NULL;
	}
	int i;
	for (i = 0; i < conn->n_outstanding; i++) {
		conn->outstanding[i].conn = conn;
		
		conn->outstanding[i].active = false;
		conn->outstanding[i].send_req_active = false;
		conn->outstanding[i].cancelled = false;
		
		// Allocate sufficient space to buffer SCP packet data (and two empty
		// padding bytes required when transmitting SCP over UDP).
		conn->outstanding[i].packet.base =
			malloc(RS__SIZEOF_SCP_PACKET(3, conn->scp_data_length) + 2);
		if (!conn->outstanding[i].packet.base) {
			while (--i >= 0)
				free(conn->outstanding[i].packet.base);
			rs__q_free(conn->request_queue);
			free(conn);
			return NULL;
		}
		// Zero the two included padding bytes
		memset(conn->outstanding[i].packet.base, 0, 2);
		
		// Initialise the timer
		if (uv_timer_init(conn->loop, &(conn->outstanding[i].timer_handle))) {
			// Timer init failed, cleanup
			while (i >= 0)
				// XXX: Doesn't close timer handles before freeing!
				free(conn->outstanding[i--].packet.base);
			rs__q_free(conn->request_queue);
			free(conn);
			return NULL;
		}
		conn->outstanding[i].timer_handle_closed = false;
		
		// Set the user data for UDP requests and timer ticks.
		conn->outstanding[i].send_req.data = (void *)&(conn->outstanding[i]);
		conn->outstanding[i].timer_handle.data =
			(void *)&(conn->outstanding[i]);
	}
	
	return conn;
}


int
rs_send_scp(rs_conn_t *conn,
            uint16_t dest_addr,
            uint8_t dest_cpu,
            uint16_t cmd_rc,
            unsigned int n_args_send,
            unsigned int n_args_recv,
            uint32_t arg1,
            uint32_t arg2,
            uint32_t arg3,
            uv_buf_t data,
            size_t data_max_len,
            rs_send_scp_cb cb,
            void *cb_data)
{
	rs__req_t *req = (rs__req_t *)rs__q_insert(conn->request_queue);
	if (!req)
		return -1;
	
	// Queue up the supplied request
	req->type = RS__REQ_SCP_PACKET;
	req->dest_addr = dest_addr;
	req->dest_cpu = dest_cpu;
	req->data.scp_packet.cmd_rc = cmd_rc;
	req->data.scp_packet.n_args_send = n_args_send;
	req->data.scp_packet.n_args_recv = n_args_recv;
	req->data.scp_packet.arg1 = arg1;
	req->data.scp_packet.arg2 = arg2;
	req->data.scp_packet.arg3 = arg3;
	req->data.scp_packet.data = data;
	req->data.scp_packet.data_max_len = data_max_len;
	req->data.scp_packet.cb = cb;
	req->cb_data = cb_data;
	
	rs__process_request_queue(conn);
	
	return 0;
}


int
rs_write(rs_conn_t *conn,
         uint16_t dest_addr,
         uint8_t dest_cpu,
         uint32_t address,
         uv_buf_t data,
         rs_rw_cb cb,
         void *cb_data)
{
	rs__req_t *req = (rs__req_t *)rs__q_insert(conn->request_queue);
	if (!req)
		return -1;
	
	// Queue up the supplied request
	req->type = RS__REQ_WRITE;
	req->dest_addr = dest_addr;
	req->dest_cpu = dest_cpu;
	req->data.rw.id = conn->next_rw_id++;
	req->data.rw.address = address;
	req->data.rw.data = data;
	req->data.rw.orig_data = data;
	req->data.rw.cb = cb;
	req->cb_data = cb_data;
	
	rs__process_request_queue(conn);
	
	return 0;
}


int
rs_read(rs_conn_t *conn,
        uint16_t dest_addr,
        uint8_t dest_cpu,
        uint32_t address,
        uv_buf_t data,
        rs_rw_cb cb,
        void *cb_data)
{
	rs__req_t *req = (rs__req_t *)rs__q_insert(conn->request_queue);
	if (!req)
		return -1;
	
	// Queue up the supplied request
	req->type = RS__REQ_READ;
	req->dest_addr = dest_addr;
	req->dest_cpu = dest_cpu;
	req->data.rw.id = conn->next_rw_id++;
	req->data.rw.address = address;
	req->data.rw.data = data;
	req->data.rw.orig_data = data;
	req->data.rw.cb = cb;
	req->cb_data = cb_data;
	
	rs__process_request_queue(conn);
	
	return 0;
}


void
rs__udp_handle_closed_cb(uv_handle_t *handle)
{
	rs_conn_t *conn = (rs_conn_t *)handle->data;
	conn->udp_handle_closed = true;
	rs_free(conn, NULL, NULL);
}


void
rs__timer_handle_closed_cb(uv_handle_t *handle)
{
	rs__outstanding_t *os = (rs__outstanding_t *)handle->data;
	os->timer_handle_closed = true;
	rs_free(os->conn, NULL, NULL);
}



void
rs_free(rs_conn_t *conn, rs_free_cb cb, void *cb_data)
{
	int i;
	
	// Set the free flag so that if the final free is postponed, the residual
	// callbacks know to re-attempt freeing.
	conn->free = true;
	
	// Store the callback and data (if supplied)
	if (cb) {
		conn->free_cb = cb;
		conn->free_cb_data = cb_data;
	}
	
	// Stop receiving data
	uv_udp_recv_stop(&(conn->udp_handle));
	
	// Close the UDP handle
	if (!uv_is_closing((uv_handle_t *)&(conn->udp_handle)))
		uv_close((uv_handle_t *)&(conn->udp_handle), rs__udp_handle_closed_cb);
	
	for (i = 0; i < conn->n_outstanding; i++) {
		// Cancel all outstanding requests
		rs__cancel_outstanding(conn, &(conn->outstanding[i]), RS_EFREE, -1);
		
		// Close the timer handles
		if (!uv_is_closing((uv_handle_t *)&(conn->outstanding[i].timer_handle)))
			uv_close((uv_handle_t *)&(conn->outstanding[i].timer_handle),
			         rs__timer_handle_closed_cb);
	}
	
	// Cancel all remaining queued requests
	rs__req_t *req;
	while ((req = rs__q_remove(conn->request_queue)))
		rs__cancel_queued(conn, req, RS_EFREE);
	
	// Check whether any UDP send requests are active or timer handle closures
	// (which require us to postpone the free since their handles would get freed
	// too!)
	for (i = 0; i < conn->n_outstanding; i++) {
		if (conn->outstanding[i].send_req_active)
			return;
		if (!conn->outstanding[i].timer_handle_closed)
			return;
	}
	
	// Likewise with the UDP handle
	if (!conn->udp_handle_closed)
		return;
	
	// Everything has shut down, free all resources now!
	for (i = 0; i < conn->n_outstanding; i++)
		free(conn->outstanding[i].packet.base);
	free(conn->outstanding);
	rs__q_free(conn->request_queue);
	
	// Just before freeing the main struct, take a copy of the callback function
	cb = conn->free_cb;
	cb_data = conn->free_cb_data;
	free(conn);
	
	// Call the callback (if defined)
	if (cb)
		cb(cb_data);
}


static const char RS__EBAD_RC_NAME[] = "RS_EBAD_RC";
static const char RS__EBAD_RC_MSG[] = "Bad response to CMD_READ/CMD_WRITE";

static const char RS__ETIMEOUT_NAME[] = "RS_ETIMEOUT";
static const char RS__ETIMEOUT_MSG[] = "SCP command timed out";

static const char RS__EFREE_NAME[] = "RS_EFREE";
static const char RS__EFREE_MSG[] = "SCP connection was closed/freed";


const char *
rs_strerror(int err)
{
	switch (err) {
		case RS_EBAD_RC:  return RS__EBAD_RC_MSG;
		case RS_ETIMEOUT: return RS__ETIMEOUT_MSG;
		case RS_EFREE:    return RS__EFREE_MSG;
		default:          return uv_strerror(err);
	}
}


const char *
rs_err_name(int err)
{
	switch (err) {
		case RS_EBAD_RC:  return RS__EBAD_RC_NAME;
		case RS_ETIMEOUT: return RS__ETIMEOUT_NAME;
		case RS_EFREE:    return RS__EFREE_NAME;
		default:          return uv_err_name(err);
	}
}
