/**
 * Rig SCP: A high performance implementation of the SpiNNaker Command Protocol
 * (SCP) transport mechanism.
 */

#ifndef RS_H
#define RS_H

#include <sys/socket.h>

#include <stdint.h>
#include <stdbool.h>

#include <uv.h>


struct rs_conn;
/**
 * Holds the state associated with an SCP connection to a single IP address.
 */
typedef struct rs_conn rs_conn_t;


/**
 * Callback function type for rs_send_scp commands.
 *
 * @param conn The SCP connection through which the packet was sent.
 * @param error 0 if a response was successfully received.  Note that the user
 *              is responsible for checking cmd_rc to check if the return code
 *              indicates an error occurred. If non-zero, all packet related
 *              fields have undefined values and the data buffer will contain
 *              undefined data. The rs_err_name and rs_strerror functions can be
 *              used to yield a human-readable error message. Negative errors
 *              correspond with libuv errors, positive errors with Rig SCP.
 * @param cmd_rc The command return code received in response to an SCP packet.
 * @param n_args The number of arguments decoded (as indicated when sending the
 *               packet). Arguments not decoded will have undefined values.
 * @param arg1 Argument 1
 * @param arg2 Argument 2
 * @param arg3 Argument 3
 * @param data Data (and its length) included with the response. This buffer may
 *             be safely freed/reused as of this callback's arrival.
 * @param cb_data The pointer supplied when registering the callback.
 */
typedef void (*rs_send_scp_cb)(rs_conn_t *conn,
                               int error,
                               uint16_t cmd_rc,
                               unsigned int n_args,
                               uint32_t arg1,
                               uint32_t arg2,
                               uint32_t arg3,
                               uv_buf_t data,
                               void *cb_data);


/**
 * Callback function type for rs_read/rs_write command completion.
 *
 * @param conn The SCP connection through which the packet was sent.
 * @param error 0 if a response was successfully received. If non-zero, the read
 *              data buffer's contents will be undefined. The rs_err_name and
 *              rs_strerror functions can be used to yield a human-readable
 *              error message. Negative errors correspond with libuv errors,
 *              positive errors with Rig SCP.
 * @param cmd_rc If error is RS_EBAD_RC, this field will be the cmd_rc returned
 *               in the first bad reply to arrive. If error is 0, the value of
 *               this argument is undefined.
 * @param data The buffer containing the read data or written data. This buffer
 *             may be safely freed/reused as of this callback's arrival.
 * @param cb_data The pointer supplied when registering the callback.
 */
typedef void (*rs_rw_cb)(rs_conn_t *conn,
                         int error,
                         uint16_t cmd_rc,
                         uv_buf_t data,
                         void *cb_data);


/**
 * Callback function type for rs_free completion.
 *
 * @param cb_data The pointer supplied when registering the callback.
 */
typedef void (*rs_free_cb)(void *cb_data);


/**
 * Allocate and initialise a new connection to an SCP endpoint.
 *
 * Returns NULL on failure.
 *
 * Note: to simplify implementation and work around shortcomings in the libuv
 * API, this library does not support the changing of the parameters supplied as
 * arguments. If these parameters need to be changed, the connection must be
 * closed (using rs_free) and a new connection made.
 *
 * @param loop The libuv event loop in which the connection will run.
 * @param addr The socket address of the remote machine.
 * @param scp_data_length The maximum length (in bytes) of the SCP data field.
 *                        This value should be chosen according to the target
 *                        devices' sver response.
 * @param n_tries Number of transmission attempts to make (including initial
 *                attempt) before giving up on a request. Must be at least 1.
 * @param n_outstanding Number of packets which may be simultaneously awaiting
 *                      responses.
 */
rs_conn_t *rs_init(uv_loop_t *loop,
                   const struct sockaddr *addr,
                   size_t scp_data_length,
                   unsigned int n_tries,
                   unsigned int n_outstanding);

