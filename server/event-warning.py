#!/usr/bin/env python3
import subprocess, time

URL   = "http://ESP-IP:80/alert"
TOKEN = "CHANGE-ME"
last = 0

proc = subprocess.Popen(["journalctl", "-fu", "ssh", "-n", "0", "-o", "cat"],
                        stdout=subprocess.PIPE, text=True)
for line in proc.stdout:
    if time.time() - last < 1:
        continue
    last = time.time()
    r = subprocess.run(
        ["curl", "-s", "-m", "10", "-o", "/dev/null", "-w", "%{http_code}",
         "-X", "POST", URL, "-H", f"X-Token: {TOKEN}", "-d", "x"],
        capture_output=True, text=True)
    print("Signal, Antwort:", r.stdout, "|", line.strip(), flush=True)