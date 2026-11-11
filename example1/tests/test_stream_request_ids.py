import subprocess
import sys
import tempfile
import time
from pathlib import Path

client_exe, server_exe = map(Path, sys.argv[1:3])
for mode in ("normal", "cancel", "cancel_all", "reentrant", "success_cancel"):
  with tempfile.TemporaryDirectory(prefix="gsync-stream-test-") as tmp:
    client_dir = Path(tmp) / "client"
    server_dir = Path(tmp) / "server"
    client_dir.mkdir()
    server_dir.mkdir()
    client = subprocess.Popen(
        [str(client_exe), mode], cwd=client_dir,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    server = None
    try:
        # Ensure stream requests are queued before the server starts.
        time.sleep(1)
        server = subprocess.Popen(
            [str(server_exe)], cwd=server_dir,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True,
        )
        output, _ = client.communicate(timeout=15)
        print(f"{mode}: exit={client.returncode}\n{output}", flush=True)
        if client.returncode != 0:
            raise RuntimeError(f"{mode} failed")
    finally:
        if client.poll() is None:
            client.kill()
            client.communicate()
        if server is not None:
            try:
                server_output, _ = server.communicate("\n", timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server_output, _ = server.communicate()
                raise RuntimeError("server did not stop")
            if server.returncode != 0:
                print(server_output, flush=True)
                raise RuntimeError(f"server exited with {server.returncode}")
