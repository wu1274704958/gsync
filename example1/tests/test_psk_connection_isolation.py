import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="gsync-psk-isolation-") as tmp:
    root = Path(tmp)
    for name in ("server", "good", "bad"):
        (root / name).mkdir()
    config = root / "client.toml"
    config.write_text(Path(sys.argv[2]).read_text().replace("port=8102", "port=0"))
    env = os.environ.copy()
    env["MQAS_TEST_CLIENT_CONFIG"] = str(config)
    processes = []
    try:
        server = subprocess.Popen([sys.argv[1], "server"], cwd=root / "server", env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        processes.append(server)
        time.sleep(1)
        good = subprocess.Popen([sys.argv[1], "client"], cwd=root / "good", env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        bad = subprocess.Popen([sys.argv[1], "client", "mismatch"], cwd=root / "bad", env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        processes.extend((good, bad))
        for name, process in (("good", good), ("bad", bad), ("server", server)):
            output, _ = process.communicate(timeout=12)
            print(name, process.returncode, output, flush=True)
            assert process.returncode == 0 and ": PASS" in output
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.communicate()
