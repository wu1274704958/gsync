import subprocess
import os
import sys
import tempfile
import time
from pathlib import Path

lobby_exe, peer_exe, tests, root = map(Path, sys.argv[1:5])
delay = sys.argv[5] if len(sys.argv) > 5 else ""
with tempfile.TemporaryDirectory(prefix="gsync-peer-test-") as tmp:
    base = Path(tmp)
    lobby_dir, host_dir, join_dir = (base / name for name in ("lobby", "host", "join"))
    for path in (lobby_dir, host_dir, join_dir):
        path.mkdir()
    client_config = (tests / "test_connect_server" / "conf.txt").read_text()
    client_config = client_config.replace("port=8102", "port=0")
    client_config = client_config.replace('[engine_config]', '[engine_config]\nserver_cert_sha256="7ca282f762eaaa3b7582e858b3c74fbec7ee421bc3448f7a6fb084e27e49a1ef"')
    for directory in (host_dir, join_dir):
        (directory / "lobby.toml").write_text(client_config)
    peer_config = (tests / "test_launch_server" / "conf.txt").read_text()
    cert = (root / "mqas" / "certificate").as_posix()
    peer_config = peer_config.replace("../../../mqas/certificate", cert)
    peer_config = peer_config.replace("ignore_verify_token=true", "ignore_verify_token=false")
    peer_config_path = base / "peer.toml"
    peer_config_path.write_text(peer_config)
    procs = []
    env = os.environ.copy()
    env["GSYNC_TEST_DELAY_PEER"] = delay if delay in ("server", "client") else ""
    if delay == "bad_identity":
        env["GSYNC_TEST_BAD_IDENTITY"] = "1"
    if delay in ("timeout", "cancel", "client_cancel", "bad_config", "lobby_close", "parallel"):
        env["GSYNC_TEST_PEER_SCENARIO"] = delay
    try:
        lobby = subprocess.Popen([str(lobby_exe), "server", "30000"], cwd=lobby_dir,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        procs.append(lobby)
        time.sleep(1)
        host = subprocess.Popen([str(peer_exe), "host", str(host_dir / "lobby.toml"), str(peer_config_path)],
                                cwd=host_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
        procs.append(host)
        time.sleep(1)
        join = subprocess.Popen([str(peer_exe), "join", str(join_dir / "lobby.toml"), str(peer_config_path)],
                                cwd=join_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
        procs.append(join)
        outputs = []
        for name, process in (("join", join), ("host", host)):
            output, _ = process.communicate(timeout=25)
            print(f"{name} exit={process.returncode}\n{output}", flush=True)
            outputs.append((process.returncode, output))
        if any(code != 0 or "peer result=PASS" not in output for code, output in outputs):
            for directory in (lobby_dir, host_dir, join_dir):
                for log in directory.glob("*.log"):
                    print(f"{log}:\n{log.read_text(errors='replace')[-8000:]}", flush=True)
            raise RuntimeError("P2P peer connection failed")
    finally:
        for process in procs:
            if process.poll() is None:
                process.kill()
                process.communicate()
