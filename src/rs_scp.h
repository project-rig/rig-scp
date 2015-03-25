/**
 * SCP protocol constants.
 */

#ifndef RS_SCP_H
#define RS_SCP_H

/**
 * Legal type values for an SCP CMD_READ/CMD_WRITE packet.
 */
typedef enum {
	RS_RW_TYPE_BYTE = 0,
	RS_RW_TYPE_SHORT = 1,
	RS_RW_TYPE_WORD = 2,
} rs_scp_rw_type_t;


#endif
