import socket

from rig.machine_control.packets import SCPPacket
from rig.machine_control.consts import SCPReturnCodes


class MockMachine(object):
    """A simple SCP-responder."""

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET,
                                  socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.1)
        self.sockname = self.sock.getsockname()

    def close(self):
        self.sock.close()

    def recv_scp(self):
        data, addr = self.sock.recvfrom(1024)
        pkt = SCPPacket.from_bytestring(data)
        return pkt, addr

    def send_scp(self, pkt, addr):
        self.sock.sendto(pkt.bytestring, addr)

    def respond_to(self, pkt, addr, cmd_rc=SCPReturnCodes.ok,
                   arg1=None, arg2=None, arg3=None, n_args=0,
                   data=b''):
        resp = SCPPacket(src_port=pkt.dest_port,
                         src_cpu=pkt.dest_cpu,
                         src_x=pkt.dest_x,
                         src_y=pkt.dest_y,
                         dest_port=pkt.src_port,
                         dest_cpu=pkt.src_cpu,
                         dest_x=pkt.src_x,
                         dest_y=pkt.src_y,
                         cmd_rc=cmd_rc,
                         seq=pkt.seq,
                         reply_expected=False,
                         arg1=arg1 or (1 if n_args >= 1 else None),
                         arg2=arg2 or (2 if n_args >= 2 else None),
                         arg3=arg3 or (3 if n_args >= 3 else None),
                         data=data)
        self.send_scp(resp, addr)
        return resp

    def handle_scp(self, *args, **kwargs):
        pkt, addr = self.recv_scp()
        resp = self.respond_to(pkt, addr, *args, **kwargs)
        return pkt, resp
