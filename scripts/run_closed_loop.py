#!/usr/bin/env python3
"""ACC and Cartpole closed-loop execution with the original Python plants.

Capture ABI and historical tolerances come from R-SHARC/scripts/decode_acc_capture.py.
The CVA6 invocation comes from scripts/hector/run_acc_cva6_closed_loop.sh.
"""
import argparse
import hashlib
import json
import math
import platform
from importlib.metadata import version
import os
import re
import signal
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SIZE = 16448
LIMITS = {"t_start": 1e-12, "t_end": 1e-12, "x_start": 1e-4,
          "x_end": 1e-4, "u_controller": .05, "u_applied_start": .05,
          "w_start": 1e-12, "cost": 100., "constraint_error": 2e-4,
          "dual_residual": 1e-3, "iterations": 25}
DISCRETE = ("k", "controller_status", "solver_status", "is_feasible",
            "adaptive_rho_interval")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def source_hash(controller="acc"):
    """Bind results to code/dependencies, independently of absolute host paths."""
    paths = [ROOT / "Makefile", ROOT / "external/versions.txt", Path(__file__).resolve(),
             ROOT / "scripts/cva6_bridge.hpp", ROOT / "requirements.txt"]
    solver = "external/nlopt" if controller == "cartpole" else "external/osqp"
    for name in ("src/" + controller, "src/dynamics", "src/common", "src/baremetal", "scripts/patches",
                 "external/eigen", "external/json", "external/libmpc/include", solver):
        paths.extend(p for p in (ROOT / name).rglob("*") if p.is_file() and "__pycache__" not in p.parts)
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(str(path.relative_to(ROOT)).encode() + b"\0")
        digest.update(bytes.fromhex(sha(path)))
    return digest.hexdigest()


def decode(path, expected=None):
    raw = Path(path).read_bytes()
    require(len(raw) == SIZE and raw[:8] == b"RSHACC01", "Invalid capture size/magic")
    _, version, stride, count, capacity, complete, peak, heap = struct.unpack_from("<8Q", raw)
    require((version, stride, capacity, complete) == (1, 256, 64, 1),
            "Incomplete capture or incompatible ABI (baseline requires ABI 1)")
    require(1 <= count <= 64 and (expected is None or count == expected), "Wrong step count")
    require(peak <= heap / 2, "Bare-metal heap margin check failed")
    rows = []
    for i in range(count):
        data = raw[64 + i * stride:64 + (i + 1) * stride]
        q, s, d = struct.unpack("<32Q", data), struct.unpack("<32q", data), struct.unpack("<32d", data)
        require(q[0] == i and s[15] in range(5) and q[18] in (0, 1), f"Invalid status/index at {i}")
        require(q[22] == 25 and q[17] > 0, f"Invalid rho interval/iterations at {i}")
        require(all(math.isfinite(d[j]) for j in (*range(1, 15), 19, 20, 21, 24, 25)),
                f"Non-finite numerical field at {i}")
        row = {"k": i, "t_start": d[1], "t_end": d[2], "x_start": list(d[3:6]),
               "x_end": list(d[6:9]), "u_controller": list(d[9:11]),
               "u_applied_start": list(d[11:13]), "w_start": list(d[13:15]),
               "controller_status": s[15], "solver_status": s[16], "iterations": q[17],
               "is_feasible": bool(q[18]), "cost": d[19], "constraint_error": d[20],
               "dual_residual": d[21], "adaptive_rho_interval": q[22],
               "rho_updates": q[23], "rho_estimate": d[24], "rho_final": d[25],
               "cycles": q[26], "instret": q[27]}
        if rows:
            require(row["x_start"] == rows[-1]["x_end"] and
                    row["u_applied_start"] == rows[-1]["u_controller"] and
                    abs(row["t_start"] - rows[-1]["t_end"]) <= 1e-12, f"Broken continuity at {i}")
        rows.append(row)
    return rows


