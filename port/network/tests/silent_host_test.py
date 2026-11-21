"""Authored pre-connection silent-host transport control.

A guest that is never polled sends nothing while it waits; destroying it sends exactly one datagram, ENet's disconnect
notice (the close path notifies the peer that authentication was cancelled, cf1ce6f1). The driver says when it is about to
be destroyed and waits for the test, so a datagram is counted before or after close by when it was sent, not by when it
was read. A guest polled once (poll-once) is the negative control: its connection request must be seen before close.
"""
import json, os, select, socket, struct, subprocess, sys, time
from pathlib import Path

def _kill_owned_peer(process, executable):
    if process.poll() is not None:
        return
    try:
        data = (Path('/proc') / str(process.pid) / 'cmdline').read_bytes().split(b'\x00')
    except OSError:
        if process.poll() is not None:
            return
        raise RuntimeError('cannot verify test peer identity')
    if not data or not data[0] or Path(os.fsdecode(data[0])).resolve() != Path(executable).resolve():
        if process.poll() is not None:
            return
        raise RuntimeError('test peer identity mismatch')
    process.kill()

def _drain_pending_datagrams(sock):
    """Collect datagrams already in flight, with both time and count bounds."""
    deadline = time.monotonic() + 0.2
    count = 0
    for _ in range(4096):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        ready, _, _ = select.select([sock], [], [], remaining)
        if not ready:
            break
        sock.recvfrom(65535)
        count += 1
    return count


