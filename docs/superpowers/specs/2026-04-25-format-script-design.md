# Design: Cross-Platform Code Formatting Script

**Date:** 2026-04-25  
**File:** `tools/format.py`

## Overview

A single Python script (no third-party dependencies) that runs code formatters and linters across all languages in the project. Supports both a fix mode (applies changes in-place) and a check mode (reports differences without modifying files, suitable for CI and pre-commit hooks).

## Command-Line Interface

```
python tools/format.py [TARGETS...] [--check] [--skip KEY [KEY...]]
```

- **TARGETS** (optional, positional): one or more of `cpp`, `tidy`, `rust`, `frontend`, `dotnet`, `android`. If omitted, all checkers run.
- **`--check`**: dry-run mode. No files are modified. Exit code is non-zero if any checker reports a difference or error.
- **`--skip KEY`**: exclude one or more checkers by key. Accepts multiple values.

### Examples

```bash
python tools/format.py                        # fix all
python tools/format.py --check                # check all (CI equivalent)
python tools/format.py cpp rust --check       # check C++ and Rust only
python tools/format.py --skip tidy android    # fix, skip slow checkers
python tools/format.py --skip tidy --check    # check without clang-tidy
```

## Architecture

Three logical layers:

1. **Checker registry** — each language/tool is a `Checker` dataclass with: `key`, human `name`, `required_bins` (list of executables to probe), `fix_cmd` (list of args or callable), `check_cmd`, and optional `precondition` (callable that returns an error string if the environment is not ready, e.g. missing `build/compile_commands.json`).
2. **Execution engine** — iterates checkers in registration order, probes `required_bins` with `shutil.which`, runs the appropriate command via `subprocess.run`, collects stdout/stderr and return code.
3. **CLI entry point** — `argparse` parses arguments, selects the active checker set, runs the engine, prints a per-checker status line and a final summary, exits with code `0` (all pass/skip) or `1` (any failure).

## Checkers

| Key | Fix command | Check command | Required bins | Notes |
|-----|-------------|---------------|---------------|-------|
| `cpp` | `clang-format -i <files>` | `clang-format --dry-run --Werror <files>` | `clang-format` | Files: `libs/`, `server/`, `client/`, `tests/` — `*.cpp`, `*.h` |
| `tidy` | *(same as check)* | `clang-tidy -p build --quiet <files>` | `clang-tidy` | Requires `build/compile_commands.json`; no auto-fix |
| `rust` | `cargo fmt` | `cargo fmt --check` | `cargo` | CWD: `apps/desktop/src-tauri` |
| `frontend` | *(same as check)* | `npm run build` | `npm` | CWD: `apps/desktop`; type-check only, no formatter |
| `dotnet` | `dotnet format <proj>` × 3 | `dotnet format --verify-no-changes <proj>` × 3 | `dotnet` | Runs on all three `.csproj` files sequentially |
| `android` | *(same as check)* | `./gradlew lint --no-daemon` | `java` | CWD: `client/android-app`; no auto-fix |

Checkers where fix == check (`tidy`, `frontend`, `android`) display a note in fix mode: "no auto-fix available, running check only".

## Missing Tool Behavior

If any binary in `required_bins` is not found via `shutil.which`, the checker is skipped and a warning line is printed:

```
[rust    ] ⚠ cargo not found, skipped
```

The checker does not count as a failure. The final exit code is unaffected by skipped checkers.

## Output Format

```
[cpp     ] ✓ 12 files formatted
[tidy    ] ✗ 2 warnings (see above)
[rust    ] ⚠ cargo not found, skipped
[frontend] ✓ type-check passed
[dotnet  ] ✓ no changes
[android ] ✓ lint passed

Summary: 4 passed, 1 failed, 1 skipped
```

Exit code `0` if all active (non-skipped) checkers pass; `1` otherwise.

## File Location

`tools/format.py` — alongside the existing `tools/package-windows-installer.ps1`.

## Non-Goals

- No wrapping of `clang-tidy --fix` (too risky to run automatically).
- No Kotlin/Android auto-formatter (not in the CI workflow).
- No frontend auto-formatter (no formatter configured in the project).
- No watch mode.
