#!/usr/bin/env python3
"""Measure an actual Linux process; no synthetic RAM estimates or third-party modules."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import time


def cpu_seconds(stat, ticks):
    fields = stat.rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / ticks


def kilobytes(text):
    return {line.split(":", 1)[0]: int(line.split()[1]) * 1024
            for line in text.splitlines() if ":" in line and len(line.split()) == 3 and line.split()[2] == "kB"}


def measure(executable, seconds=15):
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    command = [str(Path(executable).resolve()), "--demo", "--quit-after", str(seconds + 10)]
    process = subprocess.Popen(command, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(3)
        if process.poll() is not None:
            raise RuntimeError("The widget exited before measurement")
        proc = Path(f"/proc/{process.pid}")
        ticks = os.sysconf("SC_CLK_TCK")
        before = cpu_seconds((proc / "stat").read_text(), ticks)
        start = time.monotonic()
        time.sleep(seconds)
        elapsed = time.monotonic() - start
        cpu = cpu_seconds((proc / "stat").read_text(), ticks) - before
        memory = kilobytes((proc / "status").read_text())
        shared = kilobytes((proc / "smaps_rollup").read_text())
        result = {
            "os": platform.platform(),
            "architecture": platform.machine(),
            "scenario": "100 synthetic cached tasks; no Google connection; after 3 s warmup",
            "qt_platform": env["QT_QPA_PLATFORM"],
            "sample_seconds": elapsed,
            "idle_cpu_seconds": cpu,
            "idle_cpu_percent_one_core": 100 * cpu / elapsed,
            "rss_bytes": memory["VmRSS"],
            "peak_rss_bytes": memory["VmHWM"],
            "pss_bytes": shared["Pss"],
            "private_bytes": shared.get("Private_Clean", 0) + shared.get("Private_Dirty", 0),
            "executable_bytes": Path(executable).stat().st_size,
        }
        code = process.wait(timeout=15)
        if code:
            raise RuntimeError(f"The widget exited with {code}")
        return result
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    result = measure(args.executable)
    Path(args.output).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
