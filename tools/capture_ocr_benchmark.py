#!/usr/bin/env python3
"""Capture the on-device experiment without resetting the running board."""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import time

import serial

ROOT = Path(__file__).resolve().parents[1]


def provenance(output):
    paths = [ROOT / "sdkconfig", ROOT / "build/lvgl_demo_v9.bin", ROOT / "build/lvgl_demo_v9.elf"]
    paths += list((ROOT / "main").glob("*.cpp")) + list((ROOT / "main").glob("*.hpp"))
    paths += list((ROOT / "components/pp_ocr_v6").glob("*.cpp"))
    paths += list((ROOT / "components/pp_ocr_v6").glob("*.hpp"))
    paths += list((ROOT / "components/pp_ocr_v6/models/p4").glob("*.espdl"))
    paths += list((ROOT / "tools").glob("*ocr_benchmark*.py"))
    paths += [ROOT / "main/CMakeLists.txt", ROOT / "main/Kconfig.projbuild",
              ROOT / "components/pp_ocr_v6/CMakeLists.txt"]
    paths += list((ROOT / "main/test_images/benchmark").glob("*"))
    paths += [ROOT / "main/test_images/pp_ocr_v6.jpg"]
    info = {
        "captured_at": datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
        "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "git_status": subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True),
        "files": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
    }
    (output / "provenance.json").write_text(json.dumps(info, indent=2, ensure_ascii=False) + "\n")
    (output / "sdkconfig.test").write_bytes((ROOT / "sdkconfig").read_bytes())
    diff = subprocess.check_output(["git", "diff", "--", "components", "main", "sdkconfig"], cwd=ROOT)
    (output / "tracked_changes.patch").write_bytes(diff)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=2400)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    # Never mix two experiments or overwrite a previous measurement.
    if (args.output / "raw_serial.log").exists():
        parser.error("output already contains raw_serial.log; choose a new run directory")
    port = serial.Serial(port=None, baudrate=115200, timeout=0.25, exclusive=True)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    provenance(args.output)
    start = time.monotonic()
    last_trigger = 0
    started = False
    measured = 0
    pending = b""
    with port, (args.output / "raw_serial.log").open("wb") as raw, \
            (args.output / "events.jsonl").open("w", encoding="utf-8") as events:
        while time.monotonic() - start < args.timeout:
            if not started and time.monotonic() - last_trigger >= 2:
                port.write(b"b\n")
                last_trigger = time.monotonic()
            chunk = port.read(port.in_waiting or 1)
            if not chunk:
                continue
            raw.write(chunk)
            raw.flush()
            pending += chunk
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").strip()
                if any(marker in text for marker in ("Guru Meditation", "assert failed", "abort() was called", "task_wdt:")):
                    raise RuntimeError(f"Device error; measurement invalid: {text}")
                if "BENCH_JSON " not in text:
                    if "BENCH_READY" in text:
                        print(text, flush=True)
                    continue
                record = json.loads(text.split("BENCH_JSON ", 1)[1])
                events.write(json.dumps(record, ensure_ascii=False) + "\n")
                events.flush()
                kind = record["type"]
                if kind == "start":
                    if started:
                        raise RuntimeError("Unexpected restart; measurement invalid")
                    started = True
                    print(f"START {record}", flush=True)
                elif kind == "model":
                    if not record["valid"]:
                        raise RuntimeError(f"Invalid model shape: {record}")
                    print(f"MODEL {record['sample']} {record['size']} batch={record['batch']} "
                          f"input={record['input']} output={record['output']}", flush=True)
                elif kind == "run" and not record["warmup"]:
                    measured += 1
                    if record["run"] % 5 == 0:
                        print(f"RUN {measured}/160 {record['sample']} {record['size']} "
                              f"n={record['run']} total={record['total_us']/1000:.2f} ms boxes={record['boxes']}", flush=True)
                elif kind == "quality_done":
                    print(f"QUALITY {record['sample']} {record['size']}: {record['boxes']} boxes", flush=True)
                elif kind == "failed":
                    raise RuntimeError("Device reported benchmark failure")
                elif kind == "done":
                    if measured != 160:
                        raise RuntimeError(f"Expected 160 measured detections, got {measured}")
                    print(f"DONE {measured} measurements in {time.monotonic()-start:.1f} seconds", flush=True)
                    return
        raise TimeoutError("Benchmark did not complete; partial logs preserved")


if __name__ == "__main__":
    main()
