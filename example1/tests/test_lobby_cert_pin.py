import hashlib
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import time

server, client, config, cert = map(Path, sys.argv[1:5])
pem = cert.read_text()
pin = hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem)).hexdigest()
with tempfile.TemporaryDirectory(prefix="gsync-lobby-pin-") as tmp:
    work = Path(tmp)
    config_text = config.read_text().replace("port=8102", "port=0")
    service = subprocess.Popen([str(server), "server", "30000"], cwd=work, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        time.sleep(1)
        for mode, value in (("accept", pin), ("reject", "00" * 32), ("malformed", "xyz"), ("unconfigured", None)):
            target = work / (mode + ".toml")
            target.write_text(config_text if value is None else config_text.replace("[p2p]", f'server_cert_sha256="{value}"\n[p2p]'))
            result = subprocess.run([str(client), str(target), mode], cwd=work, capture_output=True, text=True, timeout=12)
            print(mode, result.returncode, result.stdout, result.stderr, flush=True)
            if result.returncode or "lobby pin: PASS" not in result.stdout:
                raise RuntimeError(f"certificate pin {mode} failed")
    finally:
        if service.poll() is None:
            service.kill()
        service.wait()
