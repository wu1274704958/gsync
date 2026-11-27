import ctypes as C, os, time, subprocess, sys, tempfile
from pathlib import Path
dll_path, lobby_exe, tests_dir, peer_config = map(Path, sys.argv[1:5])
b = dll_path.parent
if os.name == 'nt': os.add_dll_directory(str(b))
d=C.CDLL(str(dll_path))
tmp = tempfile.TemporaryDirectory(prefix='gsync-isolated-')
tmp_path = Path(tmp.name)
config = (tests_dir / 'test_connect_server' / 'conf.txt').read_text().replace('port=8102', 'port=0')
config = config.replace('[engine_config]', '[engine_config]\nserver_cert_sha256="7ca282f762eaaa3b7582e858b3c74fbec7ee421bc3448f7a6fb084e27e49a1ef"')
ipv6 = 'ipv6' in sys.argv[5:]
host = b'::1' if ipv6 else b'127.0.0.1'
server_args = [str(lobby_exe), 'server', '30000']
config = config.replace('bind_ip="0.0.0.0"', f'bind_ip="{host.decode()}"')
server_text = (tests_dir/'test_launch_server'/'conf.txt').read_text()
server_text = server_text.replace('bind_ip="0.0.0.0"', f'bind_ip="{host.decode()}"')
certificate = (tests_dir/'../../mqas/certificate').resolve().as_posix()
server_text = server_text.replace('../../../mqas/certificate', certificate)
server_path = tmp_path/'server.toml'; server_path.write_text(server_text)
server_args.append(str(server_path))
config_path = tmp_path / 'client.toml'; config_path.write_text(config)
lobby_server = subprocess.Popen(server_args, cwd=tmp_path, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
    creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
time.sleep(1)
from p2p_abi import *
refs=[]; connected={}; registered={}; incoming={}; errors={}; results={}; peerconn={}; peerstreams={}; datagram={}; received=[]; deliveries=[]; stream_creation={}; channel_results={}; channel_requests=[]; rejected_channel_ids=set(); channel_reason=C.create_string_buffer(b"channel denied"); dc=[]
server_ready_events=[]
lobby_errors=[]
LOBBY_ERROR=C.CFUNCTYPE(None,U,S,I,C.c_char_p,U)
@LOBBY_ERROR
def lobby_error(conn,sid,code,msg,rid): lobby_errors.append((conn,sid,code,msg,rid))
@READY
def p2p_server_ready(sid,peer,session): server_ready_events.append((sid,peer,session))
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
def pstream(h,conn,sid,user):
 index=int(user); peerstreams[sid]=index
 if dc[index-1].server:
  code=d.GSY_ServerConnectPeerDatagram(conn,sid,C.byref(dc[index-1]),4)
  if code: print('server stream error',code,flush=True)
@DCON
def dconnected(code,sid,peer,name,rid): datagram[sid]=(code,peer)
@CHANNEL_RESULT
def channel_result(sid,opening,code,rid,reason):
 channel_results[rid]=(sid,opening,code,reason)
@CHANNEL_REQUEST
def channel_request(conn,sid,peer_id,reason):
 channel_requests.append((sid,peer_id))
 if peer_id==102 and peer_id not in rejected_channel_ids:
  rejected_channel_ids.add(peer_id)
  reason[0]=C.addressof(channel_reason)
  return 0
 return 1
@DRECV
def dreceived(ptr,size,sid,peer,conn):
 payload=C.string_at(ptr,size)
 received.append(payload)
 deliveries.append((sid,payload))
@DEND
def dend(code,sid,peer,reason): pass
@DFAIL
def dfail(code,sid,peer,rid): print('datagram failed',code,flush=True)
@DERROR
def derror(conn,sid,code,msg,rid): print('datagram error',code,flush=True)
@CREATED
def created(code,sid,rid,user):
 stream_creation[rid]=(code,sid)
 if sid: peerstreams[sid]=int(user)
refs.extend([err,con,dis,failure,stream,reg,request,helper,pstream,dconnected,dreceived,dend,dfail,derror,created,channel_result,channel_request]); gc=GC(err); contexts=[CC(con,dis,failure,stream,stream,None),CC(con,dis,failure,stream,stream,None)]; lc=[LC() for x in range(2)]
for c in lc: c.reg=reg; c.request=C.cast(request,P); c.quit=C.cast(helper,P); c.ready=p2p_server_ready; c.error=C.cast(lobby_error,P)
d.GSY_initialize.argtypes=[I,C.POINTER(GC)];d.GSY_connect.argtypes=[U,C.c_char_p,C.c_char_p,C.c_short,C.POINTER(CC)];d.GSY_connect.restype=U
d.GSY_RegisterToLobby.argtypes=[U,C.c_char_p,C.c_char_p,C.POINTER(LC)];d.GSY_RegisterToLobby.restype=S
d.GSY_disconnect.argtypes=[U]; d.GSY_terminate.restype=I
assert d.GSY_initialize(0,C.byref(gc))==0
try:
 path=os.fsencode(config_path)
 handles=[]
 for context in contexts:
  handle=d.GSY_connect(1,path,host,8101,C.byref(context))
  assert handle and handle not in handles, (handle,handles)
  handles.append(handle)
  for _ in range(600):
   if handle in connected or handle in errors: break
   time.sleep(.01)
  assert connected.get(handle)==0, (connected,errors)
 print('handles',handles,flush=True)
 sids=[d.GSY_RegisterToLobby(handles[i],('isolated_'+str(os.getpid())+'_'+str(i)).encode(),b'',C.byref(lc[i])) for i in range(2)]
 for _ in range(600):
  if len(registered)==2: break
  time.sleep(.01)
 print('registered',registered,flush=True)
 assert all(sids) and len(registered)==2 and all(x[0]==0 for x in registered.values()),sids
 d.GSY_RequestConnectPeer.argtypes=[U,S,U,U]; d.GSY_RespondPeerConnectRequest.argtypes=[U,S,U,C.c_bool,U]
 preference=os.environ.get('GSYNC_TEST_SERVER_PREFERENCE')
 if preference:
  first_pref, second_pref=map(int,preference.split(','))
  d.GSY_RequestConnectPeerWithPreference.argtypes=[U,S,U,I,U]
  d.GSY_RespondPeerConnectRequestWithPreference.argtypes=[U,S,U,C.c_bool,I,U]
  assert d.GSY_RequestConnectPeerWithPreference(handles[0],sids[0],registered[sids[1]][1],first_pref,1)==2
 else:
  assert d.GSY_RequestConnectPeer(handles[0],sids[0],registered[sids[1]][1],1)==2
 for _ in range(800):
  if sids[1] in incoming: break
  time.sleep(.01)
 assert sids[1] in incoming, incoming
 if preference:
  assert d.GSY_RespondPeerConnectRequestWithPreference(handles[1],sids[1],incoming[sids[1]],True,second_pref,2)==2
 else:
  assert d.GSY_RespondPeerConnectRequest(handles[1],sids[1],incoming[sids[1]],True,2)==2
 for _ in range(1000):
  if len(results)==2: break
  time.sleep(.01)
 print('helper',[(sid,r.ret,r.peer,r.server,r.peer_addr.port) for sid,r in results.items()],flush=True)
 assert len(results)==2 and all(r.ret==0 for r in results.values()),results
 if preference:
  expected=0 if first_pref>second_pref else 1 if second_pref>first_pref else 0 if first_pref else min(range(2),key=lambda i: registered[sids[i]][1])
  assert results[sids[expected]].server and not results[sids[1-expected]].server
 assert results[sids[0]].peer_addr.port != results[sids[1]].peer_addr.port
 d.GSY_MakePeerDatagramStream.argtypes=[U,U,C.POINTER(DCtx),U,CREATED,P]
 d.GSY_ConnectPeerDatagram.argtypes=[U,U,U,C.POINTER(DCtx),U,CREATED,P]
 d.GSY_ServerConnectPeerDatagram.argtypes=[U,S,C.POINTER(DCtx),U]
 d.GSY_OpenDatagramChannel.argtypes=[U,S,U]; d.GSY_CloseDatagramChannel.argtypes=[U,S,U]
 d.GSY_SendDatagram.argtypes=[U,S,C.c_char_p,C.c_size_t,U]
 cfg=os.fsencode(peer_config)
 client=next(i for i in range(2) if not results[sids[i]].server); server=1-client
 for i in range(2):
  data=DCtx(dconnected,dreceived,dend,dfail,derror,registered[sids[i]][1],None,0,0,results[sids[i]].server,None,0,None,C.cast(channel_request,P),C.cast(channel_result,P)); dc.append(data)
 started=[]; stopped=[]; rejected=[]; decisions=[]; server_connections=[]; active_connections=[]
 first_results=dict(results)
 incoming.clear(); results.clear()
 assert d.GSY_RequestConnectPeer(handles[0],sids[0],registered[sids[1]][1],10)==2
 for _ in range(400):
  if sids[1] in incoming: break
  time.sleep(.01)
 assert sids[1] in incoming, incoming
 assert d.GSY_RespondPeerConnectRequest(handles[1],sids[1],incoming[sids[1]],True,11)==2
 for _ in range(600):
  if len(results)==2: break
  time.sleep(.01)
 assert len(results)==2 and all(r.ret==0 for r in results.values()), results
 assert all(results[sid].server == first_results[sid].server for sid in sids)
 assert all(results[sid].peer_addr.port == first_results[sid].peer_addr.port for sid in sids)
 second_results=dict(results)
 results=first_results
 rejection=C.create_string_buffer(b'blocked by application')
 @REJECTED
 def on_rejected(sid,reason,rid): rejected.append((reason,rid))
 dc[client].rejected=C.cast(on_rejected,P)
 @IO
 def on_started(code,hwnd): started.append((code,hwnd))
 @IO
 def on_stopped(code,hwnd): stopped.append((code,hwnd))
 @IO
 def client_ready(code,handle):
  active_connections.append(handle)
  peerconn.setdefault(client+1,handle)
 @IO
 def server_ready(code,handle):
  server_connections.append(handle)
  peerconn.setdefault(server+1,handle)
 @SC
 def server_stream(handle,sid,code): pstream(0,handle,sid,server+1)
 accepted_owners={}
 @ACCEPT
 def accept(hwnd,handle):
  accepted_owners[handle]=hwnd
  return C.addressof(server_conn_ctx)
 @POLICY
 def accept_stream(hwnd,handle,sid,remote,local,reason):
  decisions.append(remote)
  if remote == 99:
   reason[0]=C.addressof(rejection)
   return 0
  local[0]=remote+100
  return 1
 server_conn_ctx=CC(server_ready,dis,failure,server_stream,stream,None)
 active_ctx=CC(client_ready,dis,failure,stream,stream,None)
 d.GSY_GetConnectionSocket.argtypes=[U,C.POINTER(S)]; d.GSY_ReleaseSocket.argtypes=[S]
 d.GSY_AreSameSocket.argtypes=[S,S,C.POINTER(I)]
 d.GSY_launch_server.argtypes=[U,C.c_char_p,C.POINTER(ServerCtx)]; d.GSY_launch_server.restype=U
 d.GSY_stop_server.argtypes=[U]; d.GSY_RegisterServerP2PSession.argtypes=[U,S]
 d.GSY_CancelServerP2PSession.argtypes=[U,S]
 d.GSY_NotifyP2PServerReady.argtypes=[U,S,S]
 d.GSY_connect_session.argtypes=[C.c_char_p,S,C.POINTER(CC),C.POINTER(U)]
 d.GSY_GetConnectionP2PPeerId.argtypes=[U,C.POINTER(U)]
 d.GSY_FindPeerStream.argtypes=[U,U,C.POINTER(S)]
 d.GSY_GetPeerStreamIds.argtypes=[U,S,C.POINTER(U),C.POINTER(U)]
 socket_handle=S()
 assert d.GSY_GetConnectionSocket(handles[server],C.byref(socket_handle))==0
 duplicate_socket=S(); other_socket=S(); same=I()
 assert d.GSY_GetConnectionSocket(handles[server],C.byref(duplicate_socket))==0
 assert duplicate_socket.value != socket_handle.value
 assert d.GSY_AreSameSocket(socket_handle,duplicate_socket,C.byref(same))==0 and same.value==1
 assert d.GSY_GetConnectionSocket(handles[client],C.byref(other_socket))==0
 assert d.GSY_AreSameSocket(socket_handle,other_socket,C.byref(same))==0 and same.value==0
 assert d.GSY_ReleaseSocket(other_socket)==0
 options=ServerOptions(Extension(2,C.sizeof(ServerOptions)),socket_handle.value,accept_stream)
 options2=ServerOptions(Extension(3,C.sizeof(ServerOptions)),second_results[sids[server]].session,accept_stream)
 sc=ServerCtx(); sc.started=on_started; sc.stopped=on_stopped; sc.error=failure; sc.accept=accept
 sc.options=C.addressof(options)
 sc2=ServerCtx.from_buffer_copy(sc); sc2.options=C.addressof(options2)
 sh=d.GSY_launch_server(2,cfg,C.byref(sc)); assert sh
 sh2=d.GSY_launch_server(2,cfg,C.byref(sc2)); assert sh2 and sh2!=sh
 for _ in range(400):
  if len(started)==2: break
  time.sleep(.01)
 assert started==[(0,sh),(0,sh2)], (started,errors)
 assert d.GSY_ReleaseSocket(duplicate_socket)==0
 assert d.GSY_AreSameSocket(socket_handle,duplicate_socket,C.byref(same))==1006
 assert d.GSY_ReleaseSocket(socket_handle)==0
 assert d.GSY_RegisterServerP2PSession(sh,results[sids[client]].session)==-1
 session=results[sids[server]].session
 assert d.GSY_NotifyP2PServerReady(handles[server],sids[server],session)==1012
 assert d.GSY_RegisterServerP2PSession(sh,session)==0
 assert d.GSY_RegisterServerP2PSession(sh,session)==1015
 assert d.GSY_CancelServerP2PSession(sh,session)==0
 if 'check_timeout' in sys.argv[5:]:
  assert d.GSY_RegisterServerP2PSession(sh,session)==0
  for _ in range(1300):
   if sh in errors: break
   time.sleep(.01)
  assert errors.pop(sh,None)==1014, errors
  assert d.GSY_CancelServerP2PSession(sh,session)==1006
  assert d.GSY_RegisterServerP2PSession(sh,session)==0
  assert d.GSY_CancelServerP2PSession(sh,session)==0
  assert d.GSY_stop_server(sh)==0
  assert d.GSY_stop_server(sh2)==0
  print("PASS: expired server session can be registered again", flush=True)
  sys.exit(0)
 else:
  assert d.GSY_RegisterServerP2PSession(sh,session)==0
 assert d.GSY_NotifyP2PServerReady(handles[client],sids[client],session)==1006
 assert d.GSY_NotifyP2PServerReady(handles[server],sids[server]+1,session)==1006
 assert d.GSY_NotifyP2PServerReady(handles[server],sids[server],session)==2
 for _ in range(400):
  if server_ready_events: break
  time.sleep(.01)
 assert server_ready_events==[(sids[client],registered[sids[server]][1],results[sids[client]].session)],(server_ready_events,lobby_errors,errors)
 assert d.GSY_NotifyP2PServerReady(handles[server],sids[server],session)==2
 time.sleep(.1)
 assert len(server_ready_events)==1,server_ready_events
 second_session=second_results[sids[client]].session
 # A failed startup and a cancelled handshake must both release the session for retry.
 failed=U()
 missing=os.fsencode(tmp_path/'missing-peer.toml')
 assert d.GSY_connect_session(missing,second_session,C.byref(active_ctx),C.byref(failed))==2
 for _ in range(400):
  if failed.value in errors: break
  time.sleep(.01)
 assert errors.pop(failed.value,None)==1007, errors
 retry=U()
 assert d.GSY_connect_session(cfg,second_session,C.byref(active_ctx),C.byref(retry))==2
 assert d.GSY_disconnect(retry.value)==0
 for _ in range(400):
  if retry.value in errors: break
  time.sleep(.01)
 assert errors.pop(retry.value,None)==1005, errors
 assert retry.value not in active_connections

 assert d.GSY_RegisterServerP2PSession(sh2,second_results[sids[server]].session)==0
 assert d.GSY_CancelServerP2PSession(sh,second_results[sids[server]].session)==1006
 active_options=ConnectOptions(Extension(1,C.sizeof(ConnectOptions)),results[sids[client]].session)
 active_ctx.options=C.addressof(active_options)
 active_first=d.GSY_connect(2,cfg,None,0,C.byref(active_ctx))
 assert active_first
 active_ctx.options=None
 active_second=U()
 assert d.GSY_connect_session(cfg,second_session,C.byref(active_ctx),C.byref(active_second))==2

 for _ in range(600):
  if len(active_connections)==2 and len(server_connections)==2: break
  time.sleep(.01)
 assert len(active_connections)==len(server_connections)==2, (active_connections,server_connections,errors)
 assert set(accepted_owners.values())=={sh,sh2}, accepted_owners
 for h in active_connections:
  remote=U()
  assert d.GSY_GetConnectionP2PPeerId(h,C.byref(remote))==0 and remote.value==registered[sids[server]][1]
 for h in server_connections:
  remote=U()
  assert d.GSY_GetConnectionP2PPeerId(h,C.byref(remote))==0 and remote.value==registered[sids[client]][1]
 assert set(active_connections)=={active_first,active_second.value}
 assert d.GSY_disconnect(active_second.value)==0
 assert d.GSY_stop_server(sh2)==0
 assert d.GSY_is_connected(active_first)==1
 assert d.GSY_is_connected(handles[server])==1
 for _ in range(1000):
  if len(peerconn)==2: break
  time.sleep(.01)
 print('peerconn',peerconn,flush=True); assert len(peerconn)==2
 client=next(i for i in range(2) if not results[sids[i]].server); server=1-client
 assert d.GSY_ConnectPeerDatagram(peerconn[client+1],0,99,C.byref(dc[client]),99,created,client+1)==2
 for _ in range(400):
  if rejected: break
  time.sleep(.01)
 assert rejected==[(b'blocked by application',99)], (decisions,rejected,errors)
 assert d.GSY_ConnectPeerDatagram(peerconn[client+1],results[sids[client]].peer,101,C.byref(dc[client]),3,created,client+1)==2
 accepted=set()
 for _ in range(1000):
  if len(datagram)==2: break
  time.sleep(.01)
 print('datagram',datagram,flush=True); assert len(datagram)==2,datagram
 client_sid=next(sid for sid,index in peerstreams.items() if index==client+1 and sid in datagram)
 server_sid=next(sid for sid,index in peerstreams.items() if index==server+1 and sid in datagram)
 surviving=[h for h in server_connections if d.GSY_GetPeerStreamIds(h,server_sid,C.byref(U()),C.byref(U()))==0]
 assert len(surviving)==1
 peerconn[server+1]=surviving[0]
 for idx,local_id,remote_id in ((client,101,201),(server,201,101)):
  sid=S(); local=U(); remote=U()
  assert d.GSY_FindPeerStream(peerconn[idx+1],remote_id,C.byref(sid))==0
  assert d.GSY_GetPeerStreamIds(peerconn[idx+1],sid,C.byref(local),C.byref(remote))==0
  assert (local.value,remote.value)==(local_id,remote_id)
 assert decisions==[99,101]
 assert d.GSY_SendDatagram(peerconn[server+1],server_sid,b'no',2,5)==1012
 assert d.GSY_OpenDatagramChannel(peerconn[server+1],server_sid,206)==2
 for _ in range(400):
  if 206 in channel_results: break
  time.sleep(.01)
 assert channel_results[206][2]==0, channel_results
 assert d.GSY_SendDatagram(peerconn[server+1],server_sid,b'reverse',7,6)==2
 for _ in range(400):
  if b'reverse' in received: break
  time.sleep(.01)
 assert [(sid,payload) for sid,payload in deliveries if payload==b'reverse']==[(client_sid,b'reverse')]
 assert d.GSY_ConnectPeerDatagram(peerconn[client+1],0,0,C.byref(dc[client]),100,created,client+1)==-1
 assert d.GSY_ConnectPeerDatagram(peerconn[client+1],0,101,C.byref(dc[client]),102,created,client+1)==2
 assert d.GSY_MakePeerDatagramStream(peerconn[client+1],102,C.byref(dc[client]),103,created,client+1)==2
 for _ in range(400):
  if 102 in stream_creation and len(datagram)==4: break
  time.sleep(.01)
 assert stream_creation.get(102)==(1010,0), stream_creation
 assert len(datagram)==4 and decisions==[99,101,102], (datagram,decisions)
 other_client_sid=S(); other_server_sid=S()
 assert d.GSY_FindPeerStream(peerconn[client+1],202,C.byref(other_client_sid))==0
 assert d.GSY_FindPeerStream(peerconn[server+1],102,C.byref(other_server_sid))==0
 assert d.GSY_SendDatagram(peerconn[client+1],other_client_sid,b'blocked',7,6)==1012
 assert d.GSY_OpenDatagramChannel(peerconn[client+1],other_client_sid,200)==2
 for _ in range(400):
  if 200 in channel_results: break
  time.sleep(.01)
 assert channel_results[200][2:]==(-1,b"channel denied"), channel_results
 assert d.GSY_OpenDatagramChannel(peerconn[client+1],other_client_sid,201)==2
 for _ in range(400):
  if 201 in channel_results: break
  time.sleep(.01)
 assert channel_results[201][2]==0, channel_results
 assert d.GSY_OpenDatagramChannel(peerconn[client+1],other_client_sid,202)==1012
 assert d.GSY_SendDatagram(peerconn[client+1],other_client_sid,b'other',5,6)==2
 for _ in range(400):
  if b'other' in received: break
  time.sleep(.01)
 assert [(sid,payload) for sid,payload in deliveries if payload==b'other']==[(other_server_sid.value,b'other')]
 assert d.GSY_OpenDatagramChannel(peerconn[client+1],client_sid,203)==2
 for _ in range(400):
  if 203 in channel_results: break
  time.sleep(.01)
 assert channel_results[203][2]==0, channel_results
 assert d.GSY_CloseDatagramChannel(peerconn[client+1],client_sid,204)==2
 for _ in range(400):
  if 204 in channel_results: break
  time.sleep(.01)
 assert channel_results[204][2]==0, channel_results
 assert d.GSY_SendDatagram(peerconn[client+1],client_sid,b'closed',6,5)==1012
 assert d.GSY_OpenDatagramChannel(peerconn[client+1],client_sid,205)==2
 for _ in range(400):
  if 205 in channel_results: break
  time.sleep(.01)
 assert channel_results[205][2]==0, channel_results
 assert d.GSY_SendDatagram(peerconn[client+1],client_sid,b'hello',5,5)==2
 for _ in range(500):
  if b'hello' in received: break
  time.sleep(.01)
 assert b'hello' in received,received
 print('PASS: authenticated P2P datagram exchanged in one process',flush=True)
 d.GSY_ReqDisconnectPeerDatagram.argtypes=[U,S,C.c_char_p,U]
 assert d.GSY_ReqDisconnectPeerDatagram(peerconn[client+1],client_sid,b'done',7)==2
 for _ in range(400):
  if d.GSY_FindPeerStream(peerconn[client+1],201,C.byref(S()))!=0: break
  time.sleep(.01)
 assert d.GSY_FindPeerStream(peerconn[client+1],201,C.byref(S()))==1201
 assert d.GSY_FindPeerStream(peerconn[server+1],101,C.byref(S()))==1201
 assert d.GSY_ConnectPeerDatagram(peerconn[client+1],0,101,C.byref(dc[client]),104,created,client+1)==2
 for _ in range(400):
  if d.GSY_FindPeerStream(peerconn[client+1],201,C.byref(S()))==0: break
  time.sleep(.01)
 assert d.GSY_FindPeerStream(peerconn[client+1],201,C.byref(S()))==0
 assert d.GSY_stop_server(sh)==0
 for _ in range(400):
  if len(stopped)==2: break
  time.sleep(.01)
 assert (0,sh) in stopped and (0,sh2) in stopped
 time.sleep(.2)
 failed_handle=d.GSY_connect(1,os.fsencode(tmp_path/'missing.toml'),host,8101,C.byref(contexts[0]))
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
 if sys.exc_info()[0] is AssertionError and (tmp_path/'log.log').exists():
  print((tmp_path/'log.log').read_text(errors='replace')[-5000:],flush=True)
 print('terminate',d.GSY_terminate(),flush=True)
 lobby_server.terminate(); lobby_server.wait(timeout=5); tmp.cleanup()