def checked_capture(path, expected=None, controller="acc"):
    path = Path(path).resolve()
    manifest = json.loads(path.with_name("run.json").read_text())
    require(manifest["status"] == "PASS", "Run did not finish successfully")
    require(sha(path) == manifest["capture_sha256"], "Capture changed since execution")
    require(sha(manifest["elf"]) == manifest["elf_sha256"], "ELF changed since execution")
    require(manifest.get("controller", "acc") == controller, "Wrong controller reference")
    require(source_hash(controller) == manifest["sources_sha256"], "Sources changed: rerun before comparing")
    require(sha(manifest["plant"]) == manifest["plant_sha256"], "Host plant changed since execution")
    require(manifest["python_environment"] == python_environment(),
            "Python/NumPy/SciPy changed since execution")
    return decode_cartpole(path, expected) if controller == "cartpole" else decode(path, expected)


def reference_rows(path):
    if Path(path).suffix != ".json":
        return checked_capture(path)
    # Explicit historical baseline only; never an implicit dependency on R-SHARC.
    result = []
    for row in json.loads(Path(path).read_text())["steps"]:
        flat = {**row, **row["metadata"]}
        flat.setdefault("controller_status", {"SUCCESS": 0, "MAX_ITERATION": 1,
                        "INFEASIBLE": 2, "ERROR": 3, "UNKNOWN": 4}[flat["status"]])
        flat.setdefault("adaptive_rho_interval", 25)
        result.append(flat)
    return result


def comparison(observed, reference):
    require(len(observed) <= len(reference), "Reference is shorter than observed capture")
    maxima, failures, exact_differences = {}, [], []
    for a, b in zip(observed, reference):
        fields = (*DISCRETE, *LIMITS, "rho_updates", "rho_estimate", "rho_final")
        for field in dict.fromkeys(fields):
            if field not in b:
                continue
            x, y = a[field], b[field]
            xs, ys = (x, y) if isinstance(x, list) else ([x], [y])
            require(len(xs) == len(ys), f"Dimension mismatch: {field}")
            require(all(math.isfinite(v) for v in (*xs, *ys)), "Non-finite reference value")
            exact = all(struct.pack("<d", u) == struct.pack("<d", v)
                        if isinstance(u, float) or isinstance(v, float) else u == v
                        for u, v in zip(xs, ys))
            error = max(abs(u - v) for u, v in zip(xs, ys))
            maxima[field] = max(maxima.get(field, 0), error)
            if not exact and len(exact_differences) < 20:
                exact_differences.append({"k": a["k"], "field": field, "max_abs": error})
            if (field in DISCRETE and x != y) or (field in LIMITS and error > LIMITS[field]):
                failures.append({"k": a["k"], "field": field, "error": error})
    return {"steps": len(observed), "historical_tolerances_pass": not failures,
            "all_available_math_fields_exact": not exact_differences,
            "limits": LIMITS, "max_abs": maxima, "failures": failures,
            "first_exact_differences": exact_differences,
            "rho_policy": "Reported in exact comparison; historical tolerances do not bound rho diagnostics",
            "excluded": ["cycles", "instret", "heap_peak", "heap_capacity"]}


def symbols(elf, prefix, controller="acc"):
    table = subprocess.check_output([prefix + "-nm", "-S", str(elf)], text=True)
    result = {}
    for line in table.splitlines():
        fields = line.split()
        if len(fields) in (3, 4):
            result[fields[-1]] = (int(fields[0], 16), int(fields[1], 16) if len(fields) == 4 else 0)
    require(result["_start"][0] == 0x80000000, "Unexpected entry point")
    address, size = result["rsharc_" + controller + "_mailbox"]
    require(size == (256 if controller == "cartpole" else 448) and address % 64 == 0 and
            0x80000000 <= address < address + size <= 0x90000000,
            "Mailbox lies outside expected RAM or has an incompatible layout")
    segments = subprocess.check_output([prefix + "-readelf", "-lW", str(elf)], text=True)
    for line in segments.splitlines():
        f = line.split()
        if f and f[0] == "LOAD":
            start, length = int(f[3], 16), int(f[5], 16)
            require(0x80000000 <= start < start + length <= 0x90000000, "ELF LOAD outside RAM")
    return result


