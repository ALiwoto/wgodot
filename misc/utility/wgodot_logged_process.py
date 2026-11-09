# wgodot-changes::file
"""Timestamp received output; measure process durations with a monotonic clock.

Output timestamps describe receipt, not execution inside a buffered child. Use
SCons command spans and LLVM traces for per-file timings within parallel builds.
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


def run(command: list[str], label: str, log_path: Path) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    environment = os.environ.copy()
    environment["PYTHONUNBUFFERED"] = "1"
    with log_path.open("a", encoding="utf-8", buffering=1) as log:

        def record(message: str) -> None:
            utc = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
            line = f"[{utc} +{time.perf_counter() - started:.6f}s {label}] {message}"
            log.write(line + "\n")
            print(line, flush=True)

        record("START " + json.dumps(command))
        process: subprocess.Popen[str] | None = None
        code = 1
        try:
            process = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                env=environment,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
                start_new_session=os.name != "nt",
            )
            record(f"SPAWN pid={process.pid}")
            assert process.stdout is not None
            for line in process.stdout:
                record(line.rstrip("\r\n"))
            code = process.wait()
        except KeyboardInterrupt:
            code = 130
            record("INTERRUPTED")
        except OSError as error:
            record(f"ERROR {error}")
        finally:
            if process is not None:
                if process.poll() is None:
                    if os.name == "nt":
                        subprocess.run(
                            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL,
                            check=False,
                        )
                    else:
                        os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                if process.stdout is not None:
                    process.stdout.close()
            record(f"END exit={code} seconds={time.perf_counter() - started:.6f}")
    return code


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command: list[str] = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("Supply the command after --")
    return run(command, args.label, args.log)


if __name__ == "__main__":
    sys.exit(main())
