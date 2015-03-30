Rig SCP
=======

Rig SCP is a high-performance C implementation of the [SpiNNaker Command
Protocol
(SCP)](https://spinnaker.cs.manchester.ac.uk/tiki-index.php?page=Application+note+5+-+SCP+Specification)
transport mechanism.

Purpose
-------

This library is designed to support high throughput reliable transmission and
reception of SCP packets from a SpiNNaker machine with a focus on making
efficient use of all available I/O resources.
[Windowing](http://en.wikipedia.org/wiki/TCP_tuning#Window_size) is used to hide
the effects of network latency on throughput and, within the implementation,
data is not wastefully copied multiple times between the network socket and
application. The use of multiple SCP connections simultaneously is also
indirectly supported thanks to a completely asynchronous API.


The API also allows users to asynchronously send arbitrary SCP packets.  The
`CMD_READ` and `CMD_WRITE` commands are optionally treated specially via a
high-level interface which allow users to bulk-read/write arbitrarily large
blocks of data to a SpiNNaker system's memory.

Please note that this library does *not* aim to provide a general, high-level
interface to the SCP command set. Users are instead required to construct their
own sensible abstractions on top Rig SCP.

Performance
-----------

Some informal benchmarks were conducted against a locally connected SpiNN-5
board running SC&MP v1.33 where a 10 MByte block of random data was written then
read back from chip (0, 0)'s SDRAM.

Implementation                                              | Version | Read (MBit/s) | Write (MBit/s)
----------------------------------------------------------- | ------- | ------------- | --------------
Rig SCP (this)                                              | 94121f7 | 29.8          | 32.1
[ybug](https://github.com/SpiNNakerManchester/ybug)         | 1.33    | 6.5           | 6.4
[Rig](https://github.com/project-rig/rig)                   | dc97817 | 5.1           | 5.0
[SpiNNMan](https://github.com/SpiNNakerManchester/SpiNNMan) | 3eab5ee | 4.0           | 4.1


Architecture
------------

* Rig SCP operates in a single thread and uses *libuv* to handle concurrent I/O.
* Rig SCP allows the opening of multiple parallel Ethernet *connections* to a
  SpiNNaker machine.
* Each *connection* has a *request queue* containing a stream of SCP packets,
  bulk read commands and bulk write commands which is populated by the user.
* Requests from the *request queue* and taken in order and transmitted to the
  machine. SCP packets awaiting a response are placed in an *outstanding queue*
  while an acknowledgement is awaited from the machine.
* When an SCP acknowledgement arrives or a read/write completes, a callback will
  be called.
* In the case of bulk read/write commands, multiple SCP packets may be used
  internally to complete the request however only a single request need be
  presented by the user.
* If an SCP acknowledgement does not arrive before a timeout occurs, the request
  packet will be retransmitted. If after a number of retransmission attempts no
  acknowledge is forthcoming, a failure indication will be provided to the
  callback function.

The end user is notably required to perform the following duties:

* Discovery and selection of available Ethernet connections.
* Selection of the most appropriate Ethernet connection to use to send each
  request.
* Discovery of the maximum allowed packet size supported by target system.
* Discovery of the maximum number of outstanding requests allowed to a single
  connection.
* Generation and interpretation of SCP commands and responses (with the
  sole exception of bulk read/writes).

Tutorial & Example Program
--------------------------

A tutorial example program `hello.c` is included which provides a heavily
annotated walk-through of the process of using Rig SCP.

Compiling
---------

CMake is used to automate the build process. To begin with we recommend using a
seperate build directory as follows:

    $ mkdir build
    $ cd build
    $ cmake ..

This will create a `Makefile` in the `build/` directory. Type `make` to compile
Rig SCP.

Tests
-----

[Check](http://check.sourceforge.net/) is used for unit testing. To build and
run the test suite under valgrind use:

    $ make run_tests
