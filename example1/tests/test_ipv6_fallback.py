"""Two SDK processes, an IPv4-only lobby, isolated IPv4 peer paths and real IPv6 UDP."""
import ctypes as C
import multiprocessing as mp
import os
from pathlib import Path
import selectors
import socket
import subprocess
import sys
import tempfile
import threading
import time

from p2p_abi import (ACCEPT, CC, CHANNEL_RESULT, CREATED, DCON, DCtx, DEND, DERROR,
                     DFAIL, DRECV, ERR, Extension, GC, HELP, I, IO, LC, P, POLICY,
                     READY, REG, REQ, S, SC, ServerCtx, ServerOptions, U)


def wait_for(fn):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if value := fn():
            return value
        time.sleep(.01)
    raise TimeoutError(fn)


def peer(dll, config, peer_config, port, pipe, initiator, directory, bind_failure):
    os.chdir(directory)
    if bind_failure and initiator:
        os.environ["MQAS_TEST_IPV6_LOOPBACK"] = "bind-failure"
    if os.name == "nt":
        os.add_dll_directory(str(dll.parent))
    sdk = C.CDLL(str(dll))
    events = {}
    recv = []
    def event(key):
        def record(*args):
            events[key] = args
        return record
    def api(name, result, *types):
        func = getattr(sdk, "GSY_" + name)
        func.restype, func.argtypes = result, types
        return func

    gc = GC(ERR(event("error")))
    ctx = CC(IO(event("lobby")), IO(), IO(event("failed")), SC(), SC(), None)
    lobby = LC()
    lobby.reg = REG(event("registered"))
    request = REQ(lambda data, sid: events.update(incoming=data.contents.id))
    def helper(sid, result):
        r = result.contents
        events["result"] = (r.ret, r.session, r.server, r.address.ip, r.peer_addr.ip)
    quit_cb = HELP(helper)
    lobby.request, lobby.quit = C.cast(request, P), C.cast(quit_cb, P)
    lobby.ready = READY(event("ready"))
    assert api("initialize", I, I, C.POINTER(GC))(0, C.byref(gc)) == 0
    try:
        handle = api("connect", U, U, C.c_char_p, C.c_char_p, C.c_short, C.POINTER(CC))(
            1, os.fsencode(config), b"127.0.0.1", port, C.byref(ctx))
        assert wait_for(lambda: events.get("lobby")) == (0, handle)
        sid = api("RegisterToLobby", S, U, C.c_char_p, C.c_char_p, C.POINTER(LC))(
            handle, str(os.getpid()).encode(), b"", C.byref(lobby))
        code, _, self_id = wait_for(lambda: events.get("registered"))
        assert code == 0
        pipe.send(self_id)
        other = pipe.recv()
        if initiator:
            assert api("RequestConnectPeer", I, U, S, U, U)(handle, sid, other, 1) == 2
        else:
            assert wait_for(lambda: events.get("incoming")) == other
            assert api("RespondPeerConnectRequest", I, U, S, U, C.c_bool, U)(handle, sid, other, True, 2) == 2
        code, session, server, local_ip, remote_ip = wait_for(lambda: events.get("result"))
        expected = b"127.0.0.1" if bind_failure else b"::1"
        assert (code, local_ip, remote_ip) == (0, expected, expected)
        dg = DCtx()
        dg.connected = DCON(event("stream"))
        dg.recv = DRECV(lambda buf, size, *_: recv.append(C.string_at(buf, size)))
        dg.failed, dg.disconnected, dg.error = DFAIL(event("failed")), DEND(), DERROR(event("error"))
        dg.self_id, dg.server = self_id, server
        channel = CHANNEL_RESULT(event("channel"))
        dg.channel_result = C.cast(channel, P)
        accepted = api("ServerConnectPeerDatagram", I, U, S, C.POINTER(DCtx), U)
        def incoming_stream(h, stream, code):
            events["accept_stream"] = accepted(h, stream, C.byref(dg), 4)
        direct = CC(IO(event("connected")), IO(), IO(event("failed")), SC(incoming_stream) if server else SC(), SC(), None)
        if server:
            sc = ServerCtx()
            sc.started, sc.error = IO(event("started")), IO(event("failed"))
            sc.accept = ACCEPT(lambda *_: C.addressof(direct))
            def policy(h, c, s, remote, local, reason):
                local[0] = remote + 1
                return 1
            options = ServerOptions(Extension(3, C.sizeof(ServerOptions)), session, POLICY(policy))
            sc.options = C.addressof(options)
            listener = api("launch_server", U, U, C.c_char_p, C.POINTER(ServerCtx))(2, os.fsencode(peer_config), C.byref(sc))
            assert wait_for(lambda: events.get("started")) == (0, listener)
            assert api("RegisterServerP2PSession", I, U, S)(listener, session) == 0
            time.sleep(.15)
            assert api("NotifyP2PServerReady", I, U, S, S)(handle, sid, session) == 2
        else:
            assert wait_for(lambda: events.get("ready")) == (sid, other, session)
            h = U()
            assert api("connect_session", I, C.c_char_p, S, C.POINTER(CC), C.POINTER(U))(
                os.fsencode(peer_config), session, C.byref(direct), C.byref(h)) == 2
        code, h = wait_for(lambda: events.get("connected"))
        assert code == 0
        if not server:
            created = CREATED(event("created"))
            assert api("MakePeerDatagramStream", I, U, U, C.POINTER(DCtx), U, CREATED, P)(
                h, 11, C.byref(dg), 5, created, None) == 2
        code, stream, *_ = wait_for(lambda: events.get("stream"))
        assert code == 0
        if not server:
            assert api("OpenDatagramChannel", I, U, S, U)(h, stream, 6) == 2
            assert wait_for(lambda: events.get("channel"))[2] == 0
        send = api("SendDatagram", I, U, S, C.c_char_p, C.c_size_t, U)
        if server:
            wait_for(lambda: b"ping" in recv)
            # Datagram channels have independent send state in each direction.
            assert api("OpenDatagramChannel", I, U, S, U)(h, stream, 6) == 2
            assert wait_for(lambda: events.get("channel"))[2] == 0
            assert send(h, stream, b"pong", 4, 8) == 2
        else:
            assert send(h, stream, b"ping", 4, 7) == 2
            wait_for(lambda: b"pong" in recv)
        pipe.send("done")
        pipe.recv()
    finally:
        sdk.GSY_terminate()


