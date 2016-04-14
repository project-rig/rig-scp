/**
 * SCP protocol constants and utilities.
 */

#ifndef RS__SCP_H
#define RS__SCP_H

#include <stdint.h>

#include <uv.h>

/**
 * Number of bytes in an SDP packet headder.
 */
#define RS__SDP_HEADER_LENGTH 8

/**
 * Number of bytes in an SCP packet headder with all three arguments
 */
#define RS__SCP_HEADER_LENGTH(n_args) (4 + (4 * (n_args)))

/**
 * Number of bytes consumed by a complete SCP packet wrapped in an SDP packet.
 */
#define RS__SIZEOF_SCP_PACKET(n_args, data_length) \
	( \
		RS__SDP_HEADER_LENGTH + \
		RS__SCP_HEADER_LENGTH((n_args)) + \
		(data_length) \
	)


/**
 * SCP cmd_rc numbers.
 */
typedef enum {
	RS__SCP_CMD_READ = 2,
	RS__SCP_CMD_WRITE = 3,
	RS__SCP_CMD_OK = 128,
} rs__scp_cmd_rc_t;


/**
 * Legal type values for an SCP CMD_READ/CMD_WRITE packet.
 */
typedef enum {
	RS__RW_TYPE_BYTE = 0,
	RS__RW_TYPE_SHORT = 1,
	RS__RW_TYPE_WORD = 2,
} rs__scp_rw_type_t;


/**
 * Given an address and read/write length, select the appropriate read/write
 * type.
 *
 * Chooses the largest unit size suitable for the job.
 */
rs__scp_rw_type_t rs__scp_rw_type(uint32_t address, uint32_t length);


/**
 * Pack an SCP packet into a buffer.
 *
 * @param buf The buffer to write the packet to. Sets the length field.
 * @param scp_data_length The maximum length of data field (when all three
 *                        arguments are present). Packets will be truncated
 *                        accordingly.
 * @param dest_addr The chip to send the packet to (x<<8 | y).
 * @param dest_cpu The core to send the packet to.
 * @param cmd_rc The SCP command/response code.
 * @param seq_num The sequence number of the packet
 * @param n_args The number of arguments to send (0-3).
 * @param arg1 Argument 1
 * @param arg2 Argument 2
 * @param arg3 Argument 3
 * @param data The data payload to place in the packet.
 */
void rs__pack_scp_packet(uv_buf_t *buf,
                         size_t scp_data_length,
                         uint16_t dest_addr,
                         uint8_t dest_cpu,
                         uint16_t cmd_rc,
                         uint16_t seq_num,
                         unsigned int n_args,
                         uint32_t arg1,
                         uint32_t arg2,
                         uint32_t arg3,
                         uv_buf_t data);


/**
 * Unpack the sequence number from an SCP packet in a buffer.
 *
 * Warning: Does not check that the buffer is long enough! It is the caller's
 * responsibility to check that the packet is long enough to be an SCP packet.
 *
 * @param buf The buffer containing the packet.
 * @returns The sequeunce number in the packet.
 */
uint16_t rs__unpack_scp_packet_seq_num(uv_buf_t buf);


/**
 * Unpack an SCP packet from a buffer.
 *
 * Warning: It is the caller's responsibility to check that the packet is at
 * least long enough to be an SCP packet with no arguments and no payload (i.e.
 * buf.len >= RS__SIZEOF_SCP_PACKET(0, 0)).
 *
 * @param buf The buffer containing the packet.
 * @param cmd_rc The SCP command/response code.
 * @param seq_num The sequence number of the packet
 * @param n_args Input: the ideal number of arguments to unpack, output: the
 *               number of arguments actually unpacked (may be less if the input
 *               packet is too short).
 * @param arg1 Argument 1
 * @param arg2 Argument 2
 * @param arg3 Argument 3
 * @param data A buffer whose base and length will be set according to the size
 *             of the payload, pointing within the supplied buffer.
 */
void rs__unpack_scp_packet(uv_buf_t buf,
                           uint16_t *cmd_rc,
                           uint16_t *seq_num,
                           unsigned int *n_args,
                           uint32_t *arg1,
                           uint32_t *arg2,
                           uint32_t *arg3,
                           uv_buf_t *data);


#endif
