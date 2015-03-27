/**
 * Test SCP utility functions.
 */

#include <check.h>

#include <string.h>

#include "tests.h"

#include "rs__scp.h"

START_TEST (test_sizeof_scp_packet)
{
	// Just the bare essential headders (no args, no payload)
	ck_assert_int_eq(RS__SIZEOF_SCP_PACKET(0, 0), 8 + 4);
	
	// No payload
	ck_assert_int_eq(RS__SIZEOF_SCP_PACKET(3, 0), 8 + 16);
	
	// With payload
	ck_assert_int_eq(RS__SIZEOF_SCP_PACKET(3, 128), 8 + 16 + 128);
	
	// With payload, not all args
	ck_assert_int_eq(RS__SIZEOF_SCP_PACKET(1, 128), 8 + 8 + 128);
}
END_TEST


START_TEST (test_scp_rw_type)
{
	// Length implies bytes
	ck_assert_int_eq(rs__scp_rw_type(0, 1), RS__RW_TYPE_BYTE);
	ck_assert_int_eq(rs__scp_rw_type(0, 5), RS__RW_TYPE_BYTE);
	// Address implies bytes
	ck_assert_int_eq(rs__scp_rw_type(1, 1), RS__RW_TYPE_BYTE);
	ck_assert_int_eq(rs__scp_rw_type(1, 2), RS__RW_TYPE_BYTE);
	ck_assert_int_eq(rs__scp_rw_type(1, 4), RS__RW_TYPE_BYTE);
	ck_assert_int_eq(rs__scp_rw_type(5, 1), RS__RW_TYPE_BYTE);
	ck_assert_int_eq(rs__scp_rw_type(5, 2), RS__RW_TYPE_BYTE);
	ck_assert_int_eq(rs__scp_rw_type(5, 4), RS__RW_TYPE_BYTE);
	
	// Length implies shorts
	ck_assert_int_eq(rs__scp_rw_type(0, 2), RS__RW_TYPE_SHORT);
	ck_assert_int_eq(rs__scp_rw_type(0, 6), RS__RW_TYPE_SHORT);
	// Address implies shorts
	ck_assert_int_eq(rs__scp_rw_type(2, 2), RS__RW_TYPE_SHORT);
	ck_assert_int_eq(rs__scp_rw_type(2, 4), RS__RW_TYPE_SHORT);
	ck_assert_int_eq(rs__scp_rw_type(6, 2), RS__RW_TYPE_SHORT);
	ck_assert_int_eq(rs__scp_rw_type(6, 4), RS__RW_TYPE_SHORT);
	
	// Length implies words
	ck_assert_int_eq(rs__scp_rw_type(0, 4), RS__RW_TYPE_WORD);
	ck_assert_int_eq(rs__scp_rw_type(0, 8), RS__RW_TYPE_WORD);
	// Address implies words
	ck_assert_int_eq(rs__scp_rw_type(4, 4), RS__RW_TYPE_WORD);
	ck_assert_int_eq(rs__scp_rw_type(4, 8), RS__RW_TYPE_WORD);
	ck_assert_int_eq(rs__scp_rw_type(8, 4), RS__RW_TYPE_WORD);
	ck_assert_int_eq(rs__scp_rw_type(8, 8), RS__RW_TYPE_WORD);
}
END_TEST


// An SCP packet with no arguments and no data:
//   flags: 0x87
//   tag: 0xFF
//   dest_port: 0
//   dest_cpu: 7 (max 5 bits)
//   src_port: 7
//   src_cpu: 0x1F
//   dest_addr: 0xA55A
//   srce_addr: 0x0000
//   cmd_rc: 0xDEAD
//   seq_num: 0xBEEF
const char *packet_no_arg_no_data = "\x87\xff\x07\xff\x5a\xa5\x00\x00\xad\xde\xef\xbe";
const size_t packet_no_arg_no_data_len = 12;