def relay_ipv4(proxies, stop, lobby):
    # Each lobby sees the relay port. Drop peer probes arriving from the other SDK.
    with selectors.DefaultSelector() as selector:
        clients = {}
        for proxy in proxies:
            selector.register(proxy, selectors.EVENT_READ)
        while not stop.is_set():
            for key, _ in selector.select(.05):
                sock = key.fileobj
                try:
                    data, source = sock.recvfrom(65535)
                except ConnectionResetError:
                    # Winsock reports asynchronous ICMP when a peer closes its UDP port.
                    continue
                if source == lobby:
                    sock.sendto(data, clients[sock])
                elif source == clients.setdefault(sock, source):
                    sock.sendto(data, lobby)


def main():
    dll, exe, tests, peer_config = (Path(x).resolve() for x in sys.argv[1:5])
    bind_failure = "bind-failure" in sys.argv[5:]
    os.environ["MQAS_TEST_IPV6_LOOPBACK"] = "1"
    with tempfile.TemporaryDirectory() as root:
        root = Path(root)
        template = (tests / "test_connect_server/conf.txt").read_text()
        template = template.replace("port=8102", "port=0").replace("0.0.0.0", "127.0.0.1")
        pin = 'server_cert_sha256="7ca282f762eaaa3b7582e858b3c74fbec7ee421bc3448f7a6fb084e27e49a1ef"'
        client_config = root / "client.toml"
        client_config.write_text(template.replace("[engine_config]", "[engine_config]\n" + pin))
        proxy = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
        for p in proxy:
            p.bind(("127.0.0.1", 0))
        server_config = root / "server.toml"
        server_config.write_text((tests / "test_launch_server/conf.txt").read_text()
            .replace("../../../mqas/certificate", (tests / "../../mqas/certificate").resolve().as_posix()))
        server = subprocess.Popen([str(exe), "server", "40000", str(server_config)], cwd=root)
        stop = threading.Event()
        relay = threading.Thread(target=relay_ipv4, args=(proxy, stop, ("127.0.0.1", 8101)))
        relay.start()
        workers, pipes = [], []
        try:
            time.sleep(.6)
            ctx = mp.get_context("spawn")
            for i, p in enumerate(proxy):
                directory = root / str(i)
                directory.mkdir()
                parent, child = ctx.Pipe()
                worker = ctx.Process(target=peer, args=(dll, client_config, peer_config,
                    8101 if bind_failure else p.getsockname()[1], child, i == 0, directory, bind_failure))
                worker.start()
                child.close()
                workers.append(worker)
                pipes.append(parent)
            ids = [p.recv() for p in pipes]
            for p, other in zip(pipes, reversed(ids)):
                p.send(other)
            assert all(p.recv() == "done" for p in pipes)
            for p in pipes:
                p.send("stop")
            for worker in workers:
                worker.join(5)
                assert worker.exitcode == 0
        finally:
            for worker in workers:
                if worker.is_alive():
                    worker.terminate()
                worker.join()
            stop.set()
            relay.join()
            for p in proxy:
                p.close()
            server.terminate()
            server.wait()


if __name__ == "__main__":
    main()
