/**
 * A simple example program demonstrating the Rig SCP library API.
 *
 * This program is written in a tutorial style and is designed to (hopefully)
 * make sense when read through in order. The program simply sends out a handful
 * of CMD_VER (a.k.a. sver) SCP commands and prints the response and
 * demonstrates reading and writing of data from a machine.
 *
 * Once compiled with `make hello`, the usage is like so:
 *
 *     ./hello hostname scp_data_length n_outstanding
 *
 * * hostname -- the SpiNNaker machine to communicate with. The machine should
 *               be already booted and not be running any applications.
 * * scp_data_length -- the maximum data field length supported by the machine
 *                      (in bytes), typically 256.
 * * n_outstanding -- the number of simultaneous commands which Rig SCP may
 *                    issue to the machine at once, typically between 1 and 8.
 *
 * Note: In general, one should query the machine to determine the appropriate
 * values for scp_data_length and n_outstanding.
 */

#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

// Rig SCP uses libuv to drive its asynchronous comms. In particular, Rig SCP
// uses the libuv event loop which we'll see later. Don't worry, you don't
// really need to know anything about libuv to use Rig SCP!
#include <uv.h>

// This is the one and only Rig SCP headder file. You should take a look inside
// it for a complete API reference.
#include <rs.h>


// Timeout (msec) for transmission attempts made by Rig SCP
#define TIMEOUT 500

// Number of transmission attempts before Rig SCP gives up
#define N_TRIES 5

// Number of cores to send the CMD_VER command to.
#define N_CPUS 16

// Amount of data to read/write (in bytes) in this example program.
#define DATA_LEN 10 * 1024 * 1024

// An address in SpiNNaker to perform read/write operations on. In this example,
// the start of the 'User SDRAM' block (sv->sdram_base). Note, in real-world use
// you should allocate memory using SC&MP/SARK rather than using a fixed
// address.
#define TEST_ADDRESS 0x60240000


// The destination chip for all commands sent by this example program.
#define DEST_CHIP_X 0
#define DEST_CHIP_Y 0
#define DEST_CHIP ((DEST_CHIP_X) << 8 | (DEST_CHIP_Y))


// A global pointer to the libuv event loop which we'll set and use later on...
static uv_loop_t *loop;

// The "struct addrinfo" which will be set to the address/port of the SpiNNaker
// system we provide.
static struct addrinfo *addrinfo;

// A reference to our connection to the machine.
rs_conn_t *conn;

// Callback function prototypes which will be called when our example commands
// complete. Libuv (and thus Rig SCP) is event based: every command returns at
// some point in the future by calling a callback function you provide.
void cmd_ver_callback(rs_conn_t *conn,
                      int error,
                      uint16_t cmd_rc,
                      unsigned int n_args,
                      uint32_t arg1,
                      uint32_t arg2,
                      uint32_t arg3,
                      uv_buf_t data,
                      void *cb_data);
void read_callback(rs_conn_t *conn,
                   int error,
                   uint16_t cmd_rc,
                   uv_buf_t data,
                   void *cb_data);
void write_callback(rs_conn_t *conn,
                    int error,
                    uint16_t cmd_rc,
                    uv_buf_t data,
                    void *cb_data);
void conn_freed_callback(void *cb_data);

// Flag indicating we've received a response to the CMD_VER command from each
// core. (We use this to determine when all the CMD_VER commands have finished.)
bool got_cmd_ver_response[N_CPUS];


// A timestamp (in msec) we'll use this to time how long each operation takes in
// our example.
uint64_t last_time;


// Buffers to store the data we write/read to/from the machine.
unsigned char write_data[DATA_LEN];
unsigned char read_data[DATA_LEN];


