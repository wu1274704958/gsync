import subprocess
import sys
import tempfile
import time
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="gsync-psk-mismatch-") as tmp:
    root = Path(tmp)
    (root / "server").mkdir()
    (root / "client").mkdir()
    server = subprocess.Popen([sys.argv[1], "server", "mismatch"], cwd=root / "server",
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    client = None
    try:
        time.sleep(1)
        client = subprocess.Popen([sys.argv[1], "client", "mismatch"], cwd=root / "client",
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        client_out, _ = client.communicate(timeout=12)
        server_out, _ = server.communicate(timeout=12)
        print("client:\n", client_out, "server:\n", server_out)
        assert client.returncode == 0 and server.returncode == 0
        assert "custom client: PASS" in client_out and "custom server: PASS" in server_out
    finally:
        for process in (client, server):
            if process is not None and process.poll() is None:
                process.kill()
                process.communicate()
