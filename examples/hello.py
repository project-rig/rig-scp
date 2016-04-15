#!/usr/bin/env python
"""
A simple example program demonstrating the Python wrapper around the Rig SCP
library.

Once the rig_c_scp Python library has been compiled and installed, usage is
like so:

    python hello.py hostname scp_data_length n_outstanding [x y]

* hostname -- the SpiNNaker machine to communicate with. The machine should
              be already booted and not be running any applications.
* scp_data_length -- the maximum data field length supported by the machine
                     (in bytes), typically 256.
* n_outstanding -- the number of simultaneous commands which Rig SCP may
                   issue to the machine at once, typically between 1 and 8.
* x y -- (optional) the chip coordinates to communicate with.

Note: In general, one should query the machine to determine the appropriate
values for scp_data_length and n_outstanding.
"""

import sys
import time
import os

from threading import Semaphore, Event

from rig_c_scp import CSCPConnection


# Number of cores to send the CMD_VER command to.
N_CPUS = 16

# Amount of data to read/write (in bytes) in this example program.
DATA_LEN = 10 * 1024 * 1024

# An address in SpiNNaker to perform read/write operations on. In this example,
# the start of the 'User SDRAM' block (sv->sdram_base). Note, in real-world use
# you should allocate memory using SC&MP/SARK rather than using a fixed
# address.
TEST_ADDRESS = 0x60240000


def print_version_response_packet(pkt):
    """Print out a summary of the contents of an VERSION command response
    packet.
    """
    # Check for new-style version
    assert ((pkt.arg2 >> 16) & 0xFFFF) == 0xFFFF
    
    # Split the software name and version string
    data = pkt.data.split(b"\0")
    
    # Print the information received
    print("Got response from ({x}, {y}, {p:2}) "
          "with software '{name}' v{version}.".format(
        x=(pkt.arg1 >> 24) & 0xFF,
        y=(pkt.arg1 >> 16) & 0xFF,
        p=(pkt.arg1 >> 0) & 0xFF,
        name=data[0].decode("utf-8"),
        version=data[1].decode("utf-8")))


def on_error(exc):
    """A generic on-error callback which prints the error and exits."""
    print(exc)
    sys.exit(1)


if __name__ == "__main__":
    # Determine the hostname/connection parameters
    hostname = sys.argv[1]
    scp_data_length = int(sys.argv[2])
    n_outstanding = int(sys.argv[3])
    
    # Determine the chip we're supposed to talk to
    if len(sys.argv) >= 6:
        x = int(sys.argv[4])
        y = int(sys.argv[5])
    else:
        x = y = 255
    
    # Create a connection to the remote machine
    conn = CSCPConnection(hostname,
                          scp_data_length=scp_data_length,
                          n_outstanding=n_outstanding)
    
    # Queue up and send an 'sver' command to all N_CPUS CPUs. A counting
    # semaphore is used to count the responses arriving and block the main
    # thread until all have arrived.
    print("Sending CMD_VER to {} CPUs...".format(N_CPUS))
    outstanding = Semaphore(0)
    
    def sver_success(pkt):
        print_version_response_packet(pkt)
        outstanding.release()
    
    before = time.time()
    for p in range(N_CPUS):
        conn.send_scp(x, y, p, 0, on_success=sver_success, on_error=on_error)
    for _ in range(N_CPUS):
        outstanding.acquire()
    duration = time.time() - before
    
    print("All responses receieved after {} ms.\n".format(
        int(duration * 1000)))
    
    # Write some random data to SDRAM. An 'Event' is used to wait for the
    # success callback to be called.
    write_data = os.urandom(DATA_LEN)
    print("Writing {} bytes of random data to 0x{:x}...".format(
        DATA_LEN, TEST_ADDRESS))
    e = Event()
    before = time.time()
    conn.write(TEST_ADDRESS, write_data, x, y, on_success=e.set, on_error=on_error)
    e.wait()
    duration = time.time() - before
    
    print("Write completed in {} ms! Throughput = {:.3f} Mbit/s\n".format(
        int(duration * 1000), ((DATA_LEN * 8.0) / duration / 1024.0 / 1024.0)))
    
    # Read the data back from SDRAM into a buffer. Once again, an Event is used
    # to wait for the success callback.
    print("Reading back {} bytes from 0x{:x}...".format(DATA_LEN, TEST_ADDRESS))
    read_data = bytearray(DATA_LEN)
    e = Event()
    before = time.time()
    conn.read(TEST_ADDRESS, DATA_LEN, read_data, x, y,
              on_success=(lambda _: e.set()), on_error=on_error)
    e.wait()
    duration = time.time() - before
    
    print("Read completed in {} ms! Throughput = {:.3f} Mbit/s\n".format(
        int(duration * 1000), ((DATA_LEN * 8.0) / duration / 1024.0 / 1024.0)))
    
    # Validate read data matches what was written
    if write_data == read_data:
        print("The data read back matched the data written!\n")
    else:
        print("ERROR: The data read did not match the data written!\n")
    
    # Clean up nicely
    conn.close()
    print("Connection shut down.")
