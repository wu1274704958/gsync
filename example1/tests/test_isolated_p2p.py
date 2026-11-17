import ctypes as C, os, time, subprocess, sys, tempfile
from pathlib import Path
from pathlib import Path
dll_path, lobby_exe, tests_dir, peer_config = map(Path, sys.argv[1:5])
b = dll_path.parent
if os.name == 'nt': os.add_dll_directory(str(b))
d=C.CDLL(str(dll_path))
tmp = tempfile.TemporaryDirectory(prefix='gsync-isolated-')
tmp_path = Path(tmp.name)
config = (tests_dir / 'test_connect_server' / 'conf.txt').read_text().replace('port=8102', 'port=0')
config = config.replace('[engine_config]', '[engine_config]\nserver_cert_sha256="7ca282f762eaaa3b7582e858b3c74fbec7ee421bc3448f7a6fb084e27e49a1ef"')
config_path = tmp_path / 'client.toml'; config_path.write_text(config)
lobby_server = subprocess.Popen([str(lobby_exe), 'server', '30000'], cwd=tmp_path, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
    creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
time.sleep(1)
U=C.c_uint32; S=C.c_uint64; I=C.c_int; P=C.c_void_p
ERR=C.CFUNCTYPE(None,C.c_char_p,I); IO=C.CFUNCTYPE(None,I,U); SC=C.CFUNCTYPE(None,U,S,I)
class GC(C.Structure): _fields_=[('error', ERR)]
class CC(C.Structure): _fields_=[('connect',IO),('disconnect',IO),('error',IO),('open',SC),('close',SC),('extend',P)]
REG=C.CFUNCTYPE(None,I,S,U)
class PeerData(C.Structure): _fields_=[('id',U),('name',C.c_char_p)]
REQ=C.CFUNCTYPE(None,C.POINTER(PeerData),S)
class Addr(C.Structure): _fields_=[('ip',C.c_char*40),('port',C.c_uint16)]
class Result(C.Structure): _fields_=[('ret',I),('peer',U),('server',I),('peer_addr',Addr),('reason',C.c_char_p),('address',Addr),('relay_addr',Addr),('use_relay',I),('relay_token',C.c_char_p),('session',C.c_uint64),('verify_token',P),('verify_len',C.c_size_t)]
HELP=C.CFUNCTYPE(None,S,C.POINTER(Result))
PCON=C.CFUNCTYPE(None,S,U,P); PSTR=C.CFUNCTYPE(None,S,U,S,P); PTERM=C.CFUNCTYPE(None,S,I,P)
class PeerCtx(C.Structure): _fields_=[('connected',PCON),('stream',PSTR),('failed',PTERM),('closed',PTERM),('user',P)]
DCON=C.CFUNCTYPE(None,I,S,U,C.c_char_p,U); DRECV=C.CFUNCTYPE(None,P,C.c_size_t,S,U,U); DFAIL=C.CFUNCTYPE(None,I,S,U,U)
DEND=C.CFUNCTYPE(None,I,S,U,C.c_char_p); DERROR=C.CFUNCTYPE(None,U,S,I,C.c_char_p,U)
class DCtx(C.Structure): _fields_=[('connected',DCON),('recv',DRECV),('disconnected',DEND),('failed',DFAIL),('error',DERROR),('self_id',U),('token',P),('token_len',I),('relay',I),('server',I),('extend',P),('check',C.c_uint8)]
CREATED=C.CFUNCTYPE(None,I,S,U,P)
class LC(C.Structure): _fields_=[('reg',REG),('unreg',P),('respond',P),('list',P),('request',P),('error',P),('change',P),('attempt',P),('quit',P),('extend',P),('check',C.c_uint8)]
refs=[]; connected={}; registered={}; incoming={}; errors={}; results={}; peerconn={}; peerstreams={}; datagram={}; received=[]; peerc=[]; dc=[]
@ERR
def err(msg,code): print('error',code,flush=True)
@IO
def con(code,h): connected[h]=code
@IO
def dis(code,h): pass
@IO
def failure(code,h): errors[h]=code
@SC
def stream(h,s,c): pass
@REG
def reg(code,s,p): registered[s]=(code,p)
@REQ
def request(peer,sid): incoming[sid]=peer.contents.id
@HELP
def helper(sid,result): results[sid]=Result.from_buffer_copy(C.string_at(result,C.sizeof(Result)))
@PCON
def pconnected(h,conn,user): peerconn[int(user)]=conn
@PSTR
def pstream(h,conn,sid,user):
 index=int(user); peerstreams[sid]=index
 if dc[index-1].server:
  code=d.GSY_ServerConnectPeerDatagram(conn,sid,C.byref(dc[index-1]),4)
  if code: print('server stream error',code,flush=True)
@PTERM
def pterm(h,code,user): print('peer terminal',int(user),code,flush=True)
@DCON
def dconnected(code,sid,peer,name,rid): datagram[sid]=(code,peer)
@DRECV
def dreceived(ptr,size,sid,peer,conn): received.append(C.string_at(ptr,size))
@DEND
def dend(code,sid,peer,reason): pass
@DFAIL
def dfail(code,sid,peer,rid): print('datagram failed',code,flush=True)
@DERROR
def derror(conn,sid,code,msg,rid): print('datagram error',code,flush=True)
@CREATED
def created(code,sid,rid,user): peerstreams[sid]=int(user)
refs.extend([err,con,dis,failure,stream,reg,request,helper,pconnected,pstream,pterm,dconnected,dreceived,dend,dfail,derror,created]); gc=GC(err); contexts=[CC(con,dis,failure,stream,stream,None),CC(con,dis,failure,stream,stream,None)]; lc=[LC() for x in range(2)]
for c in lc: c.reg=reg; c.request=C.cast(request,P); c.quit=C.cast(helper,P)
d.GSY_initialize.argtypes=[I,C.POINTER(GC)];d.GSY_connect.argtypes=[U,C.c_char_p,C.c_char_p,C.c_short,C.POINTER(CC)];d.GSY_connect.restype=U
d.GSY_RegisterToLobby.argtypes=[U,C.c_char_p,C.c_char_p,C.POINTER(LC)];d.GSY_RegisterToLobby.restype=S
d.GSY_disconnect.argtypes=[U]; d.GSY_terminate.restype=I
assert d.GSY_initialize(0,C.byref(gc))==0
try:
 path=os.fsencode(config_path)
 handles=[d.GSY_connect(1,path,b'127.0.0.1',8101,C.byref(contexts[i])) for i in range(2)]
 assert all(handles) and handles[0] != handles[1], handles
 for _ in range(600):
  if len(connected)==2: break
  time.sleep(.01)
 assert len(connected)==2 and all(x==0 for x in connected.values()), connected
 print('handles',handles,flush=True)
 sids=[d.GSY_RegisterToLobby(handles[i],('isolated_'+str(os.getpid())+'_'+str(i)).encode(),b'',C.byref(lc[i])) for i in range(2)]
 for _ in range(600):
  if len(registered)==2: break
  time.sleep(.01)
 print('registered',registered,flush=True)
 assert all(sids) and len(registered)==2 and all(x[0]==0 for x in registered.values()),sids
 d.GSY_RequestConnectPeer.argtypes=[U,S,U,U]; d.GSY_RespondPeerConnectRequest.argtypes=[U,S,U,C.c_bool,U]
 assert d.GSY_RequestConnectPeer(handles[0],sids[0],registered[sids[1]][1],1)==2
 for _ in range(800):
  if sids[1] in incoming: break
  time.sleep(.01)
 assert sids[1] in incoming, incoming
 assert d.GSY_RespondPeerConnectRequest(handles[1],sids[1],incoming[sids[1]],True,2)==2
 for _ in range(1000):
  if len(results)==2: break
  time.sleep(.01)
 print('helper',[(sid,r.ret,r.peer,r.server,r.peer_addr.port) for sid,r in results.items()],flush=True)
 assert len(results)==2 and all(r.ret==0 for r in results.values()),results
 assert results[sids[0]].peer_addr.port != results[sids[1]].peer_addr.port
 d.GSY_StartP2PPeer.argtypes=[C.c_char_p,U,C.POINTER(Result),C.POINTER(PeerCtx),C.POINTER(S)]
 d.GSY_StartP2PPeer.restype=I; d.GSY_ConnectPeerDatagram.argtypes=[U,U,C.POINTER(DCtx),U,CREATED,P]
 d.GSY_ServerConnectPeerDatagram.argtypes=[U,S,C.POINTER(DCtx),U]
 d.GSY_SendDatagram.argtypes=[U,S,C.c_char_p,C.c_size_t,U]; d.GSY_CloseP2PPeer.argtypes=[S]
 peer_handles=[S(),S()]
 cfg=os.fsencode(peer_config)
 for i in range(2):
  peer=PeerCtx(pconnected,pstream,pterm,pterm,i+1); peerc.append(peer)
  data=DCtx(dconnected,dreceived,dend,dfail,derror,registered[sids[i]][1],None,0,0,results[sids[i]].server,None,0); dc.append(data)
  assert d.GSY_StartP2PPeer(cfg,registered[sids[i]][1],C.byref(results[sids[i]]),C.byref(peer),C.byref(peer_handles[i]))==2
 for _ in range(1000):
  if len(peerconn)==2: break
  time.sleep(.01)
 print('peerconn',peerconn,flush=True); assert len(peerconn)==2
 client=next(i for i in range(2) if not results[sids[i]].server); server=1-client
 assert d.GSY_ConnectPeerDatagram(peerconn[client+1],results[sids[client]].peer,C.byref(dc[client]),3,created,client+1)==2
 accepted=set()
 for _ in range(1000):
  if len(datagram)==2: break
  time.sleep(.01)
 print('datagram',datagram,flush=True); assert len(datagram)==2,datagram
 client_sid=next(sid for sid,index in peerstreams.items() if index==client+1)
 assert d.GSY_SendDatagram(peerconn[client+1],client_sid,b'hello',5,5)==2
 for _ in range(500):
  if b'hello' in received: break
  time.sleep(.01)
 assert b'hello' in received,received
 print('PASS: authenticated P2P datagram exchanged in one process',flush=True)
 for h in peer_handles: d.GSY_CloseP2PPeer(h)
 time.sleep(.2)
 failed_handle=d.GSY_connect(1,os.fsencode(tmp_path/'missing.toml'),b'127.0.0.1',8101,C.byref(contexts[0]))
 assert failed_handle and failed_handle not in handles
 for _ in range(500):
  if failed_handle in errors: break
  time.sleep(.01)
 assert failed_handle in errors, errors
 assert d.GSY_is_connected(handles[1])==1
 assert d.GSY_disconnect(handles[0])==0
 time.sleep(.25)
 assert d.GSY_is_connected(handles[1])==1
 print('PASS: two isolated connections and closing first preserves second',flush=True)
finally:
 print('terminate',d.GSY_terminate(),flush=True)
 lobby_server.terminate(); lobby_server.wait(timeout=5); tmp.cleanup()
