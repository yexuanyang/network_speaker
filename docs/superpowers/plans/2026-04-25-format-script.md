# Format Script Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create `tools/format.py`, a zero-dependency Python script that runs clang-format, clang-tidy, cargo fmt, tsc type-check, dotnet format, and Android lint across the project, supporting both fix and check (dry-run) modes.

**Architecture:** Single file, three layers — a `Checker` dataclass registry, an execution engine that probes for tools and runs commands, and a `main()` CLI entry point using `argparse`. Missing tools are skipped with a warning; exit code is `0` only if every active checker passes.

**Tech Stack:** Python 3.8+ standard library only (`argparse`, `shutil`, `subprocess`, `pathlib`, `dataclasses`).

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `tools/format.py` | Entire script: CLI, checker registry, execution engine |

---

### Task 1: Write `tools/format.py`

**Files:**
- Create: `tools/format.py`

- [ ] **Step 1: Create the file with this exact content**

```python
#!/usr/bin/env python3
"""Cross-platform code formatting and linting script for network_speaker.

Usage:
    python tools/format.py                        # fix all
    python tools/format.py --check                # check all (CI mode)
    python tools/format.py cpp rust --check       # check specific checkers
    python tools/format.py --skip tidy android    # fix, skip slow checkers
    python tools/format.py cpp rust               # fix specific checkers
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List, Optional

ROOT = Path(__file__).resolve().parent.parent

PASS, FAIL, SKIP = "pass", "fail", "skip"
ICON = {PASS: "✓", FAIL: "✗", SKIP: "⚠"}


# ──────────────────────────── utilities ────────────────────────────────────────

def _find_cpp_files() -> List[str]:
    files: List[str] = []
    for dir_name in ("libs", "server", "client", "tests"):
        d = ROOT / dir_name
        if d.is_dir():
            for f in d.rglob("*"):
                if f.suffix in (".cpp", ".h"):
                    files.append(str(f))
    return sorted(files)


def _dotnet_projects() -> List[str]:
    base = ROOT / "apps" / "windows-launcher"
    return [
        str(base / "NetworkSpeaker.Launcher" / "NetworkSpeaker.Launcher.csproj"),
        str(base / "NetworkSpeaker.Launcher.Core" / "NetworkSpeaker.Launcher.Core.csproj"),
        str(
            base
            / "NetworkSpeaker.Launcher.Core.Tests"
            / "NetworkSpeaker.Launcher.Core.Tests.csproj"
        ),
    ]


def _run(cmd: List[str], cwd: Optional[Path] = None) -> int:
    return subprocess.run(cmd, cwd=cwd).returncode


# ──────────────────────────── checker ──────────────────────────────────────────

@dataclass
class Checker:
    key: str
    name: str
    required_bins: List[str]
    # Each callable returns a list of commands (each command is a list of strings).
    # An empty list means "nothing to do" → treated as pass.
    check_cmds: Callable[[], List[List[str]]]
    fix_cmds: Optional[Callable[[], List[List[str]]]]  # None = no auto-fix
    cwd: Optional[Path] = None
    # Returns an error string if the environment is not ready, else None.
    precondition: Optional[Callable[[], Optional[str]]] = None

    def run(self, check_mode: bool) -> tuple[str, str]:
        missing = [b for b in self.required_bins if not shutil.which(b)]
        if missing:
            return SKIP, f"{', '.join(missing)} not found, skipped"

        if self.precondition:
            err = self.precondition()
            if err:
                return SKIP, err

        no_autofix = not check_mode and self.fix_cmds is None
        if no_autofix:
            print("  (no auto-fix available, running check only)")

        cmds = self.check_cmds() if (check_mode or self.fix_cmds is None) else self.fix_cmds()
        for cmd in cmds:
            if _run(cmd, cwd=self.cwd) != 0:
                return FAIL, "failed (see output above)"

        return PASS, "check passed" if (check_mode or self.fix_cmds is None) else "formatted"


# ──────────────────────────── registry ─────────────────────────────────────────

CHECKERS: List[Checker] = [
    Checker(
        key="cpp",
        name="C++ clang-format",
        required_bins=["clang-format"],
        check_cmds=lambda: (
            [["clang-format", "--dry-run", "--Werror"] + _find_cpp_files()]
            if _find_cpp_files()
            else []
        ),
        fix_cmds=lambda: (
            [["clang-format", "-i"] + _find_cpp_files()] if _find_cpp_files() else []
        ),
    ),
    Checker(
        key="tidy",
        name="C++ clang-tidy",
        required_bins=["clang-tidy"],
        check_cmds=lambda: (
            [["clang-tidy", "-p", str(ROOT / "build"), "--quiet"] + _find_cpp_files()]
            if _find_cpp_files()
            else []
        ),
        fix_cmds=None,
        precondition=lambda: (
            None
            if (ROOT / "build" / "compile_commands.json").exists()
            else "build/compile_commands.json not found; run cmake first"
        ),
    ),
    Checker(
        key="rust",
        name="Rust cargo fmt",
        required_bins=["cargo"],
        check_cmds=lambda: [["cargo", "fmt", "--check"]],
        fix_cmds=lambda: [["cargo", "fmt"]],
        cwd=ROOT / "apps" / "desktop" / "src-tauri",
    ),
    Checker(
        key="frontend",
        name="Frontend tsc",
        required_bins=["npm"],
        check_cmds=lambda: [["npm", "run", "build"]],
        fix_cmds=None,
        cwd=ROOT / "apps" / "desktop",
        precondition=lambda: (
            None
            if (ROOT / "apps" / "desktop" / "node_modules").exists()
            else "node_modules not found; run: cd apps/desktop && npm ci"
        ),
    ),
    Checker(
        key="dotnet",
        name=".NET dotnet format",
        required_bins=["dotnet"],
        check_cmds=lambda: [
            ["dotnet", "format", "--verify-no-changes", p] for p in _dotnet_projects()
        ],
        fix_cmds=lambda: [["dotnet", "format", p] for p in _dotnet_projects()],
    ),
    Checker(
        key="android",
        name="Android gradlew lint",
        required_bins=["java"],
        check_cmds=lambda: [[
            "gradlew.bat" if sys.platform == "win32" else "./gradlew",
            "lint",
            "--no-daemon",
        ]],
        fix_cmds=None,
        cwd=ROOT / "client" / "android-app",
    ),
]

CHECKER_MAP = {c.key: c for c in CHECKERS}
_PAD = max(len(c.name) for c in CHECKERS)


def _label(name: str) -> str:
    return f"[{name:<{_PAD}}]"


# ──────────────────────────── entry point ──────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Format and lint all code in network_speaker.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"Available checkers: {', '.join(CHECKER_MAP)}",
    )
    parser.add_argument(
        "targets",
        nargs="*",
        metavar="TARGET",
        help="Checkers to run. Default: all.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Dry-run mode: report differences without modifying files.",
    )
    parser.add_argument(
        "--skip",
        nargs="+",
        metavar="KEY",
        default=[],
        help="Skip specified checkers (space-separated).",
    )
    args = parser.parse_args()

    invalid = [t for t in args.targets if t not in CHECKER_MAP]
    invalid += [s for s in args.skip if s not in CHECKER_MAP]
    if invalid:
        parser.error(f"unknown checker(s): {', '.join(invalid)}")

    active = (
        [CHECKER_MAP[t] for t in args.targets]
        if args.targets
        else [c for c in CHECKERS if c.key not in args.skip]
    )

    results: dict[str, tuple[str, str]] = {}
    for checker in active:
        print(f"\n{_label(checker.name)} running...")
        status, msg = checker.run(check_mode=args.check)
        results[checker.key] = (status, msg)
        print(f"{_label(checker.name)} {ICON[status]} {msg}")

    passed = sum(1 for s, _ in results.values() if s == PASS)
    failed = sum(1 for s, _ in results.values() if s == FAIL)
    skipped = sum(1 for s, _ in results.values() if s == SKIP)
    print(f"\nSummary: {passed} passed, {failed} failed, {skipped} skipped")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Verify the script is importable and --help works**

Run:
```bash
cd /path/to/network_speaker
python tools/format.py --help
```

Expected output (paraphrased):
```
usage: format.py [-h] [--check] [--skip KEY [KEY ...]] [TARGET ...]

