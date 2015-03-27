/**
 * Rig SCP internal types.
 */

#ifndef RS__INTERNAL_H
#define RS__INTERNAL_H

#include <uv.h>

#include <rs.h>
#include <rs__queue.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) < (b)) ? (a) : (b))
#endif


/**
 * Indicates the type of request.
 */
typedef enum {
	// Send a specific SCP packet.
	RS__REQ_SCP_PACKET,
	
	// Send a bulk read request
	RS__REQ_READ,
	
	// Send a bulk write request
	RS__REQ_WRITE,
} rs__req_type_t;


/**
 * Represents a request sent to a SpiNNaker machine which may be either a single
 * SCP packet or a bulk read/write.
 */
typedef struct {
	// Data required by the queue datastructure.
	rs__q_entry_t _;
	
	// What type of request is this?
	rs__req_type_t type;
	
	// The address of the processor this request is destined for (X<<8 | Y)
	uint16_t dest_addr;
	
	// The CPU number this request is destined for
	uint8_t dest_cpu;
	
	// The data supplied to be supplied to the callback on completion of this
	// request
	void *cb_data;
	
	// Type-specific request values
	union {
		// Data for SCP Packet requests
		struct {
			// Command to send
			uint16_t cmd_rc;
			
			// The number of arguments to be included in the request and to be
			// extracted from the response.
			unsigned int n_args_send;
			unsigned int n_args_recv;
			
			// Command arguments (some are undefined if a lower number of arguments is
			// specified).
			uint32_t arg1;
			uint32_t arg2;
			uint32_t arg3;
			
			// Buffer containing the request's data payload (and its length) and where
			// the response's data payload (and length) will be placed
			uv_buf_t data;
			
			// The maximum number of bytes of the buffer can hold. If a response
			// longer than this is received it will be truncated.
			size_t data_max_len;
			
			// Callback function on completion
			rs_send_scp_cb cb;
		} scp_packet;
		
		// Data for read/write requests
		struct {
			// A unique ID assigned to this read/write request.
			unsigned int id;
			
			// The address to read/write to. This is advanced as the read/write
			// process proceeds.
			uint32_t address;
			
			// Buffer containing the data to send/the location to write the response
			// data. The length of this buffer also indiciates the amount to
			// read/write. The base pointer and length are updated as the packet is
			// transmitted. See orig_data.
			uv_buf_t data;
			
			// A copy of the above whose pointers are not modified as the read/write
			// proceeds.
			uv_buf_t orig_data;
			
			// Callback function on completion
			rs_rw_cb cb;
		} rw;
	} data;
	
} rs__req_t;


/**
 * State used by an outstanding transmission request.
 */
typedef struct {
	// Pointer to the owning rs_conn_t, required since a pointer to this struct is
	// used as the user-data for a number of callbacks.
	rs_conn_t *conn;
	
	// Is this outstanding channel currently awaiting a response?
	bool active;
	
	// The type of request that is active
	rs__req_type_t type;
	
	// The sequence number allocated to the packet whose response is being awaited
	uint16_t seq_num;
	
	// The number of attempts made to transmit the current packet
	unsigned int n_tries;
	
	// The raw packet value and its length (to be used for retransmission).
	uv_buf_t packet;
	
	// The current UDP send request (or NULL if the send operation is complete)
	uv_udp_send_t send_req;
	
	// Is a UDP send request actually pending?
	bool send_req_active;
	
	// If this outstanding request is cancelled while send_req_active, this flag
	// indicates that the send_req callback should mark this outstanding channel
	// as inactive.
	bool cancelled;
	
	// The timeout timer handle
	uv_timer_t timer_handle;
	
	// Flag indicating that the timer handle has been closed (and thus freeing can
	// occur)
	bool timer_handle_closed;
	
	// The data supplied to be supplied to the callback on completion of this
	// request
	void *cb_data;
	
	// Request-type specific values
	union {
		// Data for SCP Packet requests
		struct {
			// The number of arguments to be extracted from the response.
			unsigned int n_args_recv;
			
			// Buffer containing the response's data payload (and length) will be
			// placed
			uv_buf_t data;
			
			// The maximum number of bytes of the buffer can hold. If a response
			// longer than this is received it will be truncated.
			size_t data_max_len;
			
			// Callback function on completion
			rs_send_scp_cb cb;
		} scp_packet;
		
		// Data for read/write requests
		struct {
			// The ID of this read/write request.
			unsigned int id;
			
			// Buffer containing the data to send/the location to write the response
			// data. The length of this buffer also indiciates the amount to
			// read/write. The base pointer and length are updated as the packet is
			// transmitted. See orig_data.
			uv_buf_t data;
			
			// A copy of the above whose pointers are not modified as the read/write
			// proceeds.
			uv_buf_t orig_data;
			
			// Callback function to call on completion/error.
			rs_rw_cb cb;
		} rw;
	} data;
	
} rs__outstanding_t;


struct rs_conn {
	// Maximum number of bytes in an SCP packet's data field
	size_t scp_data_length;
	
	// Number of msec to wait before retransmitting a packet
	uint64_t timeout;
	
	// Number of transmission attempts before giving up (including the initial
	// attempt)
	unsigned int n_tries;
	
	// Number of outstanding commands which can be in progress at any time
	unsigned int n_outstanding;
	
