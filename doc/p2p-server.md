# 使用基础 Server 建立直连 P2P

接收方先用 lobby 连接的 UDP socket 启动 `GSY_launch_server`。一次启动可以接受多个经过认证的 peer，每次协商只需登记本次会话。主动方调用 `GSY_connect_session`，连接建立后通过 `GSY_ConnectPeerDatagram` 打开业务 stream。

## 接收方

1. 从已连接的 lobby 获取 socket。`GSY_GetConnectionSocket(lobby_connection, &socket)` 成功后返回 SDK 句柄。将它填入零初始化的 `GSY_BaseServerContext.socket`，将 `mode` 设为 `GSY_SERVER_MODE_P2P`，再用 `GSY_launch_server(2, config, &server_ctx)` 启动。两个 peer 的 P2P 配置必须使用相同 ALPN，并开启 `es_datagrams`。监听地址以 socket 的实际绑定地址为准。普通监听用默认值 `GSY_SERVER_MODE_NORMAL`，并将 `socket` 留为零。
2. 保持 lobby 连接有效，直到收到 `on_started(EC_Ok, server)`。SDK socket 句柄是弱引用，server 启动成功后才持有自己的引用。此后可以用 `GSY_ReleaseSocket(socket)` 注销句柄。启动失败时从 `on_error` 获取结果。socket 销毁后，过期句柄会在下次查找时清理。
3. lobby 的 `on_helper_quit_result` 报告 `EC_Ok` 且 `is_server == 1` 时，调用 `GSY_RegisterServerP2PSession(server, result->session_handle)`。会话必须来自同一个 UDP socket。登记成功后开始等待对端，最长 10 秒，且不会超过会话本身的有效期。应用应安排主动方在接收方登记后发起连接。
4. `on_client_connect(server, connection)` 只在认证通过后触发。返回连接专属的 `GSY_BaseConnectionContext` 接收该连接，或返回 `NULL` 拒绝。`on_connect` 随后报告连接可用。
5. 在连接的 `on_stream_open(connection, stream, EC_Ok)` 中调用 `GSY_ServerConnectPeerDatagram`，绑定接收、连接及失败回调。可选的 `on_p2p_stream_request` 在握手时收到对端业务 ID。接受时返回非零并通过 `local_stream_id` 返回本端 ID。拒绝时返回 0，并可将 `reason` 指向一段在回调返回后仍有效的字符串，SDK 会立即复制。未设置回调时，SDK 用对端 ID 作为本端 ID，校验冲突后接受。

## 主动方

1. lobby 报告 `EC_Ok` 且 `is_server == 0` 后，调用 `GSY_connect_session(config, result->session_handle, &conn_ctx, &connection)`。返回 `EC_Pending` 表示连接已开始，等 `conn_ctx.on_connect` 后使用连接句柄。失败通过 `conn_ctx.on_error` 报告。
2. 调用 `GSY_ConnectPeerDatagram(connection, peer_id, local_stream_id, &datagram_ctx, request_id, created, user)`。创建完成回调给出本地 stream 句柄，`datagram_ctx.on_connected` 表示双方已接受握手。若收到业务拒绝，`on_stream_rejected` 提供原因，`on_connect_failed` 报告失败。
3. 使用 `GSY_GetPeerStreamIds(connection, stream, &local_stream_id, &peer_stream_id)` 读取双方业务标识，或用 `GSY_FindPeerStream(connection, peer_stream_id, &stream)` 从对端标识查找本地 stream。业务标识为非零 `GSY_P2PStreamId`（`uint32_t`），在每条连接内按方向分别唯一。只有握手成功的 stream 可查询。使用本地 stream 句柄调用 `GSY_SendDatagram`。

## 生命周期

- 回调在 SDK I/O 线程执行。上下文必须存活到相应连接或 server 的终止回调返回。拒绝原因和收到的 datagram 数据只在回调期间可用。
- 用 `GSY_CancelServerP2PSession` 取消仍在等待的会话。等待超时通过 server 的 `on_error(EC_Timeout, server)` 报告。会话认证成功后即被消费，需重新协商才能建立另一条连接。超时或取消释放会话的使用权，仍有效的会话可重试或用 `GSY_ReleaseP2PSession` 释放。
- 用 `GSY_ReqDisconnectPeerDatagram` 结束业务 stream，或用 `GSY_disconnect` 关闭单个连接。用 `GSY_stop_server` 关闭该 server 的连接和待处理会话。server 和 lobby 各自持有 socket 引用。
- 原有 `GSY_StartP2PPeer` 可继续用于独立 peer 流程。基础 server 的接收端应与 `GSY_connect_session` 配对。双方必须使用支持 stream 标识的新协议版本。
