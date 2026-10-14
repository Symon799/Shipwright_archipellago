#!/usr/bin/env python3
"""Run the versioned validation suites without an AI or game assets."""

import argparse
import datetime
import json
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import time
import unittest
import uuid
from pathlib import Path
from xml.etree.ElementTree import ParseError as ETParseError

from validation_support import overall_status, read_junit, run_unittest_suite

ROOT = Path(__file__).resolve().parents[1]


def executable(name, fallback=None):
    found = shutil.which(name)
    if found:
        return found
    return str(fallback) if fallback and fallback.is_file() else None


def dependency_header(argument, env_name, candidates, header):
    explicit = argument or os.environ.get(env_name)
    if explicit:
        directory = Path(explicit).resolve()
        return str(directory) if (directory / header).is_file() else None
    return next((str(path.resolve()) for path in candidates if (path / header).is_file()), None)


def discover(args):
    game_build = args.game_build_dir.resolve()
    vcpkg = Path(os.environ.get("VCPKG_ROOT", Path.home() / "vcpkg"))
    json_include = dependency_header(args.json_include_dir, "SOH_JSON_INCLUDE_DIR", [
        vcpkg / "installed/x64-windows-static/include", vcpkg / "installed/x64-windows/include",
        game_build / "_deps/json-src/include", Path("/usr/include"), Path("/usr/local/include"),
    ], "nlohmann/json.hpp")
    imgui_include = dependency_header(args.imgui_include_dir, "SOH_IMGUI_INCLUDE_DIR", [
        game_build / "_deps/imgui-src", ROOT / "build-cmake/_deps/imgui-src",
        ROOT / "build-windows/_deps/imgui-src",
    ], "imgui.h")
    cmake = executable("cmake", Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "CMake/bin/cmake.exe")
    ctest = executable("ctest", Path(cmake).with_name("ctest.exe" if os.name == "nt" else "ctest") if cmake else None)
    toolchain = shutil.which("cl") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if os.name == "nt" and not toolchain:
        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / \
            "Microsoft Visual Studio/Installer/vswhere.exe"
        if vswhere.is_file():
            query = subprocess.run([str(vswhere), "-latest", "-requires",
                                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                                   capture_output=True, text=True, timeout=15)
            toolchain = query.stdout.strip() or None
    imgui_version = None
    if imgui_include:
        match = re.search(r'#define IMGUI_VERSION\s+"([^"]+)"',
                          (Path(imgui_include) / "imgui.h").read_text(encoding="utf-8", errors="replace"))
        imgui_version = match.group(1) if match else "unknown"
    return {"python": sys.executable, "python_version": platform.python_version(), "platform": platform.platform(),
            "cmake": cmake, "ctest": ctest, "toolchain": toolchain, "json_include": json_include,
            "imgui_include": imgui_include, "imgui_version": imgui_version}


class Validation:
    def __init__(self, args):
        self.args = args
        self.directory = args.report_dir.resolve() / (datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S")
                                                      + "-" + uuid.uuid4().hex[:6])
        self.directory.mkdir(parents=True)
        try:
            self.environment = discover(args)
        except (OSError, subprocess.SubprocessError) as error:
            self.environment = dict.fromkeys(("cmake", "ctest", "toolchain", "json_include", "imgui_include"))
            self.environment["discovery_error"] = str(error)
        self.report = {"suite": args.suite, "configuration": args.configuration, "environment": self.environment,
                       "commands": [], "suites": [], "limitations": [
                           "AP transport/session fixtures do not prove item delivery, shops or the complete reachability graph.",
                           "Packaging fixtures do not prove AppImage/DMG installation or rendering on a real GPU."]}
        git = shutil.which("git")
        if git:
            revision = subprocess.run([git, "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True, timeout=15)
            dirty = subprocess.run([git, "--no-optional-locks", "status", "--porcelain", "--ignore-submodules"],
                                   cwd=ROOT, capture_output=True, text=True, timeout=15)
            self.report["commit"] = revision.stdout.strip() if revision.returncode == 0 else None
            self.report["dirty"] = bool(dirty.stdout.strip()) if dirty.returncode == 0 else None

    def command(self, name, command, timeout=120, cwd=ROOT, env=None):
        started = time.perf_counter()
        print(f"Running {name}...", flush=True)
        process = subprocess.Popen([str(arg) for arg in command], cwd=cwd, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace",
                                   start_new_session=os.name != "nt")
        try:
            stdout, stderr = process.communicate(timeout=timeout)
            output = stdout + stderr
            code = process.returncode
        except (subprocess.TimeoutExpired, KeyboardInterrupt) as error:
            # Kill only this command tree, not other builds or the user's game.
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True, timeout=15)
            else:
                os.killpg(process.pid, signal.SIGKILL)
            process.kill()
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.stdout.close()
                process.stderr.close()
            if isinstance(error, KeyboardInterrupt):
                raise
            output = f"TIMEOUT after {timeout}s\n{error}"
            code = 124
        logfile = self.directory / f"{name}.log"
        logfile.write_text(output, encoding="utf-8")
        self.report["commands"].append({"name": name, "argv": [str(arg) for arg in command], "exit_code": code,
                                       "cwd": str(cwd),
                                       "duration_seconds": round(time.perf_counter() - started, 3), "log": str(logfile)})
        if code:
            print(output[-4000:], flush=True)
        return code, output

    def add(self, name, status, reason=None, **details):
        row = {"name": name, "status": status, **details}
        if reason:
            row["reason"] = reason
        self.report["suites"].append(row)
        print(f"{name}: {status}" + (f" ({reason})" if reason else ""), flush=True)

    def fast(self):
        git = shutil.which("git")
        if git:
            code, _ = self.command("diff-check", [git, "--no-optional-locks", "diff", "--ignore-submodules",
                                                  "--check", "HEAD", "--"])
            self.add("diff-check", "FAILED" if code else "PASSED")
        else:
            self.add("diff-check", "BLOCKED", "Git is missing")
        required = ("cmake", "ctest", "toolchain", "json_include", "imgui_include")
        missing = [key for key in required if not self.environment[key]]
        if missing:
            self.add("cpp", "BLOCKED", "Missing prerequisites: " + ", ".join(missing) + ". Run check-env.")
        else:
            self.cpp()
        self.python("python-fast", ["test_export_map_tracker_references.py", "test_validation_support.py",
                                    "test_game_validation.py"])

    def cpp(self, label="fast"):
        configurations = ("Debug", "Release") if self.args.configuration == "Both" else (self.args.configuration,)
        cmake, ctest = self.environment["cmake"], self.environment["ctest"]
        build = self.args.build_dir.resolve()
        generator = self.args.generator or ("Visual Studio 17 2022" if os.name == "nt" else
                                            ("Ninja" if shutil.which("ninja") else "Unix Makefiles"))
        network = label == "integration"
        if network:
            missing = [name for name in ("apclientpp", "wswrap", "asio", "websocketpp", "valijson")
                       if not (self.args.game_build_dir / "_deps" / f"{name}-src").is_dir()]
            if missing:
                self.add("ap-transport", "BLOCKED", "Missing existing dependencies: " + ", ".join(missing))
                network = False
        for config in configurations:
            configure = [cmake, "-S", ROOT / "tests", "-B", build, "-G", generator,
                         f"-DSOH_GAME_BUILD_DIR={self.args.game_build_dir.resolve()}",
                         f"-DSOH_JSON_INCLUDE_DIR={self.environment['json_include']}",
                         f"-DSOH_VALIDATION_NETWORK={'ON' if network else 'OFF'}",
                         f"-DPython3_EXECUTABLE={sys.executable}",
                         f"-DSOH_IMGUI_INCLUDE_DIR={self.environment['imgui_include']}"]
            if generator.startswith("Visual Studio"):
                configure += ["-A", "x64"]
            elif generator != "Ninja Multi-Config":
                configure += [f"-DCMAKE_BUILD_TYPE={config}"]
            prefix = f"{label}-{config}"
            code, _ = self.command(f"configure-{prefix}", configure)
            if code:
                self.add(f"cpp-{prefix}", "FAILED", "Configure failed; no tests were run")
                continue
            code, _ = self.command(f"build-{prefix}", [cmake, "--build", build, "--config", config,
                                                       "--parallel", self.args.jobs], timeout=300)
            if code:
                self.add(f"cpp-{prefix}", "FAILED", "Build failed; no tests were run")
                continue
            code, output = self.command(f"discover-{prefix}", [ctest, "--test-dir", build, "-C", config,
                                                                 "-L", label, "--show-only=json-v1"])
            discovered = json.loads(output)["tests"] if code == 0 else []
            expected = {"calibration", "display_modes", "check_totals", "viewport_ownership", "checks_active"}
            if os.name == "nt" and label == "fast":
                expected.add("window_shutdown")
            if label == "integration":
                expected = {"archipelago_session", "pack_schema"}
                if network:
                    expected.add("ap_transport")
            if {test["name"] for test in discovered} != expected:
                self.add(f"cpp-{prefix}", "FAILED", "Required test discovery does not match the versioned suite")
                continue
            junit = self.directory / f"cpp-{prefix}.xml"
            code, _ = self.command(f"ctest-{prefix}", [ctest, "--test-dir", build, "-C", config, "-L", label,
                                                       "--output-on-failure", "--no-tests=error", "--output-junit", junit])
            counts = read_junit(junit) if junit.is_file() else {"tests": 0, "failed": 0, "skipped": 0}
            status = "FAILED" if code or counts["failed"] or counts["tests"] != len(expected) else \
                ("SKIPPED" if counts["skipped"] else "PASSED")
            self.add(f"cpp-{prefix}", status, **counts, junit=str(junit))

    def python(self, name, patterns):
        started = time.perf_counter()
        suite = unittest.TestSuite()
        for pattern in patterns:
            discovered = unittest.defaultTestLoader.discover(str(ROOT / "scripts/tests"), pattern=pattern)
            if not discovered.countTestCases():
                raise ValueError(f"Required test file discovered zero tests: {pattern}")
            suite.addTests(discovered)
        result = run_unittest_suite(suite, self.directory / f"{name}.xml", self.directory / f"{name}.log")
        self.add(name, **result, junit=str(self.directory / f"{name}.xml"),
                 duration_seconds=round(time.perf_counter() - started, 3))

    def run(self):
        started = time.perf_counter()
        try:
            if self.args.suite == "env":
                missing = [key for key in ("cmake", "ctest", "toolchain", "json_include", "imgui_include")
                           if not self.environment[key]]
                print(json.dumps(self.environment, indent=2))
                self.add("environment", "BLOCKED" if missing else "PASSED", ", ".join(missing) or None)
            if self.args.suite in ("fast", "all"):
                self.fast()
            if self.args.suite in ("integration", "all"):
                missing = [key for key in ("cmake", "ctest", "toolchain", "json_include", "imgui_include")
                           if not self.environment[key]]
                if missing:
                    self.add("cpp-integration", "BLOCKED", "Missing prerequisites: " + ", ".join(missing))
                else:
                    self.cpp("integration")
                self.python("release-packaging", ["test_release_packaging.py"])
            if self.args.suite in ("e2e", "all"):
                if self.args.game_exe:
                    from game_validation import remove_archive_links, run_scenarios
                    try:
                        run_scenarios(self, self.args.game_exe, self.args.game_assets or self.args.game_exe.parent,
                                      self.args.e2e_scenarios)
                    finally:
                        remove_archive_links(self.directory)
                else:
                    self.add("game-e2e", "NOT_RUN", "Opt in with --game-exe or SOH_E2E_EXE. "
                             "No personal saves or game assets were accessed.")
        except (OSError, ValueError, subprocess.SubprocessError, ETParseError) as error:
            self.add("runner", "FAILED", str(error))
        except KeyboardInterrupt:
            self.add("runner", "NOT_RUN", "Interrupted; validation is incomplete")
        status, code = overall_status(self.report["suites"])
        self.report["status"] = status
        self.report["exit_code"] = code
        self.report["duration_seconds"] = round(time.perf_counter() - started, 3)
        path = self.directory / "summary.json"
        path.write_text(json.dumps(self.report, indent=2), encoding="utf-8")
        print(f"Validation: {status}. Report: {path}")
        return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suite", choices=("env", "fast", "integration", "e2e", "all"))
    parser.add_argument("--configuration", choices=("Debug", "Release", "Both"), default="Both")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/validation")
    parser.add_argument("--report-dir", type=Path, default=ROOT / "build/validation-reports")
    parser.add_argument("--game-build-dir", type=Path, default=ROOT / "build/x64-v143")
    parser.add_argument("--json-include-dir")
    parser.add_argument("--imgui-include-dir")
    parser.add_argument("--generator")
    parser.add_argument("--game-exe", type=Path, default=os.environ.get("SOH_E2E_EXE"))
    parser.add_argument("--game-assets", type=Path, help="Local archive directory; defaults to the test EXE directory")
    parser.add_argument("--e2e-scenarios", nargs="+", choices=("pause-3d", "pause-2d", "exit-loading-3d",
                                                            "exit-loading-2d", "pack-invalid"))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    return Validation(args).run()


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
