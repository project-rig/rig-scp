import pytest
import time
from mock import Mock

from threading import Event

from rig.machine_control.scp_connection import \
    FatalReturnCodeError, TimeoutError, SCPError
from rig.machine_control.consts import FATAL_SCP_RETURN_CODES, SCPCommands

from rig_c_scp import CSCPConnection

from mock_machine import MockMachine


@pytest.yield_fixture
def mm():
    """A mock machine (simple SCP responder)."""
    mm = MockMachine()
    yield mm
    mm.close()


@pytest.yield_fixture
def c(mm):
    """An SCP connection to mm."""
    c = CSCPConnection(*mm.sockname,
                       n_outstanding=1,
                       scp_data_length=10,
                       n_tries=1)
    yield c
    c.close()


def test_unresolvable_hostname():
    with pytest.raises(IOError):
        CSCPConnection("")


def test_immediately_close(c):
    pass


def test_double_close(c):
    c.close()


def test_actions_fail_after_close(c):
    c.close()
    with pytest.raises(IOError):
        c.write(0, 0, 0, 0xDEADBEEF, b"HELLO")


def test_wakeup_queue(c):
    with c._lock:
        # We can't easily test a race-condition where the queue is empty by the
        # time the lock is acquired. As a work-around we simply insert a "None"
        # job into the queue to simulate the handling of this.
        c._queue.append(None)

        # We attempt to get the lock (e.g. as would be done if in a callback we
        # tried to send a new packet) to ensure the lock isn't being held when
        # the queued function is called.
        def get_and_release():
            with c._lock:
                pass
        get_and_release = Mock(side_effect=get_and_release)
        c._queue.append(get_and_release)

        # Finally an event is used to allow this thread to wait for the queue
        # to have been processed
        e = Event()
        c._queue.append(e.set)

    c._wakeup()

    # Wait for the queue to be processed
    e.wait()

    assert len(get_and_release.mock_calls) == 1


@pytest.mark.parametrize("no_callback", [True, False])
@pytest.mark.parametrize("n_args", [0, 1, 2, 3])
@pytest.mark.parametrize("data", [b"", b"hello"])
def test_send_scp(mm, c, n_args, data, no_callback):
    e = Event()
    on_success = Mock(side_effect=(lambda *_, **__: e.set()))
    on_error = Mock(side_effect=(lambda *_, **__: e.set()))  # pragma: no cover

    c.send_scp(x=1, y=2, p=3, cmd=4, arg1=5, arg2=6, arg3=7, data=b"foo",
               expected_args=n_args,
               on_success=None if no_callback else on_success,
               on_error=on_error)

    pkt, resp = mm.handle_scp(n_args=n_args, data=data)

    # Packet sent should be recieved intact
    assert pkt.dest_x == 1
    assert pkt.dest_y == 2
    assert pkt.dest_cpu == 3
    assert pkt.cmd_rc == 4
    assert pkt.arg1 == 5
    assert pkt.arg2 == 6
    assert pkt.arg3 == 7
    assert pkt.data == b"foo"

    if no_callback:
        # Give some time to ensure it is likely the (None) callback handler was
        # called
        time.sleep(0.05)
    else:
        # Response should have been provided in callback intact
        e.wait()

        assert len(on_success.mock_calls) == 1
        assert on_success.mock_calls[0][1][0].cmd_rc == resp.cmd_rc
        assert on_success.mock_calls[0][1][0].arg1 == resp.arg1
        assert on_success.mock_calls[0][1][0].arg2 == resp.arg2
        assert on_success.mock_calls[0][1][0].arg3 == resp.arg3
        assert on_success.mock_calls[0][1][0].data == resp.data

        # Should not have failed...
        assert len(on_error.mock_calls) == 0