int
main(int argc, char *argv[])
{
	// First we'll parse the command line arguments
	if (argc != 4) {
		fprintf(stderr, "Expected 3 arguments: "
		                "hostname scp_data_length n_outstanding\n");
		return -1;
	}
	const char *hostname = argv[1];
	const size_t scp_data_length = (size_t)atoi(argv[2]);
	const unsigned int n_outstanding = (unsigned int)atoi(argv[3]);
	
	// Get a reference to the libuv event loop; we'll use this later!
	loop = uv_default_loop();
	assert(loop);
	
	// We must convert the hostname into a "struct addrinfo" as is conventional
	// for the POSIX socket API. In order to do this we must make a DNS query to
	// resolve the IP address of the given host. Note: libuv provides an
	// asynchronous version of getaddrinfo (uv_getaddrinfo) but we don't use it in
	// this example for simplicity's sake.
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;  // SpiNNaker only supports IPv4
	hints.ai_socktype = SOCK_DGRAM; // SCP is a datagram based protocol
	int success = getaddrinfo(hostname, "17893", // The SCP port number
                            &hints, &addrinfo);
	assert(success == 0);
	
	// Now lets initialise the Rig SCP connection. You need one of these for every
	// physical connection to the machine available. In this case we'll just make
	// one. Also, note that all connection parameters are set at connection time
	// and cannot be changed: you must disconnect and recreate the connection with
	// new parameters if you wish to change them later.
	conn = rs_init(loop,
	               addrinfo->ai_addr,
	               scp_data_length,
	               TIMEOUT,
	               N_TRIES,
	               n_outstanding);
	assert(conn);
	
	// Start timing...
	last_time = uv_now(loop);
	
	// Now we'll send out an CMD_VER command to each of the N_CPUS CPUs in
	// parallel.
	printf("Sending CMD_VER to %d CPUs...\n", N_CPUS);
	unsigned int i;
	for (i = 0; i < N_CPUS; i++) {
		got_cmd_ver_response[i] = false;
		
		// We must allocate a buffer to store the response data from the SCP command
		// which we specify as a uv_buf_t (as is the convention in libuv) which has
		// two fields: base and len. The base is a pointer to the start of the
		// buffer and len is used to indicate the length of the useful data within
		// it). We'll allocate rs_send_scp bytes which should be large enough to
		// accept the CMD_VER response. To indicate that there is no data to be sent
		// with our CMD_VER command we initially set data.len to 0.
		uv_buf_t data;
		data.base = malloc(scp_data_length);
		assert(data.base);
		data.len = 0;
		
		// The following function actually queues up the packet to be sent and
		// registers a callback, cmd_ver_callback, for when the response comes back.
		// The last callback to complete will trigger the next part of the example
		// program: bulk read/write operations.
		rs_send_scp(conn,
		            DEST_CHIP,
		            i, // CPU i
		            0, // cmd_rc: CMD_VER
		            3, // Must provide three arguments for CMD_VER (though their
		               // value is unimportant)
		            3, // All three arguments are expected in the response
		            0, 0, 0, // Args1-3 just set arbitrarily.
		            data, // No data to be sent but we'll get the response data here
		            scp_data_length, // Maximum length of response
		            cmd_ver_callback, // Callback on completion.
		            &(got_cmd_ver_response[i]));
	}
	
	// The rest of this program's activity will be purely event based so we just
	// start the libuv event loop. The event loop will be terminated by a call to
	// uv_stop in our final callback handler, returning control back here.
	uv_run(loop, UV_RUN_DEFAULT);
	
	// Finally we need to close the connection and free all the resources used by
	// the connection. Even this function is asynchronous(!) and has a callback
	// when it is complete.
	rs_free(conn, conn_freed_callback, NULL);
	
	// Due to the slightly awkward way libuv works, the free command actually
	// requires the libuv event loop to run for a short time... Once the
	// connection has been freed our callback will once again call uv_stop and the
	// event loop will terminate.
	uv_run(loop, UV_RUN_DEFAULT);
	
	return 0;
}


/**
 * Callback for rs_free call at the end of the program.
 */
void
conn_freed_callback(void *cb_data)
{
	printf("Connection freed!\n");
	uv_stop(loop);
}



/**
 * Callback function called when each CMD_VER returns.
 */
