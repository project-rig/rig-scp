"""A Python wrapper around Rig C SCP which presents a simple asynchronous API
for sending SCP packets and performing bulk reads/writes.
"""

from threading import Thread, Lock
from functools import wraps, partial
from collections import deque

from rig.machine_control.consts import SCPCommands, SCPReturnCodes, SCP_PORT
from rig.machine_control.scp_connection import \
    SCPError, TimeoutError, FatalReturnCodeError
from rig.machine_control.packets import SCPPacket

from _rig_c_scp import ffi, lib


class CSCPConnection(object):
    """A Python wrapper around the high performance 'Rig C SCP' SCP wrapper.

    The :py:class:`.CSCPConnection` object spawns a background thread in which
    the Rig C SCP/libuv event loop executes. SCP/read/write commands may be
    made from any Python thread and the requests are initially queued (in
    :py:attr:`._queue`). When a new request is queued, the libuv event loop is
    woken up and the requests taken off the Python queue and inserted into the
    C-managed queue.
    """

    def __init__(self, hostname, port=SCP_PORT, n_tries=5,
                 n_outstanding=1, scp_data_length=256):
        self._hostname = hostname
        self._port = port
        self._n_tries = n_tries
        self._n_outstanding = n_outstanding
        self._scp_data_length = scp_data_length

        # A lock to be held when interacting with _queue and _closed.
        self._lock = Lock()

        # Calls queued for execution in the libuv thread. This queue just
        # contains a series of zero-argument functions. Protected by
        # self._lock.
        self._queue = deque()

        # Set to true when the connection is being shut down. Protected by
        # self._lock.
        self._closed = False

        # Mapping from outstanding _Callback objects to their CFFI handles.
        # Used to keep the handles alive until the callback is used.
        self._cb_handles = {}

        # Create a sockaddr for the host (and resolve the hostname)
        self._addrinfo = lib.hostname_to_addrinfo(hostname.encode("UTF-8"),
                                                  str(port).encode("UTF-8"))
        if self._addrinfo == ffi.NULL:
            raise IOError("Could not resolve {}.".format(hostname))
        self._addrinfo = ffi.gc(self._addrinfo, lib.free)

        # Create a C object which refers to this connection object. Used as
        # userdata in certain callbacks.
        self._self = ffi.new_handle(self)

        # Create the LibUV event loop
        self._loop = lib.malloc_uv_loop_t()
        assert self._loop != ffi.NULL, \
            "Failed to allocate memory for libuv event loop"
        self._loop = ffi.gc(self._loop, lib.free)
        assert lib.uv_loop_init(self._loop) == 0, \
            "Failed to initialise event libuv loop."

        # Set up a thread-safe handle which allows other threads to wake up the
        # libuv event loop, e.g. when a new command is sent/received
        self._wakeup_handle = lib.malloc_uv_async_t()
        assert self._wakeup_handle != ffi.NULL, \
            "Failed to allocate memory for async wakeup handle."
        self._wakeup_handle.data = self._self
        assert lib.uv_async_init(self._loop,
                                 self._wakeup_handle,
                                 lib.async_cb) == 0, \
            "Failed to initialise async wakeup handle."

        # Create the Rig C SCP connection
        self._conn = None
        self._rs_init()

        # Start background thread in which the event loop will run.
        self._bg_thread = Thread(target=lib.uv_run,
                                 args=(self._loop, lib.UV_RUN_DEFAULT),
                                 name="CSCPConnection to {}:{}".format(
                                    hostname, port))
        self._bg_thread.daemon = True
        self._bg_thread.start()

    def close(self):
        """Shut down the connection."""
        # Trigger shutdown
        with self._lock:
            self._closed = True
        self._wakeup()

        # Wait for the event loop to terminate
        self._bg_thread.join()

    def _wakeup(self):
        """Wakeup the libuv thread and cause calls in `_queue` to be processed.

        May be called from any thread. Schedules :py:meth:`._on_wakeup` to be
        called on this object by this object's libuv mainloop.
        """
        lib.uv_async_send(self._wakeup_handle)

    def _on_wakeup(self):
        """Callback called in the libuv thread once _wakeup has been called.

        This method forwards queued calls
        """
        # Handle all queued actions.  Does nothing while not connected. Once
        # the connection is recreated, this wakeup function is called again.
        while self._queue and self._conn is not None:
            with self._lock:
                if self._queue:
                    fn = self._queue.popleft()
                else:  # pragma: no cover
                    fn = None
            if fn is not None:
                fn()

        # If the connection is closed free the connection (causing the queued
        # actions to have their error callback called).
        with self._lock:
            if self._closed:
                # Queue not emptied, wake up again to process the queue and
                # then try freeing then.
                if self._queue:
                    # Can't easily test this line being hit...
                    self._wakeup()  # pragma: no cover
                else:
                    self._rs_free()

    def _execute_in_bg_thread(f):
        """A decorator which causes calls to a method to be enqueued and
        executed in the libuv thread.
        """
        @wraps(f)
        def _f(self, *args, **kwargs):
            with self._lock:
                if not self._closed:
                    self._queue.append(partial(f, self, *args, **kwargs))
                    self._wakeup()
                else:
                    raise IOError("CSCPConnection closed!")
        return _f

    def _rs_init(self):
        """Initialise a new Rig C SCP connection."""
        self._conn = lib.rs_init(self._loop,
                                 self._addrinfo.ai_addr,
                                 self._scp_data_length,
                                 self._n_tries,
                                 self._n_outstanding)
        assert self._conn != ffi.NULL, \
            "Failed to allocate/initialise Rig C SCP connection."

    def _rs_free(self):
        """Shutdown and free the current Rig C SCP connection."""
        lib.rs_free(self._conn, lib.free_cb, self._self)
        self._conn = None

    def _on_free(self):
        """Callback when the current Rig C SCP connection has been freed."""
        # If the connection has been closed, shut down the background
        # thread, otherwise re-create the connection
        if self._closed:
            lib.uv_stop(self._loop)
            return
        else:
            # NB: Executed iff self._closed is false.
            self._rs_init()

            # Process any tasks queued while the previous connection was
            # freed.
            self._on_wakeup()

    def _run_as_callback(cb_name):
        """Decorator which runs the given *method* as a C-registered callback.

        Example usage::

            >>> #class CSCPConnection(object): ...
            >>>     @_run_as_callback("some_cb")
            ...     def _on_some_cb(self, a, b, c, callback):
            ...         # ...

        In this example, a C function has been "extern python"'d in the CFFI
        wrapped called 'some_cb' whose function signature looks something
        like::

            extern "Python" void some_cb(rs_conn_t *conn,
                                         type a, type b, type c,
                                         void *cb_data);

        When this C function is called (from C), the decorated Python method
        will be called.

        The 'cb_data' argument is presumed to contain a CFFI handle pointing to
        a :py:class:`_Callback` object whose ``connection`` attribute is a
        reference to a :py:class:`CSCPConnection` instance. It is this instance
        whose '_on_some_cb' method will be called.

        In the call to the decorated Python method, the ``conn`` argument is
        omitted and the ``cb_data`` argument is substituted for the
        :py:class:`._Callback` object.
        """
        def wrap(f):
            # Associate the callback function with its C prototype
            def c_callback(*args):
                callback = ffi.from_handle(args[-1])
                args = list(args[1:-1]) + [callback]
                f(callback.connection, *args)
            ffi.def_extern(cb_name)(c_callback)
            return f
        return wrap

    def _report_error(self, error, cmd_rc, callback):
        """Report an error via a callback if one has occurred. Returns True if
        an error occurred."""
        if error:
            # An error occurred, report this via the supplied callback (if
            # present).
            if callback.on_error is not None:
                err_name = ffi.string(lib.rs_err_name(error)).decode("UTF-8")
                if err_name == "RS_EBAD_RC":
                    callback.on_error(
                        FatalReturnCodeError(cmd_rc, callback.packet))
                elif err_name == "RS_ETIMEOUT":
                    callback.on_error(TimeoutError(packet=callback.packet))
                else:
                    callback.on_error(SCPError(
                        "{}: {}".format(
                            err_name,
                            ffi.string(
                                lib.rs_strerror(error)).decode("UTF-8")),
                        callback.packet))
            return True
        elif cmd_rc != SCPReturnCodes.ok:
            # The return code indicates a problem, report this back
            if callback.on_error is not None:
                callback.on_error(
                    FatalReturnCodeError(cmd_rc, callback.packet))
            return True
        else:
            # No error to report...
            return False

    @_execute_in_bg_thread
    def send_scp(self, x, y, p, cmd, arg1=0, arg2=0, arg3=0, data=b'',
                 expected_args=3, on_success=None, on_error=None,
                 timeout=0.5):
        """Send an SCP command."""
        cb = _Callback(self, on_success, on_error,
                       x=x, y=y, p=p, cmd=cmd,
                       n_args=3, arg1=arg1, arg2=arg2, arg3=arg3,
                       data=data)
        cb_handle = ffi.new_handle(cb)
        self._cb_handles[cb] = cb_handle

        # Allocate buffer for data field
        data_buf = ffi.new("uv_buf_t *")
        data_buf.base = lib.malloc(self._scp_data_length)
        assert data_buf.base != ffi.NULL, \
            "Could not allocate buffer for SCP command data."
        data_buf.len = min(len(data), self._scp_data_length)

        # Copy in argument data
        ffi.memmove(data_buf.base, data, data_buf.len)

        lib.rs_send_scp(self._conn,
                        (((x & 0xFF) << 8) | (y & 0xFF)),
                        p,
                        cmd,
                        3,  # n_args_send
                        expected_args,
                        arg1, arg2, arg3,
                        data_buf[0],
                        self._scp_data_length,
                        int(timeout * 1000),
                        lib.send_scp_cb,
                        cb_handle)

    @_run_as_callback("send_scp_cb")
    def _on_scp_cb(self, error, cmd_rc, n_args, arg1, arg2, arg3, data_buf,
                   callback):
        """Callback in response to SCP command."""
        self._cb_handles.pop(callback)

        try:
            if not self._report_error(error, cmd_rc, callback):
                if callback.on_success is not None:
                    # Copy response data out of buffer
                    data = bytes(ffi.buffer(data_buf.base, data_buf.len))
                    callback.on_success(SCPPacket(
                        cmd_rc=cmd_rc,
                        arg1=arg1 if n_args >= 1 else None,
                        arg2=arg2 if n_args >= 2 else None,
                        arg3=arg3 if n_args >= 3 else None,
                        data=data))
        finally:
            # Free the buffer allocated to the call's response
            lib.free(data_buf.base)

    @_execute_in_bg_thread
    def write(self, x, y, p, address, data, on_success=None, on_error=None,
              timeout=0.5):
        """Perform a bulk write operation."""
        cb = _Callback(self, on_success, on_error,
                       x=x, y=y, p=p, cmd=SCPCommands.write,
                       n_args=3, arg1=address, arg2=None, arg3=None,
                       data=b'')
        cb_handle = ffi.new_handle(cb)
        self._cb_handles[cb] = cb_handle

        # Set up buffer for write data
        data_buf = ffi.new("uv_buf_t *")
        data_buf.base = lib.malloc(len(data))
        assert data_buf.base != ffi.NULL, \
            "Could not allocate buffer for write data."
        data_buf.len = len(data)

        # Copy write data into buffer
        ffi.memmove(data_buf.base, data, data_buf.len)

        lib.rs_write(self._conn,
                     (((x & 0xFF) << 8) | (y & 0xFF)),
                     p,
                     address,
                     data_buf[0],
                     int(timeout * 1000),
                     lib.write_cb,
                     cb_handle)

    @_run_as_callback("write_cb")
    def _on_write_cb(self, error, cmd_rc, data_buf, callback):
        """Callback in response to data write."""
        self._cb_handles.pop(callback)
        lib.free(data_buf.base)

        if not self._report_error(error, cmd_rc, callback):
            if callback.on_success is not None:
                callback.on_success()

    @_execute_in_bg_thread
    def read(self, x, y, p, address, length, buffer,
             on_success=None, on_error=None, timeout=0.5):
        """Perform a bulk read operation."""
        cb = _Callback(self, on_success, on_error, buffer,
                       x=x, y=y, p=p, cmd=SCPCommands.read,
                       n_args=3, arg1=address, arg2=None, arg3=None,
                       data=b'')
        cb_handle = ffi.new_handle(cb)
        self._cb_handles[cb] = cb_handle

        # Set up buffer for read data
        data_buf = ffi.new("uv_buf_t *")
        data_buf.base = lib.malloc(length)
        assert data_buf.base != ffi.NULL, \
            "Could not allocate buffer for read data."
        data_buf.len = length

        lib.rs_read(self._conn,
                    (((x & 0xFF) << 8) | (y & 0xFF)),
                    p,
                    address,
                    data_buf[0],
                    int(timeout * 1000),
                    lib.read_cb,
                    cb_handle)

    @_run_as_callback("read_cb")
    def _on_read_cb(self, error, cmd_rc, data_buf, callback):
        """Callback in response to data read."""
        self._cb_handles.pop(callback)

        try:
            if not self._report_error(error, cmd_rc, callback):
                if callback.on_success is not None:
                    # Copy read data into a Python-accessible buffer
                    ffi.memmove(callback.buffer, data_buf.base,
                                min(data_buf.len, len(callback.buffer)))

                    callback.on_success(callback.buffer)
        finally:
            lib.free(data_buf.base)

    @property
    def scp_data_length(self):
        """The maximum number of data payload bytes to include in an SCP packet.

        .. warning::

            Changing this value while any command/read/write is in progress may
            result in that command failing.
        """
        return self._scp_data_length

    @scp_data_length.setter
    @_execute_in_bg_thread
    def scp_data_length(self, value):
        self._scp_data_length = value
        self._rs_free()

    @property
    def n_outstanding(self):
        """The number of SCP packets which may be sent without a response being
        received.

        .. warning::

            Changing this value while any command/read/write is in progress may
            result in that command failing.
        """
        return self._n_outstanding

    @n_outstanding.setter
    @_execute_in_bg_thread
    def n_outstanding(self, value):
        self._n_outstanding = value
        self._rs_free()