// An SCP packet with some arguments and data, depending how many arguments are
// interpreted.
//   flags: 0x87
//   tag: 0xFF
//   dest_port: 0
//   dest_cpu: 7 (max 5 bits)
//   src_port: 7
//   src_cpu: 0x1F
//   dest_addr: 0xA55A
//   srce_addr: 0x0000
//   cmd_rc: 0xDEAD
//   seq_num: 0xBEEF
//   arg1: 0x11213141
//   arg2: 0x12223242
//   arg3: 0x13233343
//   data: 0x12345678
const char *packet = "\x87\xff\x07\xff\x5a\xa5\x00\x00\xad\xde\xef\xbe"
                     "\x41\x31\x21\x11"   // Arg 1
                     "\x42\x32\x22\x12"   // Arg 2
                     "\x43\x33\x23\x13"   // Arg 3
                     "\x78\x56\x34\x12";  // Data
const size_t packet_len = 28;

START_TEST (test_unpack_scp_packet)
{
	// Create a buffer large enough for the largest packet
	char *buf_data[packet_len];
	uv_buf_t buf;
	
	// Grab the no-args test example and put it in a buffer
	memcpy(buf_data, packet_no_arg_no_data, packet_no_arg_no_data_len);
	buf.base = (void *)buf_data;
	buf.len = packet_no_arg_no_data_len;
	
	// Ensure that all fields are unpacked correctly
	uint16_t cmd_rc = 0;
	uint16_t seq_num = 0;
	uv_buf_t data;
	rs__unpack_scp_packet(buf,
	                      &cmd_rc, &seq_num,
	                      0, NULL, NULL, NULL,
	                      &data);
	ck_assert_uint_eq(cmd_rc, 0xDEAD);
	ck_assert_uint_eq(seq_num, 0xBEEF);
	ck_assert_uint_eq(data.len, 0);
	
	// Ensure the data didn't get modified
	ck_assert(memcmp(buf_data, packet_no_arg_no_data, packet_no_arg_no_data_len)
	          == 0);
	
	// Grab the with args test example and put it in a buffer
	memcpy(buf_data, packet, packet_len);
	buf.base = (void *)buf_data;
	buf.len = packet_len;
	
	// Ensure that all fields are unpacked correctly
	cmd_rc = 0;
	seq_num = 0;
	uint32_t arg1 = 0;
	uint32_t arg2 = 0;
	uint32_t arg3 = 0;
	rs__unpack_scp_packet(buf,
	                      &cmd_rc, &seq_num,
	                      3, &arg1, &arg2, &arg3,
	                      &data);
	ck_assert_uint_eq(cmd_rc, 0xDEAD);
	ck_assert_uint_eq(seq_num, 0xBEEF);
	ck_assert_uint_eq(arg1, 0x11213141);
	ck_assert_uint_eq(arg2, 0x12223242);
	ck_assert_uint_eq(arg3, 0x13233343);
	ck_assert_uint_eq(data.len, 4);
	// Ensure the data is correct
	ck_assert(memcmp(data.base, packet + 24, data.len)
	          == 0);
	
	// Ensure the data didn't get modified
	ck_assert(memcmp(buf_data, packet, packet_len) == 0);
	
	// Ensure that all fields are unpacked correctly when a different number of
	// arguments is supplied.
	cmd_rc = 0;
	seq_num = 0;
	arg1 = 0;
	arg2 = 0;
	arg3 = 0;
	rs__unpack_scp_packet(buf,
	                      &cmd_rc, &seq_num,
	                      2, &arg1, &arg2, NULL,
	                      &data);
	ck_assert_uint_eq(cmd_rc, 0xDEAD);
	ck_assert_uint_eq(seq_num, 0xBEEF);
	ck_assert_uint_eq(arg1, 0x11213141);
	ck_assert_uint_eq(arg2, 0x12223242);
	ck_assert_uint_eq(data.len, 8);
	// Ensure the data is correct
	ck_assert(memcmp(data.base, packet + 20, data.len)
	          == 0);
	
	// Ensure the data didn't get modified
	ck_assert(memcmp(buf_data, packet, packet_len) == 0);
}
END_TEST


