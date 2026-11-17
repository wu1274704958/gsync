import ctypes as c
import os
from pathlib import Path
import socket
import sys
import tempfile
import time

root = Path(sys.argv[1]); dll = Path(sys.argv[2])
os.add_dll_directory(str(dll.parent)); lib = c.CDLL(str(dll))
U = c.c_uint32; I = c.c_int; P = c.c_void_p
ERR = c.CFUNCTYPE(None, c.c_char_p, I); EVENT = c.CFUNCTYPE(None, I, U)
START = c.CFUNCTYPE(None, I, U); DIS = c.CFUNCTYPE(None, U, U); ACCEPT = c.CFUNCTYPE(P, U, U)
STREAM = c.CFUNCTYPE(None, U, c.c_uint64, I)
class Global(c.Structure): _fields_ = [('error', ERR)]
class Connection(c.Structure): _fields_ = [('connect', EVENT), ('disconnect', EVENT), ('error', EVENT), ('open', STREAM), ('close', STREAM), ('extra', P)]
class Server(c.Structure): _fields_ = [('started', START), ('stopped', START), ('error', EVENT), ('accept', ACCEPT), ('disconnect', DIS), ('extra', P)]
lib.GSY_initialize.argtypes = [I, c.POINTER(Global)]; lib.GSY_initialize.restype = I
lib.GSY_launch_server.argtypes = [U, c.c_char_p, c.POINTER(Server)]; lib.GSY_launch_server.restype = U
lib.GSY_stop_server.argtypes = [U]; lib.GSY_stop_server.restype = I
lib.GSY_connect.argtypes = [U, c.c_char_p, c.c_char_p, c.c_short, c.POINTER(Connection)]; lib.GSY_connect.restype = U
lib.GSY_disconnect.argtypes = [U]; lib.GSY_disconnect.restype = I
lib.GSY_terminate.restype = I
lib.GSY_test_server_engine.argtypes = [U]; lib.GSY_test_server_engine.restype = c.c_size_t
lib.GSY_test_server_port.argtypes = [U]; lib.GSY_test_server_port.restype = c.c_uint16
lib.GSY_test_server_engine_count.restype = c.c_size_t
keep = []; events = []
def cb(t, f):
    x = t(f); keep.append(x); return x
def wait(pred, desc, timeout=10):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        if pred(): return
        time.sleep(.01)
    raise AssertionError(f'{desc}; events={events}')

global_ctx = Global(cb(ERR, lambda msg, code: events.append(('global',code))))
client = Connection(); client.connect = cb(EVENT, lambda e,h: events.append(('client_connected', e, h)))
client.disconnect = cb(EVENT, lambda e,h: events.append(('client_disconnected', e, h)))
server_conn = Connection()
def server_ctx(label, accept):
    obj = Server()
    obj.started = cb(START, lambda e,h: events.append((label, 'started', e, h)))
    obj.stopped = cb(START, lambda e,h: events.append((label, 'stopped', e, h)))
    obj.error = cb(EVENT, lambda e,h: events.append((label, 'error', e, h)))
    obj.disconnect = cb(DIS, lambda s,h: events.append((label, 'disconnect', s, h)))
    def incoming(s,h):
        events.append((label, 'accept', s, h))
        return c.addressof(server_conn) if accept else None
    obj.accept = cb(ACCEPT, incoming); return obj

