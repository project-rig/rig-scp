Rig C SCP
=========

Rig C SCP is a low-level, high-performance C implementation of the [SpiNNaker
Command Protocol
(SCP)](https://spinnaker.cs.manchester.ac.uk/tiki-index.php?page=Application+note+5+-+SCP+Specification)
transport mechanism built on top of [libuv](http://docs.libuv.org/en/v1.x/).

Though the C library can be used as-is, it is intended primarily to be used via
a Python wrapper build with [CFFI](https://cffi.readthedocs.org/en/latest/).

This library is intended to serve as an optional higher-performance back-end
for the [Rig](github.com/project-rig/rig) library.

Purpose
-------

This library is designed to support high throughput, reliable transmission and
reception of SCP packets from a SpiNNaker machine with a focus on making
efficient use of all available I/O resources.

Though a number of Python SCP implementations now have comparable throughput to
Rig C SCP, these implementations quickly become CPU bound if multiple
connections are used in parallel. By contrast, this C implementation has been
observed saturating 1 GBit Ethernet links while not saturating the CPU.

[Windowing](http://en.wikipedia.org/wiki/TCP_tuning#Window_size) is used to
hide the effects of network latency on throughput and, within the
implementation, data is not wastefully copied multiple times between the
network socket and application. The use of multiple SCP connections
simultaneously is also indirectly supported thanks to a completely asynchronous
API.

The API also allows users to asynchronously send arbitrary SCP packets.  The
`CMD_READ` and `CMD_WRITE` commands are optionally treated specially via a
high-level interface which allow users to bulk-read/write arbitrarily large
blocks of data to a SpiNNaker system's memory.

Please note that this library does *not* aim to provide a general, high-level
interface to the SCP command set. Users are instead required to construct their
own sensible abstractions on top Rig SCP.

C API Documentation
-------------------

The C API is documented in detail in its (short) header file
[`ric_c_scp/rs.h`](ric_c_scp/rs.h).

A tutorial example program [`examples/hello.c`](examples/hello.c) is included
which provides a heavily annotated tutorial-style walk-through of the complete
API.

Python API Documentation
------------------------

The Python API largely lives in
[`ric_c_scp/c_scp_connection.py`](ric_c_scp/c_scp_connection.py) and includes
standard Python docstring API documentation.

A tutorial example program [`examples/hello.py`](examples/hello.py) is included
which provides an annotated walk-through of the API.


Python library compilation and installation
-------------------------------------------

Rig C SCP depends on [libuv](http://docs.libuv.org/en/v1.x/) which should be
installed prior to installation.

The Python module is built and installed as usual::

    $ python setup.py install


Testing
-------

[Check](http://check.sourceforge.net/) is used for unit testing of the C
library. To build and run the C library test suite under valgrind use:

    $ ./c_tests/run.sh

[pytest](http://pytest.org/) is used for unit testing of the Python wrapper
library. Assuming the library has been installed, the tests are executed using:

    $ pip install -r requirements-test.txt
    $ py.test py_tests

[flake8](https://pypi.python.org/pypi/flake8) is used for linting the Python
wrapper code like so:

    $ flake8 rig_c_scp py_tests

[tox](https://tox.readthedocs.org/) may be used to automatically build and test
the Python library against several versions of Python (and also run the code
lint checker) like so:

    $ pip install tox
    $ tox


Internal Architecture (C Library)
---------------------------------

The following diagram depicts a single Rig C SCP 'connection' to a single IP
address (i.e. SpiNNaker Chip):

	                         A Rig SCP 'Connection'
	                         ======================
	
	                                                  Outstanding
	                                                     Slots
	                                                  '''''''''''
	                     Request Queue
	                     '''''''''''''          /|    +------+--+    |\      +---+
	                                           / |--->|Packet|Tm|<-->| \     | S |
	                 +---+---+--   --+---+    |  |    +------+--+    |  |    | o |
	rs_send_scp -,   |Req|Req|       |Req|    |  |--->|Packet|Tm|<-->|  |    | c |
	    rs_read -+-->|   |   |  ...  |   |--->|  |    +------+--+    |  |<-->| k |
	   rs_write -'   |   |   |       |   |    |  |        ...        |  |    | e |
	                 +---+---+--   --+---+    |  |    +------+--+    |  |    | t |
	                                           \ |--->|Packet|Tm|<-->| /     |   |
	                                            \|    +------+--+    |/      +---+
	
	    (1)                   (2)              (3)      (4)   (5)    (6)      (7)


1. Rig SCP uses [libuv](http://docs.libuv.org/en/v1.x/) to present a simple
   asynchronous interface. Users call the API functions to schedule the sending
   of SCP packets and register a *callback* function to be called when the
   packet's response returns (or an error occurs). Users supply the data to
   transmit by reference and it is copied into the transmit buffer at the last
   possible moment.

2. Each API call generates a single *request* which is placed in the *request
   queue*. Requests represent either a single SCP packet or a bulk read/write
   operation (which may eventually result in the sending of many SCP packets).

3. *Requests* are processed out of the *request queue* where they are split into
   (possibly many) individual SCP packets which are allocated to one of
   `n_outstanding` *outstanding slots* and sent to the machine. Once all SCP
   packets associated with a *request* have been allocated an *outstanding
   slots*, the *request* is removed from the *request queue*.

4. Each *outstanding slot* represents a single SCP packet which has been sent
   to the machine and is awaiting a response.

5. Each *outstanding slot* has a timer which causes packets to be retransmitted
   if a response is not received after `timeout` milliseconds. If a packet does
   not receive a response after `n_tries` transmissions it is dropped and the
   user callback is called with an error status.

6. Each packet is allocated a unique *sequence number* which is used to identify
   responses from a machine and return them to the correct *outstanding
   slot*. When the last packet associated with a request receives its response
   or if any packet produces an error, the user supplied *callback* is called
   and the request is considered complete.

7. Note that only a single UDP socket is used by a Rig SCP connection. Since Rig
   SCP is asynchronous, multiple Rig SCP connections can coexist in the same
   thread and thus make use of additional Ethernet links to a single SpiNNaker
   machine.

Given the above description, the following observations are worth highlighting:

* This library is low level. Many basic, but higher level, functions are left up
  to the user:
  * Discovery of the maximum allowed `scp_data_length`
  * Discovery of the maximum allowed `n_outstanding`
  * Discovery of available Ethernet connections
  * Intelligently selecting which of a number of Rig SCP connections to use for a
    given task
  * Generation and interpretation of all SCP commands excluding `CMD_WRITE` and
    `CMD_READ`.
* The library automatically splits reads/writes issued via the API into SCP
  packets whose payload is no longer than `scp_data_length`.
* The maximum number of *outstanding slots* is fixed after the connection is
  created, as a result only one SCP connection should be made to a given
  SpiNNaker chip at any one time.
* When a read or write is issued, it will be spread across as many outstanding
  slots at once as possible. Subsequent requests will not be processed until all
  read/write packets have been issued.
* The *request queue* grows transparently to accommodate as many outstanding
  requests as are supplied.
* Though users are free to generate their own read/write SCP packets, this
  necessitates the creation of a large number of requests (compared with just
  one when using the built-in API). As a result, it is far more efficient to
  use the API for writes.