	// The libuv event loop this connection lives in
	uv_loop_t *loop;
	
	// The address to send requests to
	const struct sockaddr *addr;
	
	// The UDP connection handle used for this connection
	uv_udp_t udp_handle;
	
	// Flag indicating that the UDP connection handle has been closed (and thus
	// freeing can occur)
	bool udp_handle_closed;
	
	// Request queue containing rs__req_t entries representing SCP packets or bulk
	// reads/writes which have not yet been handled.
	rs__q_t *request_queue;
	
	// An array of n_outstanding outstanding packet transmission attempt states.
	rs__outstanding_t *outstanding;
	
	// Counter used to assign packet sequence numbers. Contains the next value to
	// be assigned.
	uint16_t next_seq_num;
	
	// Counter used to assign unique IDs to read/write requests. Contains the next
	// value to be assigned.
	unsigned int next_rw_id;
	
	// A flag which indicates that this structure should be freed as soon as
	// possible.
	bool free;
};


/**
 * If and outstanding channels are available, process commands from the queue.
 */
void rs__process_request_queue(rs_conn_t *conn);


/**
 * Attempt (re-)transmission of an outstanding packet.
 *
 * This function attemtps to transmit the current packet in the outstanding
 * channel provided unless the number of transmission attempts passes the limit
 * or transmission fails. If the transmission fails, the user's callback will be
 * called appropriately, the channel marked as inactive and the request
 * cancelled.
 *
 * Note: The outstanding channel must be active when calling this function.
 */
void rs__attempt_transmission(rs_conn_t *conn, rs__outstanding_t *os);


/**
 * Cancel the sending of a given outstanding request.
 *
 * Returns an error with the specified cmd_rc (should be -1 if the error is not
 * an SCP-reported one).
 *
 * If the request is a read or write, it also cancells all other associated
 * outstanding channels and removes the request from the request queue (if it is
 * still there).
 */
void rs__cancel_outstanding(rs_conn_t *conn, rs__outstanding_t *os,
                            uint16_t cmd_rc);


/**
 * Cancel a request from the request queue.
 *
 * This function takes a request removed from the request queue and sends an
 * error to the associated callback. Note: this command presumes that the
 * request is not outstanding (i.e. for read/write requests that any outstanding
 * part of the request has already been cancelled with rs__cancel_outstanding.
 */
void rs__cancel_queued(rs_conn_t *conn, rs__req_t *req);


/**
 * Used by rs__process_request_queue. Processes a single SCP packet request.
 *
 * Note: The caller should remove the request from the queue once this function
 * has returned.
 *
 * The request must be of type RS__REQ_SCP_PACKET and the outstanding channel
 * must be inactive.
 */
void rs__process_queued_scp_packet(rs_conn_t *conn,
                                   rs__req_t *req,
                                   rs__outstanding_t *os);


/**
 * Used by rs__process_request_queue. Processes a single read/write request.
 *
 * The request must be of type RS__REQ_READ or RS__REQ_WRITE and the outstanding
 * channel must be inactive.
 *
 * @returns true if this call transmitted the last packet required for this
 *          read/write and thus the request should be removed from the queue.
 */
bool rs__process_queued_rw(rs_conn_t *conn,
                           rs__req_t *req,
                           rs__outstanding_t *os);


/**
 * Callback function to allocate memory in advance of an SCP packet arriving.
 */
void rs__udp_recv_alloc_cb(uv_handle_t *handle,
                           size_t suggested_size, uv_buf_t *buf);


/**
 * Callback function when an SCP packet arrives.
 *
 * If an outstanding channel with a matching sequence number is found,
 * rs__process_response will be called with the response and the UDP data (which
 * will be freed as soon as rs__process_response returns).
 */
void rs__udp_recv_cb(uv_udp_t *handle,
                     ssize_t nread, const uv_buf_t *buf,
                     const struct sockaddr *addr,
                     unsigned int flags);


/**
 * Callback function when a timeout occurs on a packet.
 *
 * Simply attempts to retransmit the packet.
 */
void rs__timer_cb(uv_timer_t *handle);


/**
 * Callback function on uv_udp_send() completion.
 *
 * Start the packet timeout timer. Also includes cleanup-code.
 */
void rs__udp_send_cb(uv_udp_send_t *req, int status);


/**
 * Called by rs__udp_recv_cb. Process an incoming SCP response.
 */
void rs__process_response(rs_conn_t *conn, rs__outstanding_t *os,
                          uv_buf_t buf);


/**
 * Called by rs__process_response. Process an incoming SCP packet response.
 */
void rs__process_response_scp_packet(rs_conn_t *conn, rs__outstanding_t *os,
                                     uv_buf_t buf);


/**
 * Called by rs__process_response. Process an incoming read/write response.
 */
void rs__process_response_rw(rs_conn_t *conn, rs__outstanding_t *os,
                             uv_buf_t buf);


/**
 * Callback on closing the UDP handle.
 *
 * Simply used to attempt to complete the freeing process once this handle has
 * been closed.
 */
void rs__udp_handle_closed_cb(uv_handle_t *handle);


/**
 * Callback on closing a timer handle.
 *
 * Simply used to attempt to complete the freeing process once this handle has
 * been closed.
 */
void rs__timer_handle_closed_cb(uv_handle_t *handle);

#endif
