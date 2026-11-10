"""Authored pre-connection silent-host transport control."""

"""Authored pre-connection silent-host transport control."""
import json, os, select, socket, subprocess, sys, time
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
    negative, negative_received = run('no-poll')
    if not (not negative['failed'] and (not negative['auth_class'])):
        raise AssertionError('control-4')
    if not negative_received == 0:
        raise AssertionError('control-5')