Format and lint all code in network_speaker.
...
Available checkers: cpp, tidy, rust, frontend, dotnet, android
```

If you see `SyntaxError` or `ImportError`, fix before continuing.

---

### Task 2: Verify `cpp` checker

**Files:**
- Read: `tools/format.py` (already created)

- [ ] **Step 1: Run check mode on cpp only**

Run:
```bash
python tools/format.py cpp --check
```

Expected: `[C++ clang-format] ✓ check passed` (if code is already formatted)  
or: `[C++ clang-format] ✗ failed (see output above)` with clang-format diff output  
or: `[C++ clang-format] ⚠ clang-format not found, skipped` if tool not installed

- [ ] **Step 2: Run fix mode on cpp**

Run:
```bash
python tools/format.py cpp
```

Expected: `[C++ clang-format] ✓ formatted` (formats in place, exit 0)

---

### Task 3: Verify `rust` checker

- [ ] **Step 1: Run check mode on rust only**

Run:
```bash
python tools/format.py rust --check
```

Expected: `[Rust cargo fmt] ✓ check passed`  
or: `⚠ cargo not found, skipped` if Rust not installed

- [ ] **Step 2: Run fix mode on rust**

Run:
```bash
python tools/format.py rust
```

Expected: `[Rust cargo fmt] ✓ formatted`

---

### Task 4: Verify `--skip` and `--check` flags

- [ ] **Step 1: Run check mode skipping slow checkers**

Run:
```bash
python tools/format.py --check --skip tidy android
```

Expected: runs cpp, rust, frontend, dotnet only. Summary line shows correct counts.

- [ ] **Step 2: Verify unknown checker is rejected**

Run:
```bash
python tools/format.py --skip unknown 2>&1
```

Expected: `error: unknown checker(s): unknown`

- [ ] **Step 3: Run all available checkers (default, fix mode)**

Run:
```bash
python tools/format.py
```

Expected: all six checkers run in order. Any missing tool shows `⚠ ... not found, skipped`. Summary at end.

---

### Task 5: Commit

- [ ] **Step 1: Stage and commit**

```bash
git add tools/format.py docs/superpowers/specs/2026-04-25-format-script-design.md docs/superpowers/plans/2026-04-25-format-script.md
git commit -m "feat(tools): add cross-platform format.py script

Supports fix and --check modes for C++ (clang-format + clang-tidy),
Rust (cargo fmt), frontend (tsc), .NET (dotnet format), and Android
(gradlew lint). Missing tools are skipped with a warning.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```
