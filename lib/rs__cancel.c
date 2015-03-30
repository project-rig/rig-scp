/**
 * Internal functions for cancelling ongoing requests.
 */

#include <sys/socket.h>

#include <stdint.h>
#include <stdbool.h>

#include <uv.h>

#include <rs.h>
#include <rs__internal.h>
#include <rs__scp.h>


void
rs__cancel_outstanding(rs_conn_t *conn, rs__outstanding_t *os,
                       int error, uint16_t cmd_rc)
{
	int i;
	
	// Don't bother if the request has already been cancelled
	if (!os->active || os->cancelled)
		return;
	
	// Indicate that this request has been cancelled
	if (!os->send_req_active) {
		os->active = false;
	} else {
		// We can't mark this channel as inactive until the send request completes
		// (otherwise it would be reused too soon). As a result the cancelled flag
		// is set which will cause the scp send request callback to finally flip the
		// active flag.
		os->cancelled = true;
	}
	
	// Kill the timeout timer (if running)
	if (uv_is_active((uv_handle_t *)&(os->timer_handle)))
		uv_timer_stop(&(os->timer_handle));
	
	// This flag is set if this cancellation also requires that another
	// outstanding channel must also be cancelled(i.e. in the case of reads and
	// writes).
	bool others_to_cancel = false;
	// If this is a read/write, several things may require cancelling
	if (os->type == RS__REQ_READ || os->type == RS__REQ_WRITE) {
		// Find the other outstanding channels which are performing the same
		// read/write request which must be cancelled too.
		for (i = 0; i < conn->n_outstanding; i++) {
			rs__outstanding_t *other_os = conn->outstanding + i;
			if (// Skip things which have already been cancelled
			    other_os->active && !other_os->cancelled &&
			    // Skip things that aren't reads or writes (they won't have anything
			    // to do with this request)
			    other_os->type == os->type &&
			    // Skip reads/writes which aren't part of the same request
			    other_os->data.rw.id == os->data.rw.id) {
				others_to_cancel = true;
			}
		}
	}
	
	// Send the user callback indicating failiure. If this is a read/write
	// request, multiple outstanding channels may be cancelled and to prevent the
	// user callback being called multiple times, only the last one to be
	// cancelled will raise the callback.
	if (!others_to_cancel) {
		switch (os->type) {
			case RS__REQ_SCP_PACKET:
				os->data.scp_packet.cb(conn, error,
				                       cmd_rc, 0, 0, 0, 0, os->data.scp_packet.data,
				                       os->cb_data);
				break;
			
			case RS__REQ_READ:
			case RS__REQ_WRITE:
				os->data.rw.cb(conn, error,
				               cmd_rc, os->data.rw.orig_data,
				               os->cb_data);
				break;
		}
	}
	
	// If this is a read/write, several things may require cancelling
	if (os->type == RS__REQ_READ || os->type == RS__REQ_WRITE) {
		// Find the other outstanding channels which are performing the same
		// read/write request and cancel them too.
		for (i = 0; i < conn->n_outstanding; i++) {
			rs__outstanding_t *other_os = conn->outstanding + i;
			if (// Skip things which have already been cancelled
			    other_os->active && !other_os->cancelled &&
			    // Skip things that aren't reads or writes (they won't have anything
			    // to do with this request)
			    other_os->type == os->type &&
			    // Skip reads/writes which aren't part of the same request
			    other_os->data.rw.id == os->data.rw.id) {
				rs__cancel_outstanding(conn, other_os, error, cmd_rc);
			}
		}
		
		// If this read/write request is still in the request queue, remove it
		rs__req_t *req = rs__q_peek(conn->request_queue);
		if (req && req->type == os->type && req->data.rw.id == os->data.rw.id)
			rs__q_remove(conn->request_queue);
	}
	
	// We have possibly cleared an outstanding packet, attempt to queue a new
	// packet in its place
	rs__process_request_queue(conn);
}


void
rs__cancel_queued(rs_conn_t *conn, rs__req_t *req, int error)
{
	// Just raise the associated callback with an error status. The caller will
	// handle the removing of the request from the queue
	switch (req->type) {
		case RS__REQ_SCP_PACKET:
			req->data.scp_packet.cb(conn, error,
			                        0, 0, 0, 0, 0, req->data.scp_packet.data,
			                        req->cb_data);
			break;
		
		case RS__REQ_READ:
		case RS__REQ_WRITE:
			req->data.rw.cb(conn, error,
			                0, req->data.rw.orig_data,
			                req->cb_data);
			break;
	}
}