/**
 * Queue up an SCP packet to be sent via an SCP connection.
 *
 * @param conn The connection to send the packet via.
 * @param dest_addr The address of the chip to send the packet to.
 * @param dest_cpu The CPU number to send the packet to.
 * @param cmd_rc The SCP command to send.
 * @param n_args_send The number of arguments to send (0-3).
 * @param n_args_recv The number of arguments to recieve (0-3) in the response.
 * @param arg1 Argument 1
 * @param arg2 Argument 2
 * @param arg3 Argument 3
 * @param data The payload data (and its length) to send with the packet. Must
 *             be allocated by the sender and remain valid until the callback
 *             function is called. When the response/acknowledge to the packet
 *             is returned, the data sent with it will be written to the same
 *             buffer supplied and the length field updated accordingly. If the
 *             data supplied is longer than the scp_data_length supplied during
 *             initialisation (less the space saved by using unused argument
 *             fields) the data will be silently truncated.
 * @param data_max_len The maximum length of the data buffer. If the received
 *                     response is longer than this value (or the
 *                     scp_data_length value provided during initialisation,
 *                     less the space saved by using unused argument fields) the
 *                     data will be silently truncated.
 * @param timeout Number of milliseconds to wait for a response from the
 *                machine before retransmitting.
 * @param cb A callback function which will be called when a response is
 *           returned.
 * @param cb_data User-supplied data that will be passed to the callback
 *                function.
 * @returns 0 if successfuly queued, non-zero otherwise.
 */
int rs_send_scp(rs_conn_t *conn,
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
                uint64_t timeout,
                rs_send_scp_cb cb,
                void *cb_data);

/**
 * Write a large block of data to a machine using SCP CMD_WRITE packets.
 *
 * @param conn The connection to send the request down.
 * @param dest_addr The address of the chip to send the packet to.
 * @param dest_cpu The CPU number to send the packet to.
 * @param addr The address to write the data to.
 * @param data The data to write to the machine. Must remain valid until the
 *             callback function is called.
 * @param timeout Number of milliseconds to wait for a response from the
 *                machine before retransmitting.
 * @param cb A callback function which will be called when the write completes.
 * @param cb_data User-supplied data that will be passed to the callback
 *                function.
 * @returns 0 if successfully queued, non-zero otherwise.
 */
int rs_write(rs_conn_t *conn,
             uint16_t dest_addr,
             uint8_t dest_cpu,
             uint32_t address,
             uv_buf_t data,
             uint64_t timeout,
             rs_rw_cb cb,
             void *cb_data);

/**
 * Read a large block of data from a machine using SCP CMD_READ packets.
 *
 * @param conn The connection to send the packet via.
 * @param dest_addr The address of the chip to send the packet to.
 * @param dest_cpu The CPU number to send the packet to.
 * @param addr The address to write the data to.
 * @param data A data buffer whose length will be used to determine the amount
 *             of data to read back. Must remain valid until the callback
 *             function is called.
 * @param timeout Number of milliseconds to wait for a response from the
 *                machine before retransmitting.
 * @param cb A callback function which will be called once the read is complete.
 * @param cb_data User-supplied data that will be passed to the callback
 *                function.
 * @returns 0 if successfully queued, non-zero otherwise.
 */
int rs_read(rs_conn_t *conn,
            uint16_t dest_addr,
            uint8_t dest_cpu,
            uint32_t address,
            uv_buf_t data,
            uint64_t timeout,
            rs_rw_cb cb,
            void *cb_data);

/**
 * Free any resources used by an SCP connection.
 *
 * This command cancels any incomplete requests immediately with an error
 * RS_EFREE. Note that this command does not complete immediately and instead
 * relies on the libuv event loop running for a short time.
 *
 * @param conn The connection to free. This should be considered invalid
 *             immediately after calling this function.
 * @param cb A callback to call when the free operation is complete or NULL if
 *           no callback is required. Warning: this is the only function in this
 *           library which accepts a NULL callback!
 * @param cb_data A user-defined pointer to be passed to the callback function.
 */
void rs_free(rs_conn_t *conn, rs_free_cb cb, void *cb_data);


/**
 * Error number returned when a read or write command receives a bad response
 * code.
 */
#define RS_EBAD_RC 1


/**
 * Error number returned when an SCP command has timed out n_tries times.
 */
#define RS_ETIMEOUT 2


/**
 * Error number returned when rs_free has been called.
 */
#define RS_EFREE 3


/**
 * Returns the error message for the given error code.
 */
const char *rs_strerror(int err);


/**
 * Returns the error name for the given error code.
 */
const char *rs_err_name(int err);

#endif