def python_environment():
    dependencies = {"python": platform.python_version()}
    for line in (ROOT / "requirements.txt").read_text().splitlines():
        name, expected = line.split("==")
        actual = version(name)
        require(actual == expected, f"{name} {actual}: install requirements.txt using {sys.executable}")
        dependencies[name] = actual
    return dependencies


def run_plant(args):
    """Host loop around the unchanged original ACCDynamics/OdeDynamics."""
    import numpy as np
    sys.path.insert(0, str(ROOT / "src"))
    from dynamics.acc import ACCDynamics

    require(1 <= args.steps <= 64, "Invalid plant step count")
    config = json.loads((ROOT / "src/acc/config.json").read_text())
    dynamics = ACCDynamics(config)
    dt = float(config["system_parameters"]["sample_time"])
    x = np.array(config["x0"], dtype=float).reshape(3, 1)
    applied = np.array(config["u0"], dtype=float).reshape(2, 1)
    previous = applied.copy()
    capture = bytearray(SIZE)
    peak = heap = 0
    for k in range(args.steps):
        t = k * dt
        w = dynamics.get_exogenous_input(t)
        request = struct.pack("<i4x8d", k, t, *x[:, 0], *w[:, 0], *previous[:, 0])
        sys.stdout.buffer.write(request)
        sys.stdout.buffer.flush()
        reply = bytearray()
        while len(reply) < 256:
            part = sys.stdin.buffer.read(256 - len(reply))
            require(part, f"Controller EOF at step {k}")
            reply.extend(part)
        words = struct.unpack("<32Q", reply)
        require(words[0] == k, "Controller response sequence mismatch")
        require(words[15] in (0, 1) and words[18] == 1, f"MPC failed or infeasible at step {k}")
        u = np.array(struct.unpack_from("<2d", reply, 72)).reshape(2, 1)
        require(np.isfinite(u).all(), "Non-finite control")
        # Preserve sample-time updates: integrate with the previous applied
        # control; the newly computed command is applied next interval.
        tf, xf = dynamics.evolve_state(t, x, applied, w, t + dt)
        require(np.isfinite(xf).all(), "Non-finite plant state")
        struct.pack_into("<Q14d", reply, 0, k, t, tf,
                         *x[:, 0], *xf[:, 0], *u[:, 0], *applied[:, 0], *w[:, 0])
        peak, heap = words[28:30]
        reply[224:256] = bytes(32)  # Reserved capture slots remain zero.
        capture[64 + k * 256:64 + (k + 1) * 256] = reply
        print(f"ACC_PLANT_STEP k={k} t={tf:.17g} x={xf[:, 0].tolist()} u={u[:, 0].tolist()}",
              file=sys.stderr, flush=True)
        x, applied, previous = xf, u.copy(), u.copy()
    struct.pack_into("<8Q", capture, 0, int.from_bytes(b"RSHACC01", "little"),
                     1, 256, args.steps, 64, 1, peak, heap)
    (args.out / "capture.bin").write_bytes(capture)



def decode_cartpole(path, expected=None):
    capture = json.loads(Path(path).read_text())
    require(capture["schema"] == 1 and capture["controller"] == "cartpole" and capture["complete"] is True,
            "Incomplete or incompatible Cartpole capture")
    rows = capture["steps"]
    require(1 <= len(rows) <= 64 and (expected is None or len(rows) == expected), "Wrong step count")
    for k, row in enumerate(rows):
        require(row["k"] == k and type(row["solver_status"]) is int and
                type(row["is_feasible"]) is bool and type(row["cost_available"]) is bool,
                f"Invalid Cartpole status/index at {k}")
        for name, length in (("x_start", 4), ("x_end", 4), ("u_controller", 1),
                             ("u_applied_start", 1), ("w_start", 1)):
            require(len(row[name]) == length and all(math.isfinite(v) for v in row[name]),
                    f"Invalid {name} at {k}")
        require(math.isfinite(row["t_start"]) and math.isfinite(row["t_end"]) and
                row["t_end"] > row["t_start"], f"Invalid time at {k}")
        require((row["cost_available"] and row["cost"] is not None and math.isfinite(row["cost"])) or
                (not row["cost_available"] and row["cost"] is None), f"Invalid cost at {k}")
        require(0 <= row["heap_peak"] <= row["heap_capacity"] / 2, f"Heap margin failed at {k}")
        if k:
            require(row["x_start"] == rows[k-1]["x_end"] and
                    row["u_applied_start"] == rows[k-1]["u_controller"] and
                    abs(row["t_start"] - rows[k-1]["t_end"]) <= 1e-12, f"Broken continuity at {k}")
    return rows