@ffi.def_extern()
def free_cb(cb_data):
    """Callback for rs_free."""
    ffi.from_handle(cb_data)._on_free()


@ffi.def_extern()
def async_cb(handle):
    """Callback on libuv async events."""
    ffi.from_handle(handle.data)._on_wakeup()


class _Callback(object):
    def __init__(self, connection, on_success, on_error, buffer=None,
                 x=None, y=None, p=None, cmd=None,
                 n_args=0, arg1=None, arg2=None, arg3=None, data=None):
        # CSCPConnection object which owns this callback
        self.connection = connection

        # Callback functions
        self.on_success = on_success
        self.on_error = on_error

        # Buffer supplied for reading into
        self.buffer = buffer

        # SCP packet contents
        self.x = x
        self.y = y
        self.p = p
        self.cmd = cmd
        self.arg1 = arg1 if n_args >= 1 else None
        self.arg2 = arg2 if n_args >= 2 else None
        self.arg3 = arg3 if n_args >= 3 else None
        self.data = data

    @property
    def packet(self):
        return SCPPacket(reply_expected=True, tag=0xff, dest_port=0,
                         dest_cpu=self.p, src_port=7, src_cpu=31,
                         dest_x=self.x, dest_y=self.y, src_x=0, src_y=0,
                         cmd_rc=self.cmd, seq=None,
                         arg1=self.arg1, arg2=self.arg2, arg3=self.arg3,
                         data=self.data)
