"""High-level integration tests for Rig SCP bindings.

Basically a synchronous re-write of examples/hello.c
"""
import rig_scp


def test_rig_scp():
    # Create a new SCP Connection, using the default values for timeout, number
    # of retries and window size (number of packets that may be outstanding).
    conn = rig_scp.SCPConnection("google.com")

    for x, y, p in [(0, 0, 0), (1, 0, 5)]:
        # Use this new connection to get the software version of the ethernet
        # connected chip (x, y), there are no arguments.
        sver_packet = conn.send_scp(
            x,  # x co-ordinate of target chip
            y,  # y co-ordinate of target chip
            p,  # virtual core number
            0  # command = 0 "SVER",
        )

        # The returned packet should look like a Rig SCP packet, for the most
        # part, we unpack it to check that the response is loosely what we'd
        # expect.
        assert sver_packet.arg1 & 0xff000000 == x << 24
        assert sver_packet.arg1 & 0x00ff0000 == y << 16
        assert sver_packet.arg1 & 0x000000ff == p

        assert ((sver_packet.arg2 & 0xffff0000) >> 16) / 100.0 >= 1.31  # vrsn

        assert b"SpiNNaker" in sver.data