def comparison_cartpole(observed, reference):
    require(len(observed) <= len(reference), "Reference is shorter than observed capture")
    fields = ("k", "t_start", "t_end", "x_start", "x_end", "u_controller", "u_applied_start",
              "w_start", "solver_status", "is_feasible", "cost_available", "cost")
    differences, maxima = [], {}
    for a, b in zip(observed, reference):
        for field in fields:
            xs = a[field] if isinstance(a[field], list) else [a[field]]
            ys = b[field] if isinstance(b[field], list) else [b[field]]
            require(len(xs) == len(ys), "Reference dimension mismatch: " + field)
            exact = True
            for x, y in zip(xs, ys):
                if x is None or y is None:
                    exact &= x is y
                elif isinstance(x, float) or isinstance(y, float):
                    exact &= struct.pack("<d", x) == struct.pack("<d", y)
                    maxima[field] = max(maxima.get(field, 0.0), abs(x - y))
                else:
                    exact &= x == y
            if not exact:
                differences.append({"k": a["k"], "field": field})
    return {"steps": len(observed), "all_available_math_fields_exact": not differences,
            "max_abs": maxima, "difference_count": len(differences),
            "first_exact_differences": differences[:20],
            "excluded": ["cycles", "instret", "heap_peak", "heap_capacity"]}


def run_cartpole_plant(args):
    import numpy as np
    sys.path.insert(0, str(ROOT / "src"))
    from dynamics.cartpole import CartPoleDynamics

    require(1 <= args.steps <= 64, "Invalid plant step count")
    config = json.loads((ROOT / "src/cartpole/config.json").read_text())
    dynamics = CartPoleDynamics(config)
    dt = float(config["system_parameters"]["sample_time"])
    require(dt > 0 and math.isfinite(dt), "Invalid sample period")
    x = np.array(config["x0"], dtype=float).reshape(4, 1)
    applied = np.array(config["u0"], dtype=float).reshape(1, 1)
    previous = applied.copy()
    rows = []
    for k in range(args.steps):
        t = k * dt
        w = dynamics.get_exogenous_input(t)
        request = struct.pack("<Q7d", k, t, *x[:, 0], *w[:, 0], *previous[:, 0])
        sys.stdout.buffer.write(request)
        sys.stdout.buffer.flush()
        reply = bytearray()
        while len(reply) < 80:
            part = sys.stdin.buffer.read(80 - len(reply))
            require(part, f"Controller EOF at step {k}")
            reply.extend(part)
        index, status, feasible, cost_available, force, cost, cycles, instret, peak, heap = struct.unpack(
            "<QqQQddQQQQ", reply)
        require(index == k and feasible in (0, 1) and cost_available in (0, 1), "Invalid controller reply")
        require(math.isfinite(force) and (not cost_available or math.isfinite(cost)), "Non-finite MPC result")
        u = np.array([[force]])
        # Same sample-time calendar as ACC: use the previous control for this
        # interval and apply the newly computed command at the next sample.
        tf, xf = dynamics.evolve_state(t, x, applied, w, t + dt)
        require(abs(tf - (t + dt)) <= 1e-12 and np.isfinite(xf).all(), "Plant integration failed")
        rows.append({"k": k, "t_start": t, "t_end": tf, "x_start": x[:, 0].tolist(),
                     "x_end": xf[:, 0].tolist(), "u_controller": [force],
                     "u_applied_start": applied[:, 0].tolist(), "w_start": w[:, 0].tolist(),
                     "solver_status": status, "is_feasible": bool(feasible),
                     "cost_available": bool(cost_available), "cost": cost if cost_available else None,
                     "cycles": cycles, "instret": instret, "heap_peak": peak, "heap_capacity": heap})
        print(f"CARTPOLE_PLANT_STEP k={k} t={tf:.17g} x={xf[:, 0].tolist()} u={force:.17g} status={status}",
              file=sys.stderr, flush=True)
        x, applied, previous = xf, u.copy(), u.copy()
    write_json(args.out / "capture.json", {"schema": 1, "controller": "cartpole",
               "complete": True, "steps": rows})


