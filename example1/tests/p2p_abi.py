"""ctypes declarations shared by the SDK integration tests."""
import ctypes as C

U=C.c_uint32; S=C.c_uint64; I=C.c_int; P=C.c_void_p
ERR=C.CFUNCTYPE(None,C.c_char_p,I); IO=C.CFUNCTYPE(None,I,U); SC=C.CFUNCTYPE(None,U,S,I)
class GC(C.Structure): _fields_=[('error', ERR)]
class CC(C.Structure): _fields_=[('connect',IO),('disconnect',IO),('error',IO),('open',SC),('close',SC),('extend',P),('options',P)]
REG=C.CFUNCTYPE(None,I,S,U)
class PeerData(C.Structure): _fields_=[('id',U),('name',C.c_char_p)]
REQ=C.CFUNCTYPE(None,C.POINTER(PeerData),S)
class Addr(C.Structure): _fields_=[('ip',C.c_char*40),('port',C.c_uint16)]
class Result(C.Structure): _fields_=[('ret',I),('peer',U),('server',I),('peer_addr',Addr),('reason',C.c_char_p),('address',Addr),('relay_addr',Addr),('use_relay',I),('relay_token',C.c_char_p),('session',C.c_uint64),('verify_token',P),('verify_len',C.c_size_t)]
HELP=C.CFUNCTYPE(None,S,C.POINTER(Result))
DCON=C.CFUNCTYPE(None,I,S,U,C.c_char_p,U); DRECV=C.CFUNCTYPE(None,P,C.c_size_t,S,U,U); DFAIL=C.CFUNCTYPE(None,I,S,U,U)
DEND=C.CFUNCTYPE(None,I,S,U,C.c_char_p); DERROR=C.CFUNCTYPE(None,U,S,I,C.c_char_p,U)
class DCtx(C.Structure): _fields_=[('connected',DCON),('recv',DRECV),('disconnected',DEND),('failed',DFAIL),('error',DERROR),('self_id',U),('token',P),('token_len',I),('relay',I),('server',I),('extend',P),('check',C.c_uint8),('rejected',P),('channel_request',P),('channel_result',P)]
CHANNEL_RESULT=C.CFUNCTYPE(None,S,I,I,U,C.c_char_p)
CHANNEL_REQUEST=C.CFUNCTYPE(I,U,S,U,C.POINTER(P))
CREATED=C.CFUNCTYPE(None,I,S,U,P)
ACCEPT=C.CFUNCTYPE(P,U,U); DISCONNECT=C.CFUNCTYPE(None,U,U)
POLICY=C.CFUNCTYPE(I,U,U,S,U,C.POINTER(U),C.POINTER(P))
REJECTED=C.CFUNCTYPE(None,S,C.c_char_p,U)
class Extension(C.Structure): _fields_=[('type',U),('size',U)]
class ConnectOptions(C.Structure): _fields_=[('header',Extension),('session',S)]
class ServerOptions(C.Structure): _fields_=[('header',Extension),('socket',S),('policy',POLICY)]
class ServerCtx(C.Structure): _fields_=[('started',IO),('stopped',IO),('error',IO),('accept',ACCEPT),('disconnect',DISCONNECT),('extra',P),('options',P)]
READY=C.CFUNCTYPE(None,S,U,S)
class LC(C.Structure): _fields_=[('reg',REG),('unreg',P),('respond',P),('list',P),('request',P),('error',P),('change',P),('attempt',P),('quit',P),('extend',P),('check',C.c_uint8),('ready',READY)]
