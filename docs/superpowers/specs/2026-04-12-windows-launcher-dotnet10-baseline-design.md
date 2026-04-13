# Windows Launcher `.NET 10` Baseline Design

Date: 2026-04-12

## Context

`plan-1.md` introduced a Windows GUI launcher, MSI packaging, and GitHub Release automation for `hostd.exe`.
That implementation was originally scaffolded around `.NET 8`, but the current Windows development machine already has `.NET 10 SDK` installed and the team wants to make `.NET 10 + Visual Studio Community 2026` the new minimum development baseline for the Windows GUI release pipeline.

The project should therefore stop treating `.NET 8` as required for launcher-related work and instead align the entire Windows GUI, packaging, and release toolchain on `.NET 10`.

## Goals

- Make `.NET 10` the single baseline for the Windows launcher solution.
- Keep the current `plan-1` scope intact:
  - WPF GUI launcher
  - launcher tests
  - MSI packaging
  - GitHub Release automation
- Remove the current hard dependency on `.NET 8 SDK`.
- Keep C++ `hostd` build behavior unchanged except where the packaging pipeline invokes it.
- Update contributor and user-facing documentation so local setup matches the actual supported toolchain.

## Non-Goals

- No redesign of the launcher UI or launcher behavior.
- No change to the C++20/CMake baseline for core audio components.
- No attempt to preserve compatibility with `.NET 8`, older Visual Studio editions, or old release runners.
- No expansion of the MSI feature set beyond what `plan-1` already defines.

## Options Considered

### Option 1: Keep launcher on `.NET 8`

Pros:
- Smallest code change.

Cons:
- Keeps the project blocked on installing an older SDK.
- Conflicts with the desired new minimum baseline.
- Leaves setup and release guidance lagging behind the active development environment.

### Option 2: Use `.NET 10 SDK` but keep targets on `net8.0`

Pros:
- Smaller migration than a full target update.

Cons:
- Still produces a `.NET 8` application.
- Creates confusing documentation and support expectations.
- Does not satisfy the goal of moving the Windows launcher pipeline to `.NET 10`.

### Option 3: Hard-cut the full Windows launcher pipeline to `.NET 10`

Pros:
- Matches the current machine and the desired future baseline.
- Simplifies local setup, CI configuration, and release guidance.
- Removes the immediate `.NET 8` installation blocker.

Cons:
- Drops compatibility with older environments.
- Requires updating build, test, packaging, and docs together.

## Decision

Choose **Option 3**.

The Windows launcher, launcher tests, local packaging script, and release workflow will all move to `.NET 10`. The project will explicitly document `.NET 10 + Visual Studio Community 2026` as the minimum supported Windows GUI development environment.

## Design

### 1. Baseline and Version Policy

- `global.json` will move from `.NET 8` to the installed `.NET 10` SDK line.
- Launcher-related projects will target:
  - `net10.0-windows` for WPF GUI
  - `net10.0` for launcher core and tests
- The Windows GUI toolchain baseline becomes:
  - `.NET 10 SDK`
  - Visual Studio Community 2026
  - Visual Studio C++ desktop build tools for `hostd`

This is a hard baseline change, not a multi-target or compatibility layer.

### 2. Project Changes

The following launcher-side files will be updated:

- `global.json`
- `apps/windows-launcher/NetworkSpeaker.Launcher/*.csproj`
- `apps/windows-launcher/NetworkSpeaker.Launcher.Core/*.csproj`
- `apps/windows-launcher/NetworkSpeaker.Launcher.Core.Tests/*.csproj`

Expected changes:
- replace `net8.0-windows` with `net10.0-windows`
- replace `net8.0` with `net10.0`
- keep existing package references unless a restore/build failure proves version bumps are required

This keeps migration scope narrow and avoids unnecessary dependency churn.

### 3. Packaging Pipeline Changes

`tools/package-windows-installer.ps1` will be updated so that:

- SDK validation checks for `.NET 10` instead of `.NET 8`
- error messages and usage guidance refer to `.NET 10 SDK`
- the rest of the packaging flow remains unchanged:
  - configure/build `hostd.exe`
  - `dotnet publish` launcher
  - build WiX installer project
  - emit MSI and SHA256

The packaging script will continue to accept an explicit `-DotnetPath`, but the expected SDK behind that executable becomes `.NET 10`.

### 4. Release Workflow Changes

`.github/workflows/release.yml` will be updated to align with the new baseline:

- install/use `.NET 10`
- run launcher tests on `.NET 10`
- keep existing release triggers and artifact structure unchanged

The release workflow should describe the Windows GUI pipeline as a `.NET 10` workflow end to end, not as a `.NET 8` workflow with local overrides.

### 5. Documentation Changes

The following docs will be aligned with the new baseline:

- `README.md`
- `docs/CONTRIBUTE.md`
- `docs/DESIGN.md`
- `plan-1.md`
- `progress-1.md` where needed

Documentation should explicitly state:

- `.NET 10 SDK` is required for launcher build/test/package work
- Visual Studio Community 2026 is the minimum GUI development baseline
- Visual Studio Installer is preferred for Visual Studio-managed components
- NuGet restore remains separate from Visual Studio Installer component management

### 6. Error Handling and Failure Modes

The migration should keep failures precise and actionable.

Requirements:

- local packaging must fail early with a clear message if `.NET 10 SDK` is missing
- CI failure output should clearly show whether the problem is:
  - SDK resolution
  - NuGet restore
  - WPF build
  - WiX build
- the migration should not silently roll forward to an unsupported target framework

If package versions or WiX SDK behavior break on `.NET 10`, those issues should be fixed as part of the migration rather than hidden by broad SDK fallback rules.

### 7. Testing and Verification

Verification for this migration is:

1. `dotnet test` for launcher tests succeeds under `.NET 10`
2. `dotnet publish` for the WPF launcher succeeds under `.NET 10`
3. `tools/package-windows-installer.ps1` succeeds far enough to produce:
   - published launcher output
   - MSI
   - SHA256 file
4. release workflow configuration is updated to request `.NET 10`

If WiX or package restore introduces new blockers, those become implementation issues for the migration rather than reasons to keep `.NET 8`.

## Implementation Outline

1. Update SDK and target frameworks to `.NET 10`
2. Run launcher tests and fix any compile/test issues
3. Update packaging script SDK checks/messages
4. Update GitHub Actions workflow
5. Update documentation
6. Re-run verification for test/build/package flow

## Risks

- Some NuGet package versions may have warnings or restore issues under `.NET 10`
- WiX SDK integration may expose build-time incompatibilities only during real package builds
- Existing docs may still mention `.NET 8` in multiple places, causing inconsistent setup guidance

These are acceptable migration risks and should be resolved in the same change set.

## Acceptance Criteria

- The repository no longer requires `.NET 8 SDK` for Windows launcher work
- Launcher projects target `.NET 10`
- Packaging script checks for `.NET 10`
- Release workflow uses `.NET 10`
- Docs consistently describe `.NET 10 + Visual Studio Community 2026` as the minimum Windows GUI baseline
- Local Windows verification can proceed without installing `.NET 8`