def execute_pair(command, args, environment, out, manifest):
    """Two persistent processes, two anonymous pipes; no sockets or daemons."""
    request_read, request_write = os.pipe()
    reply_read, reply_write = os.pipe()
    descriptors = [request_read, request_write, reply_read, reply_write]
    processes = []
    started = time.monotonic()
    try:
        with (out / "plant.log").open("w") as plant_log, (out / "console.log").open("w") as log:
            host = subprocess.Popen(manifest["plant_command"], stdin=reply_read,
                                    stdout=request_write, stderr=plant_log,
                                    env=environment, cwd=out, start_new_session=True)
            processes.append(host)
            if args.mode == "verilator":
                environment.update(MPC_REQUEST_FD=str(request_read), MPC_RESPONSE_FD=str(reply_write))
                service = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                                           pass_fds=(request_read, reply_write), env=environment,
                                           cwd=out, start_new_session=True)
            else:
                service = subprocess.Popen(command, stdin=request_read, stdout=reply_write, stderr=log,
                                           env=environment, cwd=out, start_new_session=True)
            processes.append(service)
            for fd in descriptors: os.close(fd)
            descriptors.clear()
            while any(p.poll() is None for p in processes):
                require(all(p.poll() in (None, 0) for p in processes),
                        "Plant/controller failed: see plant.log and console.log")
                require(time.monotonic() - started < args.timeout, "Plant/controller wall timeout")
                time.sleep(.05)
            manifest.update(returncode=service.returncode, plant_returncode=host.returncode)
            require(host.returncode == service.returncode == 0,
                    "Plant/controller failed: see plant.log and console.log")
    finally:
        for fd in descriptors: os.close(fd)
        for process in processes:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def channel_hashes():
    return {name: sha(ROOT / "src" / name) for name in (
        "acc/acc_channel.hpp", "acc/ACC_Step_Types.h", "acc/acc_capture.hpp",
        "cartpole/cartpole_channel.hpp", "common/cva6_profile.h")}