@pytest.mark.parametrize("no_callback", [True, False])
def test_write(mm, c, no_callback):
    e = Event()
    on_success = Mock(side_effect=(lambda *_, **__: e.set()))
    on_error = Mock(side_effect=(lambda *_, **__: e.set()))  # pragma: no cover

    c.write(x=1, y=2, p=3, address=0xDEADBEEF, data=b"FOOBAR",
            on_success=None if no_callback else on_success, on_error=on_error)

    pkt, resp = mm.handle_scp()

    # Packet sent should be recieved intact
    assert pkt.dest_x == 1
    assert pkt.dest_y == 2
    assert pkt.dest_cpu == 3
    assert pkt.cmd_rc == SCPCommands.write
    assert pkt.arg1 == 0xDEADBEEF
    assert pkt.arg2 == 6
    assert pkt.data == b"FOOBAR"

    if no_callback:
        # Give some time to ensure it is likely the (None) callback handler was
        # called
        time.sleep(0.05)
    else:
        e.wait()

        # Completion should be acknowledged
        on_success.assert_called_once_with()

        # Should not have failed...
        assert len(on_error.mock_calls) == 0


@pytest.mark.parametrize("no_callback", [True, False])
def test_read(mm, c, no_callback):
    e = Event()
    on_success = Mock(side_effect=(lambda *_, **__: e.set()))
    on_error = Mock(side_effect=(lambda *_, **__: e.set()))  # pragma: no cover

    buffer = bytearray(6)
    c.read(x=1, y=2, p=3, address=0xDEADBEEF, length=6,
           buffer=buffer,
           on_success=None if no_callback else on_success,
           on_error=on_error)

    pkt, resp = mm.handle_scp(data=b"FOOBAR")

    # Packet sent should be recieved intact
    assert pkt.dest_x == 1
    assert pkt.dest_y == 2
    assert pkt.dest_cpu == 3
    assert pkt.cmd_rc == SCPCommands.read
    assert pkt.arg1 == 0xDEADBEEF
    assert pkt.arg2 == 6
    assert pkt.data == b""

    if no_callback:
        # Give some time to ensure it is likely the (None) callback handler was
        # called
        time.sleep(0.05)
    else:
        e.wait()

        # Completion should be acknowledged and the data read provided
        on_success.assert_called_once_with(b"FOOBAR")

        # The buffer provided should have been used
        assert buffer == b"FOOBAR"
        on_success.mock_calls[0][1][0] is buffer

        # Should not have failed...
        assert len(on_error.mock_calls) == 0


@pytest.mark.parametrize("error_cb", [True, False])
@pytest.mark.parametrize("send_cmd,send_args",
                         [("send_scp", (1, 2, 3, 0)),
                          ("write", (1, 2, 3, 0, b"hello")),
                          ("read", (1, 2, 3, 0, 5, bytearray(5)))])
@pytest.mark.parametrize("cmd_rc", FATAL_SCP_RETURN_CODES)
def test_error_bad_rc(mm, c, send_cmd, send_args, cmd_rc, error_cb):
    e = Event()
    on_success = Mock(  # pragma: no cover
        side_effect=(lambda *_, **__: e.set()))
    on_error = Mock(side_effect=(lambda *_, **__: e.set()))

    # Send the command
    getattr(c, send_cmd)(*send_args, on_success=on_success,
                         on_error=on_error if error_cb else None)

    # Respond with a bad RC
    mm.handle_scp(cmd_rc=cmd_rc)

    # Should get an error back if the callback is present (shouldn't crash
    # otherwise)
    if error_cb:
        e.wait()
        assert len(on_success.mock_calls) == 0
        assert len(on_error.mock_calls) == 1

        # The error should contain the relevent info...
        exc = on_error.mock_calls[0][1][0]
        assert isinstance(exc, FatalReturnCodeError)
        assert FATAL_SCP_RETURN_CODES[cmd_rc] in str(exc)
        assert exc.packet.dest_x == 1
        assert exc.packet.dest_y == 2
        assert exc.packet.dest_cpu == 3


@pytest.mark.parametrize("error_cb", [True, False])
@pytest.mark.parametrize("send_cmd,send_args",
                         [("send_scp", (1, 2, 3, 0)),
                          ("write", (1, 2, 3, 0, b"hello")),
                          ("read", (1, 2, 3, 0, 5, bytearray(5)))])