START_TEST (test_unpack_scp_packet_seq_num)
{
	// Create a buffer large enough for the packet
	char *buf_data[packet_no_arg_no_data_len];
	uv_buf_t buf;
	
	// Grab the no-args test example and put it in a buffer
	memcpy(buf_data, packet_no_arg_no_data, packet_no_arg_no_data_len);
	buf.base = (void *)buf_data;
	buf.len = packet_no_arg_no_data_len;
	
	// Ensure that all fields are unpacked correctly
	ck_assert_uint_eq(rs__unpack_scp_packet_seq_num(buf), 0xBEEF);
	
	// Ensure the data didn't get modified
	ck_assert(memcmp(buf_data, packet_no_arg_no_data, packet_no_arg_no_data_len)
	          == 0);
}
END_TEST


START_TEST (test_pack_scp_packet)
{
	size_t scp_data_length;
	unsigned int n_args;
	int with_data;
	
	// Create a buffer large enough for the largest packet
	char *buf_data[packet_len];
	uv_buf_t buf;
	buf.base = (void *)buf_data;
	buf.len = 0;
	
	// Field that specifies the data to pack with the packet
	uv_buf_t data;
	
	// Ensure that all fields are packed correctly with no arguments and no
	// payload, no matter the data field length limit is
	data.len = 0;
	for (scp_data_length = 0; scp_data_length < 4; scp_data_length++) {
		rs__pack_scp_packet(&buf,
		                    scp_data_length,
		                    0xA55A, 7,
		                    0xDEAD, 0xBEEF,
		                    0, 0, 0, 0,
		                    data);
		ck_assert_uint_eq(buf.len, packet_no_arg_no_data_len);
		ck_assert(memcmp(buf.base,
		                 packet_no_arg_no_data,
		                 buf.len) == 0);
	}
	
	// Ensure that all fields are packed correctly with arguments and with and
	// without a payload. Also checks that when the data length limit is in place,
	// the payload is only truncated after it has used up the argument space.
	data.len = 0;
	for (scp_data_length = 0; scp_data_length < 4; scp_data_length++) {
		for (n_args = 0; n_args <= 3; n_args++) {
			for (with_data = 0; with_data < 2; with_data++) {
				// Creates the maxmimum length of data possible (regardless of
				// scp_data_length).
				data.base = (void *)(packet + 12 + (4 * n_args));
				data.len = with_data ? (4 * (3 - n_args)) + 4 : 0;
				rs__pack_scp_packet(&buf,
				                    scp_data_length,
				                    0xA55A, 7,
				                    0xDEAD, 0xBEEF,
				                    n_args, 0x11213141, 0x12223242, 0x13233343,
				                    data);
				ck_assert_uint_eq(buf.len,
				                  // Up to the full length of the packet
				                  packet_len
				                  // Less the missing arguments
				                  - ((3 - n_args) * 4)
				                                 // With a payload, the packet should
				                                 // fill up to the maximum allowed.
				                  - (with_data ? (4 - scp_data_length)
				                                 // Without a payload, just includes the
				                                 // arguments specified
				                               : 4
				                    )
				                  );
				ck_assert(memcmp(buf.base,
				                 packet,
				                 buf.len) == 0);
			}
		}
	}
}
END_TEST


Suite *
make_scp_suite(void)
{
	Suite *s = suite_create("scp");
	
	// Add tests to the test case
	TCase *tc_core = tcase_create("Core");
	tcase_add_test(tc_core, test_sizeof_scp_packet);
	tcase_add_test(tc_core, test_scp_rw_type);
	tcase_add_test(tc_core, test_unpack_scp_packet);
	tcase_add_test(tc_core, test_unpack_scp_packet_seq_num);
	tcase_add_test(tc_core, test_pack_scp_packet);
	
	// Add each test case to the suite
	suite_add_tcase(s, tc_core);
	
	return s;
}

