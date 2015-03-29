/**
 * SCP protocol utility functions.
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <uv.h>

#include <rs__internal.h>
#include <rs__scp.h>


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


rs__scp_rw_type_t
rs__scp_rw_type(uint32_t address, uint32_t length)
{
	if (address % 4 == 0 && length % 4 == 0)
		return RS__RW_TYPE_WORD;
	else if (address % 2 == 0 && length % 2 == 0)
		return RS__RW_TYPE_SHORT;
	else
		return RS__RW_TYPE_BYTE;
}


void
rs__pack_scp_packet(uv_buf_t *buf,
                    size_t scp_data_length,
                    uint16_t dest_addr,
                    uint8_t dest_cpu,
                    uint16_t cmd_rc,
                    uint16_t seq_num,
                    unsigned int n_args,
                    uint32_t arg1,
                    uint32_t arg2,
                    uint32_t arg3,
                    uv_buf_t data)
{
	// Set up the header
	sdp_scp_header_t header;
	
	// SDP header
	header.flags = 0x87;  // Always require a reply
	header.tag = 0xFF;
	header.dest_port_cpu = dest_cpu & 0x1F;  // Port zero
	header.srce_port_cpu = 0xFF;
	header.dest_addr = dest_addr;
	header.srce_addr = 0;  // (0, 0)
	
	// SCP header
	header.cmd_rc = cmd_rc;
	header.seq_num = seq_num;
	header.arg1 = arg1;
	header.arg2 = arg2;
	header.arg3 = arg3;
	
	// Copy the header into the buffer
	memcpy(buf->base, &header, sizeof(sdp_scp_header_t));
	
	// Truncate the payload
	data.len = MIN(data.len, scp_data_length);
	
	// Copy the truncated payload into the buffer
	memcpy(buf->base + RS__SIZEOF_SCP_PACKET(n_args, 0),
	       data.base, data.len);
	
	// Calculate the final length of the packet
	buf->len = sizeof(sdp_scp_header_t) + data.len - (4 * (3 - n_args));
}


uint16_t
rs__unpack_scp_packet_seq_num(uv_buf_t buf)
{
	sdp_scp_header_t *header = (sdp_scp_header_t *)buf.base;
	return header->seq_num;
}


void
rs__unpack_scp_packet(uv_buf_t buf,
                      uint16_t *cmd_rc,
                      uint16_t *seq_num,
                      unsigned int *n_args,
                      uint32_t *arg1,
                      uint32_t *arg2,
                      uint32_t *arg3,
                      uv_buf_t *data)
{
	sdp_scp_header_t *header = (sdp_scp_header_t *)buf.base;
	
	// Unpack basic SCP fields
	*cmd_rc = header->cmd_rc;
	*seq_num = header->seq_num;
	
	// Truncate n_args if the packet is too short
	if (buf.len <= RS__SIZEOF_SCP_PACKET(0, 0))
		*n_args = MIN(0, *n_args);
	else if (buf.len <= RS__SIZEOF_SCP_PACKET(1, 0))
		*n_args = MIN(1, *n_args);
	else if (buf.len <= RS__SIZEOF_SCP_PACKET(2, 0))
		*n_args = MIN(2, *n_args);
	else if (buf.len <= RS__SIZEOF_SCP_PACKET(3, 0))
		*n_args = MIN(3, *n_args);
	
	// Unpack arguments (if present)
	if (*n_args >= 1)
		*arg1 = header->arg1;
	if (*n_args >= 2)
		*arg2 = header->arg2;
	if (*n_args >= 3)
		*arg3 = header->arg3;
	
	// Setup the pointers to the data
	data->base = buf.base + RS__SIZEOF_SCP_PACKET(*n_args, 0);
	data->len = buf.len - RS__SIZEOF_SCP_PACKET(*n_args, 0);
}