def test_error_timeout(mm, c, send_cmd, send_args, error_cb):
    e = Event()
    on_success = Mock(  # pragma: no cover
        side_effect=(lambda *_, **__: e.set()))
    on_error = Mock(side_effect=(lambda *_, **__: e.set()))

    # Send the command
    getattr(c, send_cmd)(*send_args,
                         on_success=on_success,
                         on_error=on_error if error_cb else None,
                         timeout=0.01)

    # Ignore the incoming requests (causing them to timeout)
    mm.recv_scp()

    # Should get a timeout error back if the callback is present (shouldn't
    # crash otherwise)
    if error_cb:
        e.wait()
        assert len(on_success.mock_calls) == 0
        assert len(on_error.mock_calls) == 1

        # The error should contain the relevent info...
        exc = on_error.mock_calls[0][1][0]
        assert isinstance(exc, TimeoutError)
        assert exc.packet.dest_x == 1
        assert exc.packet.dest_y == 2
        assert exc.packet.dest_cpu == 3


@pytest.mark.parametrize("error_cb", [True, False])
@pytest.mark.parametrize("send_cmd,send_args",
                         [("send_scp", (1, 2, 3, 0)),
                          ("write", (1, 2, 3, 0, b"hello")),
                          ("read", (1, 2, 3, 0, 5, bytearray(5)))])
def test_error_free(mm, c, send_cmd, send_args, error_cb):
    e = Event()
    on_success = Mock(  # pragma: no cover
        side_effect=(lambda *_, **__: e.set()))
    on_error = Mock(side_effect=(lambda *_, **__: e.set()))

    # Send the command
    getattr(c, send_cmd)(*send_args, on_success=on_success,
                         on_error=on_error if error_cb else None)

    # Ignore the incoming request, and, once its arrrived, we'll free the
    # connection forcing the request to get a 'freed' error.
    mm.recv_scp()

    c.close()

    # Should get a generc SCP error back if the callback is present (shouldn't
    # crash otherwise)
    if error_cb:
        e.wait()
        assert len(on_success.mock_calls) == 0
        assert len(on_error.mock_calls) == 1

        # The error should contain the relevent info...
        exc = on_error.mock_calls[0][1][0]
        assert isinstance(exc, SCPError)
        assert "FREE" in str(exc)
        assert exc.packet.dest_x == 1
        assert exc.packet.dest_y == 2
        assert exc.packet.dest_cpu == 3


def test_scp_data_length(mm, c):
    # Check that the value provided in the constructor is used
    c.scp_data_length == 10

    # Should get two SCP commands for a 20-byte block
    e = Event()
    c.write(0, 0, 0, 0xDEADBEEF, b"ABCDEFGHIJKLMNOPQRST",
            on_success=e.set)
    mm.handle_scp()
    mm.handle_scp()
    e.wait()

    # If we change the block size, the number of blocks should be changed
    # accordingly
    e = Event()
    c.scp_data_length = 5
    c.write(0, 0, 0, 0xDEADBEEF, b"ABCDEFGHIJKLMNOPQRST",
            on_success=e.set)
    mm.handle_scp()
    mm.handle_scp()
    mm.handle_scp()
    mm.handle_scp()
    e.wait()


def test_n_outstanding(mm, c):
    # Check that the value provided in the constructor is used
    c.n_outstanding == 1

    # Should timeout having only sent the first packet
    e = Event()
    c.write(0, 0, 0, 0xDEADBEEF, b"ABCDEFGHIJKLMNOPQRST",
            on_success=e.set, on_error=(lambda _: e.set()), timeout=0.01)
    e.wait()
    mm.recv_scp()
    with pytest.raises(Exception):
        print(">>>>", mm.recv_scp())

    # Increasing n_outstanding, should timeout having sent both writes
    e = Event()
    c.n_outstanding = 2
    c.write(0, 0, 0, 0xDEADBEEF, b"ABCDEFGHIJKLMNOPQRST",
            on_success=e.set, on_error=(lambda _: e.set()), timeout=0.01)
    e.wait()
    mm.recv_scp()
    mm.recv_scp()