void
cmd_ver_callback(rs_conn_t *conn,
                 int error,
                 uint16_t cmd_rc,
                 unsigned int n_args,
                 uint32_t arg1,
                 uint32_t arg2,
                 uint32_t arg3,
                 uv_buf_t data,
                 void *cb_data)
{
	// Make sure we got the correct reply. Note that we must do all this by hand:
	// Rig SCP does not provide support for generating/consuming SCP commands.
	if (error) {
		// The rs_strerror function maps error numbers to human-readable strings.
		// Negative error numbers correspond with libuv error codes (usually network
		// related errors) while positive error numbers correspond with Rig SCP
		// errors (e.g. timeouts).
		printf("ERROR: %s\n", rs_strerror(error));
		abort();
	}
	if (cmd_rc != 128) {
		// The CMD_VER command expects "RC_OK" (128) in response.
		printf("ERROR: Unexpected return code for CMD_VER %u\n", cmd_rc);
		abort();
	}
	if (n_args != 3) {
		printf("ERROR: Expect 3 arguments in response to CMD_VER\n");
		abort();
	}
	if (data.len <= 0) {
		printf("ERROR: Expect a null terminated string in response to CMD_VER.\n");
		abort();
	}
	
	// Unpack the version information and print it out. Notice that we might not
	// get all our responses back in the same order we sent them if n_outstanding
	// is greater than 1!
	unsigned int x = (arg1 >> 24) & 0xFF;
	unsigned int y = (arg1 >> 16) & 0xFF;
	unsigned int cpu_num = (arg1 >> 0) & 0xFF;
	const char *vers_string = data.base;
	double vers_num = (double)((arg2 >> 16) & 0xFFFF) / 100.0;
	printf("Got response from (%u, %u, %2u) with software '%s' v%1.2f.\n",
	       x, y, cpu_num, vers_string, vers_num);
	
	// Free the buffer we used for the response data
	free(data.base);
	
	// Get the pointer to the callback response flag we provided as user data and
	// indicate we've got a response.
	bool *my_got_cmd_ver = (bool *)cb_data;
	*my_got_cmd_ver = true;
	
	// If all responses have been received, start the write operation
	bool got_all_replies = true;
	int i;
	for (i = 0; i < N_CPUS; i++)
		if (!got_cmd_ver_response[i])
			got_all_replies = false;
	
	if (got_all_replies) {
		printf("All responses received after %0.0f ms.\n\n",
		       (double)(uv_now(loop) - last_time));
		
		// Generate some random data to write and set up a uv_buf_t as before, this
		// time we set the len field to indicate how much data in the buffer is to
		// be written.
		for (i = 0; i < DATA_LEN; i++)
			write_data[i] = rand();
		uv_buf_t data;
		data.base = (void *)write_data;
		data.len = DATA_LEN;
		
		printf("Writing %u bytes of random data to 0x%08X...\n",
		       DATA_LEN, TEST_ADDRESS);
		
		// Start timing again...
		last_time = uv_now(loop);
		
		// Now lets actually queue up the write, setting up a callback for when the
		// write completes.
		rs_write(conn,
		         DEST_CHIP,
		         0, // Write to CPU 0's memory
		         TEST_ADDRESS,
		         data,
		         write_callback, // Callback when the write completes.
		         NULL);
	}
}


/**
 * Callback function called when the write completes.
 */
void
write_callback(rs_conn_t *conn,
               int error,
               uint16_t cmd_rc,
               uv_buf_t data,
               void *cb_data)
{
	// Make sure nothing went wrong.
	if (error) {
		printf("ERROR: %s\n", rs_strerror(error));
		// In the case of read/write operations, the error RS_EBAD_RC is returned
		// if the machine returns anything but "RC_OK" in response to a read/write
		// command. In this special case, the return code produced is placed in
		// cmd_rc.
		if (error == RS_EBAD_RC)
			printf("(cmd_rc = %d)\n", cmd_rc);
		abort();
	}
	
	// Set up a buffer into which the data will be read from the machine. This
	// time the len field indicates how much data to read.
	uv_buf_t r_data;
	r_data.base = (void *)read_data;
	r_data.len = DATA_LEN;
	
	double duration = uv_now(loop) - last_time;
	printf("Write completed in %0.0f ms! Throughput = %0.3f Mbit/s.\n\n",
	       duration, ((DATA_LEN * 8.0) / (duration / 1000.0) / 1024.0 / 1024.0));
	
	// Re-start timing...
	last_time = uv_now(loop);
	
	printf("Reading back %u bytes from 0x%08X...\n",
	       DATA_LEN, TEST_ADDRESS);
	
	// Read back the data we just wrote; we'll check it matches in the callback
	// function.
	rs_read(conn,
	        DEST_CHIP, // Read from chip (0, 0)'s memory
	        0, // Read from CPU 0's memory
	        TEST_ADDRESS,
	        r_data,
	        read_callback, // Callback when the read completes.
	        NULL);
}


/**
 * Callback function called when the read completes.
 */
void
read_callback(rs_conn_t *conn,
              int error,
              uint16_t cmd_rc,
              uv_buf_t data,
              void *cb_data)
{
	// Make sure nothing went wrong.
	if (error) {
		printf("ERROR: %s\n", rs_strerror(error));
		if (error == RS_EBAD_RC)
			printf("(cmd_rc = %d)\n", cmd_rc);
		abort();
	}
	
	double duration = uv_now(loop) - last_time;
	printf("Read completed in %0.0f ms! Throughput = %0.3f Mbit/s.\n\n",
	       duration, ((DATA_LEN * 8.0) / (duration / 1000.0) / 1024.0 / 1024.0));
	
	// Check the read data matches what we wrote before
	if (memcmp(read_data, write_data, DATA_LEN) == 0)
		printf("The data read back matched the data written!\n\n");
	else
		printf("ERROR: The data read did not match the data written!\n\n");
	
	// And that's the end of this simple example! Stop the libuv event loop
	// bringing control back to the main function.
	uv_stop(loop);
}
