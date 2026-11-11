import subprocess
import sys
import tempfile
import time
from pathlib import Path

mode = sys.argv[1]
with tempfile.TemporaryDirectory(prefix="gsync-examples-") as tmp:
    server_dir = Path(tmp) / "server"
    client_dir = Path(tmp) / "client"
    server_dir.mkdir()
    client_dir.mkdir()
    server_args = [sys.argv[2], "server"] if mode in ("custom", "lobby") else [sys.argv[2]]
    client_args = [sys.argv[2], "client"] if mode in ("custom", "lobby") else [sys.argv[3]]
    server = subprocess.Popen(server_args, cwd=server_dir, stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    client = None
    try:
        time.sleep(1)
        client = subprocess.Popen(client_args, cwd=client_dir, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if mode == "p2p":
            time.sleep(3)
        client_out, _ = client.communicate("\n", timeout=12)
        server_out, _ = server.communicate("\n", timeout=12)
        print("client:\n", client_out, "server:\n", server_out)
        assert client.returncode == 0 and server.returncode == 0
        if mode == "custom":
            assert "custom client: PASS" in client_out and "custom server: PASS" in server_out
        elif mode == "lobby":
            assert "lobby registration: PASS" in client_out
        else:
            assert "unauthenticated connection rejected" in client_out
            assert "stream creation code=0" not in client_out
    finally:
        for process in (client, server):
            if process is not None and process.poll() is None:
                process.kill()
                process.communicate()