def prepare_runner(args):
    """Recompile the C++ testbench around the existing generated RTL model."""
    source, build, out = args.source.resolve(), args.runner_build.resolve(), args.out.resolve()
    require(out.is_relative_to(ROOT / "build"), "Runner output must be inside build/")
    require(not out.exists(), "Runner output exists; use another --out or remove that generated directory")
    text = source.read_text()
    # Refuse unknown testbench shapes instead of applying an approximate patch.
    require("#define MEM " in text and "0x80000000" in text,
            "Unsupported SRAM mapping in CVA6 testbench")
    anchors = {
        '#include <stdio.h>': '#include "cva6_bridge.hpp"\n#include <stdio.h>',
        '  while ((ignore_dtm_done || !dtm->done()) &&':
            '  MPCBridge mpc_bridge(reinterpret_cast<unsigned char*>(MEM), sizeof(MEM));\n'
            '  while ((ignore_dtm_done || !dtm->done()) &&',
        '    main_time++;\n    if (main_time % progress_interval':
            '    main_time++;\n'
            '    try { mpc_bridge.tick(); }\n'
            '    catch (const std::exception& error) {\n'
            '      std::cerr << "MPC_CHANNEL_ERROR: " << error.what() << "\\n";\n'
            '      return 90;\n'
            '    }\n    if (main_time % progress_interval',
    }
    for old, new in anchors.items():
        require(text.count(old) == 1, "Unsupported testbench anchor: " + old)
        text = text.replace(old, new)
    # atoll parses a signed 64-bit limit. Accept larger budgets only when both
    # host and generated RTL timeout counters have verified 64-bit storage.
    generated = (build / "Variane_testharness___024root.h").read_text()
    wide_counters = ("uint64_t max_cycles =" in text and "vluint64_t main_time =" in text and
                     "max_cycles = atoll(optarg)" in text and
                     all(re.search(r"QData/\*63:0\*/\s+\w+__DOT__i_rvfi_tracer__DOT__" +
                                   field + r";", generated) for field in ("cycles", "SIM_FINISH")))
    max_cycles_limit = (2**63 - 2) if wide_counters else (2**32 - 2)
    original = (build / "Variane_testharness.mk").read_text()
    pattern = r"^ariane_tb\.o: .+$"
    require(len(re.findall(pattern, original, re.M)) == 1, "Unsupported generated makefile")
    require((build / "Variane_testharness").is_file(), "Missing original runner")
    require(not build.is_relative_to(out), "Runner destination contains source build")
    shutil.copytree(build, out, symlinks=True)
    patched = out / "ariane_tb_mpc.cpp"
    patched.write_text(text)
    for name in ("acc_channel.hpp", "ACC_Step_Types.h", "acc_capture.hpp"):
        shutil.copy2(ROOT / "src/acc" / name, out / name)
    shutil.copy2(ROOT / "src/cartpole/cartpole_channel.hpp", out / "cartpole_channel.hpp")
    shutil.copy2(ROOT / "src/common/cva6_profile.h", out / "cva6_profile.h")
    shutil.copy2(ROOT / "scripts/cva6_bridge.hpp", out / "cva6_bridge.hpp")
    # Existing dump-enabled testbenches may include this header as well.
    for header in source.parent.glob("*.hpp"):
        if not (out / header.name).exists(): shutil.copy2(header, out / header.name)
    modified = re.sub(pattern, "ariane_tb.o: " + str(patched), original, flags=re.M)
    modified = modified.replace("-std=c++17", "-std=c++20")
    (out / "Variane_testharness.mk").write_text(modified)
    (out / "ariane_tb.o").unlink(missing_ok=True)
    (out / "ariane_tb.d").unlink(missing_ok=True)
    command = ["make", "-C", str(out), "-f", "Variane_testharness.mk",
               "Variane_testharness", "OBJCACHE=", "OPT_FAST=-O3", "-j2"]
    subprocess.run(command, check=True)
    write_json(out / "mpc-channel-provenance.json", {
        "testbench": str(source), "testbench_sha256": sha(source),
        "original_runner": str(build / "Variane_testharness"),
        "original_runner_sha256": sha(build / "Variane_testharness"),
        "runner_sha256": sha(out / "Variane_testharness"),
        "bridge_sha256": sha(ROOT / "scripts/cva6_bridge.hpp"),
        "channel_sha256": channel_hashes(), "command": command,
        "max_cycles_limit": max_cycles_limit,
        "generated_counters_sha256": sha(build / "Variane_testharness___024root.h"),
        "rtl_changed": False, "cache_contract": "CVA6 CSR 0x7c1 bypass during exchange only"})
    print("CVA6_RUNNER=" + str(out / "Variane_testharness"))