def run():
    sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM); sock.bind(('127.0.0.1',0)); port = sock.getsockname()[1]; sock.close()
    cert = (root/'mqas/certificate/cert-def.pem').as_posix(); key = (root/'mqas/certificate/key-def.pem').as_posix()
    with tempfile.TemporaryDirectory(prefix='server-reuse-') as tmp:
        tmp = Path(tmp)
        common = f'[engine_config]\nbind_ip="127.0.0.1"\nport={port}\nalpn="echo"\nssl_cert_path="{cert}"\nssl_key_path="{key}"\n'
        (tmp/'server.toml').write_text(common + '\n[lsquic_settings]\nes_datagrams=1\n')
        (tmp/'mismatch.toml').write_text(common + '\n[lsquic_settings]\nes_datagrams=0\n')
        (tmp/'client.toml').write_text('[engine_config]\nbind_ip="127.0.0.1"\nport=0\nalpn="echo"\n[lsquic_settings]\nes_datagrams=1\n')
        assert lib.GSY_initialize(0,c.byref(global_ctx)) == 0
        a,b,bad=server_ctx('a',False),server_ctx('b',True),server_ctx('bad',True)
        path=str(tmp/'server.toml').encode(); ah=lib.GSY_launch_server(2,path,c.byref(a)); bh=lib.GSY_launch_server(2,path,c.byref(b))
        assert ah and bh and ah!=bh
        wait(lambda: sum(1 for e in events if len(e)>1 and e[1]=='started')==2, 'both servers started')
        shared_engine = lib.GSY_test_server_engine(ah)
        assert shared_engine and shared_engine == lib.GSY_test_server_engine(bh), 'compatible fixed endpoints did not reuse the same engine'
        assert lib.GSY_test_server_engine_count() == 1
        # Per-server business and log options do not affect engine compatibility.
        variant_path = tmp/'variant.toml'
        variant_path.write_text((tmp/'server.toml').read_text().replace('alpn="echo"', 'log_level="info"\nalpn="echo"') + '\n[p2p]\nname="another-server"\n')
        variant = server_ctx('variant', False)
        vh = lib.GSY_launch_server(2, str(variant_path).encode(), c.byref(variant)); assert vh
        wait(lambda: any(e[:2] == ('variant', 'started') for e in events), 'business-config variant started')
        assert lib.GSY_test_server_engine(vh) == shared_engine and lib.GSY_test_server_engine_count() == 1
        assert lib.GSY_stop_server(vh) == 0
        wait(lambda: any(e[:2] == ('variant', 'stopped') for e in events), 'business-config variant stopped')

        # Different engine type and a wildcard-overlapping endpoint cannot reuse.
        conflict = server_ctx('conflict', False)
        wildcard = tmp/'wildcard.toml'
        wildcard.write_text((tmp/'server.toml').read_text().replace('127.0.0.1', '0.0.0.0'))
        assert lib.GSY_launch_server(1, str(wildcard).encode(), c.byref(conflict))
        wait(lambda: any(e[:3] == ('conflict', 'error', 1200) for e in events), 'overlapping endpoint rejected')
        assert lib.GSY_test_server_engine_count() == 1

        # Distinct fixed ports and port=0 must create separate live engines.
        second_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        second_sock.bind(('127.0.0.1', 0)); other_port = second_sock.getsockname()[1]; second_sock.close()
        while other_port == port:
            second_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            second_sock.bind(('127.0.0.1', 0)); other_port = second_sock.getsockname()[1]; second_sock.close()
        other_config = tmp/'other.toml'; other_config.write_text(common.replace(f'port={port}', f'port={other_port}') + '\n[lsquic_settings]\nes_datagrams=1\n')
        zero_config = tmp/'zero.toml'; zero_config.write_text(common.replace(f'port={port}', 'port=0') + '\n[lsquic_settings]\nes_datagrams=1\n')
        other, zero1, zero2 = server_ctx('other', False), server_ctx('zero1', False), server_ctx('zero2', False)
        oh = lib.GSY_launch_server(2, str(other_config).encode(), c.byref(other))
        zh1 = lib.GSY_launch_server(2, str(zero_config).encode(), c.byref(zero1))
        zh2 = lib.GSY_launch_server(2, str(zero_config).encode(), c.byref(zero2))
        assert oh and zh1 and zh2
        for label in ('other', 'zero1', 'zero2'):
            wait(lambda label=label: any(e[:2] == (label, 'started') for e in events), f'{label} started')
        identities = [shared_engine] + [lib.GSY_test_server_engine(h) for h in (oh, zh1, zh2)]
        assert all(identities) and len(set(identities)) == 4, f'expected four distinct engines: {identities}'
        assigned = [lib.GSY_test_server_port(h) for h in (ah, oh, zh1, zh2)]
        assert all(assigned) and len(set(assigned)) == 4, f'expected distinct actual ports: {assigned}'
        assert assigned[:2] == [port, other_port]
        assert lib.GSY_test_server_engine_count() == 4
        for label, handle in (('other', oh), ('zero1', zh1), ('zero2', zh2)):
            assert lib.GSY_stop_server(handle) == 0
            wait(lambda label=label: any(e[:2] == (label, 'stopped') for e in events), f'{label} stopped')
        wait(lambda: lib.GSY_test_server_engine_count() == 1, 'independent engines released')
        assert lib.GSY_test_server_engine(ah) == shared_engine == lib.GSY_test_server_engine(bh)

        lib.GSY_launch_server(2,str(tmp/'mismatch.toml').encode(),c.byref(bad))
        wait(lambda: any(e[:3]==('bad','error',1300) for e in events), 'incompatible config rejected')
        bad_alpn_path = tmp/'bad-alpn.toml'
        bad_alpn_path.write_text((tmp/'server.toml').read_text().replace('alpn="echo"', 'alpn="other"'))
        bad_alpn = server_ctx('bad-alpn', False)
        assert lib.GSY_launch_server(2, str(bad_alpn_path).encode(), c.byref(bad_alpn))
        wait(lambda: any(e[:3] == ('bad-alpn', 'error', 1300) for e in events), 'incompatible ALPN rejected')
        assert lib.GSY_test_server_engine_count() == 1
        ch=lib.GSY_connect(2,str(tmp/'client.toml').encode(),b'127.0.0.1',port,c.byref(client)); assert ch
        wait(lambda: any(e[:2]==('b','accept') for e in events), 'connection routed to second server')
        wait(lambda: any(e[0]=='client_connected' and e[1]==0 for e in events), 'client connected')
        assert any(e[:2]==('a','accept') for e in events)
        assert lib.GSY_stop_server(ah)==0
        wait(lambda: any(e[:2]==('a','stopped') for e in events), 'first server stopped')
        assert not any(e[:2]==('b','disconnect') for e in events), 'other server connection was closed'
        assert lib.GSY_stop_server(bh)==0
        wait(lambda: any(e[:2]==('b','disconnect') for e in events), 'owned connection closed')
        wait(lambda: any(e[:2]==('b','stopped') for e in events), 'second server stopped')
        wait(lambda: lib.GSY_test_server_engine_count() == 0, 'last shared engine released')
        assert lib.GSY_test_server_engine(ah) == 0 and lib.GSY_test_server_engine(bh) == 0
        restarted = server_ctx('restarted', False)
        rh = lib.GSY_launch_server(2, path, c.byref(restarted)); assert rh
        wait(lambda: any(e[:2] == ('restarted', 'started') for e in events), 'server restarted after release')
        assert lib.GSY_test_server_engine(rh) and lib.GSY_test_server_engine_count() == 1
        assert lib.GSY_stop_server(rh) == 0
        wait(lambda: any(e[:2] == ('restarted', 'stopped') for e in events), 'restarted server stopped')
        wait(lambda: lib.GSY_test_server_engine_count() == 0, 'restarted engine released')
        assert lib.GSY_terminate()==0
        print('shared server engine: PASS', flush=True)
if __name__=='__main__': run()