def _verify_post_exit_drain():
    """Wait for a short-lived sender before checking its queued datagram."""
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        receiver.bind(("127.0.0.1", 0))
        sender_code = (
            "import socket,sys;"
            "s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);"
            "s.sendto(b'q',('127.0.0.1',int(sys.argv[1])));s.close()"
        )
        sender = subprocess.Popen(
            [sys.executable, "-c", sender_code, str(receiver.getsockname()[1])],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        try:
            sender.wait(timeout=3)
            assert sender.returncode == 0, "queued sender failed"
        finally:
            if sender.poll() is None:
                _kill_owned_peer(sender, sys.executable)
                sender.wait(timeout=3)
        assert _drain_pending_datagrams(receiver) == 1, "queued datagram not observed"
    finally:
        receiver.close()


# ENet's wire header (include/enet/protocol.h of the pinned ENet): no compression or checksum is enabled on this transport.
_HEADER_COMPRESSED, _HEADER_SENT_TIME = 1 << 14, 1 << 15
_COMMAND_MASK, _FLAG_UNSEQUENCED = 0x0F, 1 << 6
_COMMAND_CONNECT, _COMMAND_DISCONNECT = 2, 4


def classify(datagram):
    """'disconnect' when the datagram is one ENet disconnect notice and nothing else, else the first command's number
    (or 'malformed'). The notice is unsequenced, on channel 0xFF, with data 0."""
    if len(datagram) < 2:
        return 'malformed'
    header = struct.unpack('>H', datagram[:2])[0]
    if header & _HEADER_COMPRESSED:
        return 'malformed'
    body = datagram[4:] if header & _HEADER_SENT_TIME else datagram[2:]
    if len(body) < 4:
        return 'malformed'
    command, channel = body[0], body[1]
    if (command & _COMMAND_MASK) != _COMMAND_DISCONNECT:
        return command & _COMMAND_MASK
    if len(body) != 8 or not command & _FLAG_UNSEQUENCED or channel != 0xFF or struct.unpack('>I', body[4:8])[0] != 0:
        return 'malformed'
    return 'disconnect'


def _verify_classifier():
    """The classifier itself can tell a notice from anything else."""
    notice = bytes([0x0F, 0xFF, _COMMAND_DISCONNECT | _FLAG_UNSEQUENCED, 0xFF, 0, 0, 0, 0, 0, 0])
    timed = bytes([0x8F, 0xFF, 0x12, 0x34]) + notice[2:]
    assert classify(notice) == 'disconnect' and classify(timed) == 'disconnect', 'classifier refused a notice'
    assert classify(notice + b'\0') == 'malformed', 'classifier accepted a trailing command'
    assert classify(bytes([0x4F, 0xFF]) + notice[2:]) == 'malformed', 'classifier accepted a compressed datagram'
    assert classify(notice[:2] + bytes([_COMMAND_DISCONNECT, 0xFF]) + notice[4:]) == 'malformed', 'classifier accepted a sequenced notice'
    assert classify(notice[:2] + bytes([_COMMAND_CONNECT | 0x80, 0xFF]) + bytes(40)) == _COMMAND_CONNECT, 'classifier missed a connection request'
    assert classify(b'\0') == 'malformed', 'classifier accepted a short datagram'


def _drain(sock, window=0.2):
    """The datagrams already in flight, within time and count bounds."""
    deadline = time.monotonic() + window
    out = []
    for _ in range(4096):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        ready, _, _ = select.select([sock], [], [], remaining)
        if not ready:
            break
        out.append(sock.recvfrom(65535)[0])
    return out


def _run_close(executable, mode):
    """Run a guest that is destroyed through the close handshake: (its result, datagrams sent before close, after)."""
    sink = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sink.bind(('127.0.0.1', 0))
    process = subprocess.Popen([str(executable), str(sink.getsockname()[1]), mode], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    before, after, text = [], [], b''
    try:
        deadline = time.monotonic() + 9
        out = process.stdout.fileno()
        for _iteration in range(4096):
            if b'closing\n' in text or time.monotonic() >= deadline:
                break
            ready, _, _ = select.select([sink, out], [], [], 0.1)
            if sink in ready:
                before.append(sink.recvfrom(65535)[0])
            if out in ready:
                chunk = os.read(out, 4096)
                if not chunk:
                    break
                text += chunk
        if b'closing\n' not in text:
            raise AssertionError('driver did not reach close, exit=' + str(process.poll()))
        before += _drain(sink)          # everything sent before the driver said it was closing
        process.stdin.write(b'c')
        process.stdin.close()
        process.wait(timeout=5)
        if process.returncode:
            raise AssertionError('driver failed, exit=' + str(process.returncode))
        after = _drain(sink)
        return json.loads(text.split(b'\n', 1)[0]), before, after
    finally:
        sink.close()
        if process.poll() is None:
            _kill_owned_peer(process, executable)
            process.wait(timeout=1)
        process.stdout.close()
        if not process.stdin.closed:
            process.stdin.close()


def verify_silent_host(executable):

    def run(mode):
        sink = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sink.bind(('127.0.0.1', 0))
        process = subprocess.Popen([str(executable), str(sink.getsockname()[1]), mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.monotonic() + 9
        packets = 0
        try:
            for _iteration in range(4096):
                if not (process.poll() is None and time.monotonic() < deadline):
                    break
                ready, _, _ = select.select([sink], [], [], 0.1)
                if ready:
                    sink.recvfrom(65535)
                    packets += 1
            if process.poll() is None:
                _kill_owned_peer(process, executable)
                raise AssertionError('bounded test timed out')
            stdout, stderr = process.communicate(timeout=1)
            if process.returncode:
                raise AssertionError('driver failed, exit=' + str(process.returncode))
            result = json.loads(stdout)
            packets += _drain_pending_datagrams(sink)
            return (result, packets)
        finally:
            sink.close()
            if process.poll() is None:
                _kill_owned_peer(process, executable)
                process.wait(timeout=1)
    _verify_post_exit_drain()
    normal, received = run('poll')
    if not received > 0:
        raise AssertionError('control-1')
    if not (normal['failed'] and normal['auth_class']):
        raise AssertionError('control-2')
    if not 5000 <= normal['elapsed_ms'] < 7000:
        raise AssertionError('control-3')
    _verify_classifier()
    negative, before, after = _run_close(executable, 'no-poll')
    if not (not negative['failed'] and (not negative['auth_class'])):
        raise AssertionError('control-4')
    # Nothing before close, and the close sends the disconnect notice and nothing else.
    if before:
        raise AssertionError('control-5: %d datagram(s) before close' % len(before))
    if [classify(d) for d in after] != ['disconnect']:
        raise AssertionError('control-6: after close %r' % [classify(d) for d in after])
    # The negative control: a guest polled once sends its connection request before close, and the window sees it.
    _, early, late = _run_close(executable, 'poll-once')
    if not early or classify(early[0]) != _COMMAND_CONNECT:
        raise AssertionError('control-7: before close %r' % [classify(d) for d in early])
    if [classify(d) for d in late] != ['disconnect']:
        raise AssertionError('control-8: after close %r' % [classify(d) for d in late])