def run(args):
    require(not args.exact or args.reference is not None, "--exact requires --reference")
    require(args.reference is None or args.reference.is_file(),
            "Reference capture not found or not a file: " + str(args.reference))
    elf, out = args.elf.resolve(), args.out.resolve()
    cartpole = args.controller == "cartpole"
    plant = ROOT / "src/dynamics" / (args.controller + ".py")
    dependencies = python_environment()
    require(out.is_relative_to(ROOT / "build"), "Execution output must be inside build/")
    require(1 <= args.steps <= 64 and args.timeout > 0, "Invalid steps/timeout")
    out.mkdir(parents=True, exist_ok=True)
    capture = out / ("capture.json" if cartpole else "capture.bin")
    capture.unlink(missing_ok=True)
    (out / "comparison.json").unlink(missing_ok=True)
    manifest = {"status": "RUNNING", "controller": args.controller, "mode": args.mode, "steps": args.steps,
                "elf": str(elf), "elf_sha256": sha(elf), "sources_sha256": source_hash(args.controller),
                "plant": str(plant), "plant_sha256": sha(plant),
                "python_environment": dependencies,
                "integrator": "original OdeDynamics.evolve_state / scipy.integrate.solve_ivp defaults",
                "architecture": "host plant + persistent controller service",
                "control_calendar": "one solve per sample; new control applied in next interval; no measured delays",
                "build_config": (elf.parent / "build-config.txt").read_text(),
                "target_counters_available": args.mode != "x86", "heap_available": args.mode != "x86"}
    write_json(out / "run.json", manifest)
    print("RESULT_DIR=" + str(out), flush=True)
    started = time.monotonic()
    try:
        environment = os.environ.copy()
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        if args.mode == "x86":
            command = [str(elf)]
        elif args.mode == "spike":
            require(shutil.which(args.spike), "Spike executable not found")
            command = [args.spike, "--isa=RV64GC", "--priv=m",
                       "-m0x80000000:0x10000000", str(elf)]
        else:
            require(args.runner, "Set CVA6_RUNNER to the shared MPC channel runner")
            runner = shutil.which(args.runner)
            require(runner is not None, "CVA6 runner not found/executable")
            require(b"RSHARC_MPC_CHANNEL_V1" in Path(runner).read_bytes(),
                    "Incompatible runner: run make prepare-runner first")
            provenance = json.loads(Path(runner).with_name("mpc-channel-provenance.json").read_text())
            require(provenance["runner_sha256"] == sha(runner) and
                    provenance["bridge_sha256"] == sha(ROOT / "scripts/cva6_bridge.hpp") and
                    provenance["channel_sha256"] == channel_hashes(),
                    "Runner/channel changed: regenerate the shared MPC runner")
            require(1 <= args.max_cycles <= provenance["max_cycles_limit"] and 0 <= args.seed <= 4294967295,
                    "Runner cycle/seed parameters outside supported range")
            syms = symbols(elf, args.prefix, args.controller)
            address, size = syms["rsharc_" + args.controller + "_mailbox"]
            environment.update(MPC_MAILBOX_ADDRESS=hex(address), MPC_CONTROLLER=args.controller)
            command = [runner, f"--max-cycles={args.max_cycles}",
                       f"--seed={args.seed}", str(elf),
                       "+debug_disable=1", "+jtag_rbb_enable=0", "+trace_disable=1",
                       "+progress_interval=5000000", f"+time_out={args.max_cycles}",
                       f"+ntb_random_seed={args.seed}", "++" + str(elf), "+elf_file=" + str(elf),
                       "+core_name=cv64a6_imafdc_sv39", f"+tohost_addr={syms['tohost'][0]:016x}"]
            manifest.update(runner=str(Path(runner).resolve()), runner_sha256=sha(runner),
                            max_cycles=args.max_cycles, seed=args.seed,
                            runner_libraries=subprocess.check_output(["ldd", runner], text=True))
        manifest.update(command=command, plant_command=[sys.executable, "-B",
            str(Path(__file__).resolve()), "plant", "--controller", args.controller,
            "--steps", str(args.steps), "--out", str(out)])
        write_json(out / "run.json", manifest)
        execute_pair(command, args, environment, out, manifest)
        if args.mode == "verilator":
            log_text = (out / "console.log").read_text()
            require("RSHARC_MPC_CHANNEL_V1" in log_text and
                    "*** SUCCESS *** (tohost = 0)" in log_text,
                    "Runner did not confirm channel activation and successful termination")
        rows = decode_cartpole(capture, args.steps) if cartpole else decode(capture, args.steps)
        if args.mode != "x86":
            require(all(r["cycles"] > 0 and r["instret"] > 0 for r in rows), "Missing RV64 counters")
        if cartpole:
            manifest.update(scope="64 or fewer closed-loop intervals and controller execution; stability not certified",
                            solver_error_steps=[r["k"] for r in rows if r["solver_status"] < 0],
                            infeasible_steps=[r["k"] for r in rows if not r["is_feasible"]],
                            final_state=rows[-1]["x_end"])
        else:
            write_json(out / "runtime_result.json", {"steps": rows})
        manifest.update(status="PASS", capture_sha256=sha(capture))
        if args.reference:
            if cartpole:
                check = comparison_cartpole(rows, checked_capture(args.reference, controller="cartpole"))
            else:
                check = comparison(rows, reference_rows(args.reference))
            write_json(out / "comparison.json", check)
            manifest.update(reference=str(args.reference.resolve()), reference_sha256=sha(args.reference))
            manifest["math_exact"] = check["all_available_math_fields_exact"]
            print("MATH_EXACT=" + str(manifest["math_exact"]), flush=True)
            if not cartpole:
                require(check["historical_tolerances_pass"], "Numerical comparison failed: see comparison.json")
                print("HISTORICAL_TOLERANCES=PASS", flush=True)
            require(not args.exact or manifest["math_exact"], "Exact comparison failed: see comparison.json")
        if cartpole:
            print("SOLVER_ERROR_STEPS=" + str(manifest["solver_error_steps"]), flush=True)
            print("INFEASIBLE_STEPS=" + str(manifest["infeasible_steps"]), flush=True)
        outcome = "EXECUTION_PASS" if cartpole else "PASS"
        print(f"{args.controller.upper()}_{args.mode.upper()}_STEPS={len(rows)} {outcome}", flush=True)
    except BaseException as error:
        manifest.update(status="FAIL", error=str(error))
        raise
    finally:
        manifest["host_seconds"] = time.monotonic() - started
        write_json(out / "run.json", manifest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    execute = sub.add_parser("run")
    execute.add_argument("--mode", choices=("x86", "verilator", "spike"), required=True)
    execute.add_argument("--elf", type=Path, required=True)
    execute.add_argument("--out", type=Path, required=True)
    execute.add_argument("--spike", default="spike")
    execute.add_argument("--steps", type=int, default=64)
    execute.add_argument("--prefix", default="riscv64-unknown-elf")
    execute.add_argument("--runner", default="")
    execute.add_argument("--max-cycles", type=int, default=4000000000)
    execute.add_argument("--timeout", type=int, default=86400)
    execute.add_argument("--seed", type=int, default=475092611)
    execute.add_argument("--reference", type=Path)
    execute.add_argument("--exact", action="store_true")
    compare = sub.add_parser("compare")
    compare.add_argument("--observed", type=Path, required=True)
    compare.add_argument("--reference", type=Path, required=True)
    compare.add_argument("--steps", type=int, default=64)
    compare.add_argument("--exact", action="store_true")
    prepare = sub.add_parser("prepare-runner")
    prepare.add_argument("--source", type=Path, required=True)
    prepare.add_argument("--runner-build", type=Path, required=True)
    prepare.add_argument("--out", type=Path, required=True)
    plant = sub.add_parser("plant")
    plant.add_argument("--steps", type=int, required=True)
    plant.add_argument("--out", type=Path, required=True)
    for command in (execute, compare, plant):
        command.add_argument("--controller", choices=("acc", "cartpole"), default="acc")
    args = parser.parse_args()
    if args.action == "run":
        run(args)
    elif args.action == "plant":
        if args.controller == "cartpole": run_cartpole_plant(args)
        else: run_plant(args)
    elif args.action == "prepare-runner":
        prepare_runner(args)
    else:
        if args.controller == "cartpole":
            check = comparison_cartpole(checked_capture(args.observed, args.steps, "cartpole"),
                                        checked_capture(args.reference, controller="cartpole"))
        else:
            check = comparison(checked_capture(args.observed, args.steps), reference_rows(args.reference))
        check.update(observed_sha256=sha(args.observed), reference_sha256=sha(args.reference))
        write_json(args.observed.with_name("comparison.json"), check)
        print(json.dumps(check, indent=2))
        if args.controller == "acc":
            require(check["historical_tolerances_pass"], "Historical tolerance comparison failed")
        require(not args.exact or check["all_available_math_fields_exact"], "Exact comparison failed")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, ImportError, subprocess.SubprocessError) as error:
        sys.exit("MPC_RUN_ERROR: " + str(error))
