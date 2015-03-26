Rig SCP
=======

Rig SCP is a high-performance C implementation of the SpiNNaker Command Protocol
(SCP) transport mechanism.

Purpose
-------

This library is designed to support high throughput reliable transmission and
reception of SCP packets from a SpiNNaker machine with a focus on making
efficient use of all available I/O resources. In particular, multiple Ethernet
connections to a single machine are supported as well as the use of windowing to
hide the effects of latency on throughput. The library provides a
high-performance wrapper around the `CMD_READ` and `CMD_WRITE` commands which
allows users to bulk-send/receive arbitrarily large blocks of data to a
SpiNNaker system. With this single exception, the library does *not* aim to
provide a general high-level interface to SCP commands, users are required to
construct their own sensible abstraction on top of this.

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
