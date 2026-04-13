# Windows Launcher `.NET 10` Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the Windows launcher, packaging, and release pipeline from `.NET 8` to `.NET 10` so local development and CI work against the new minimum Windows GUI baseline.

**Architecture:** Keep the existing launcher architecture and MSI pipeline intact, but change the SDK and target framework baseline across the launcher projects, packaging script, workflow, and docs. Treat the current `.NET 8` requirement as the failing baseline, then bring each layer forward to `.NET 10` with fresh verification after each change.

**Tech Stack:** `.NET 10 SDK`, WPF, xUnit, PowerShell, WiX Toolset SDK, GitHub Actions, CMake/MSVC for `hostd.exe`

---

### Task 1: Switch launcher projects to `.NET 10`

**Files:**
- Modify: `C:\Users\11822\Documents\Code\network_speaker\global.json`
- Modify: `C:\Users\11822\Documents\Code\network_speaker\apps\windows-launcher\NetworkSpeaker.Launcher\NetworkSpeaker.Launcher.csproj`
- Modify: `C:\Users\11822\Documents\Code\network_speaker\apps\windows-launcher\NetworkSpeaker.Launcher.Core\NetworkSpeaker.Launcher.Core.csproj`
- Modify: `C:\Users\11822\Documents\Code\network_speaker\apps\windows-launcher\NetworkSpeaker.Launcher.Core.Tests\NetworkSpeaker.Launcher.Core.Tests.csproj`

- [ ] **Step 1: Reproduce the current SDK failure**

Run:

```powershell
& 'C:\Program Files\dotnet\dotnet.exe' test .\apps\windows-launcher\NetworkSpeaker.Launcher.Core.Tests\NetworkSpeaker.Launcher.Core.Tests.csproj
```

Expected: FAIL with `A compatible .NET SDK was not found` because `global.json` still requests `.NET 8`.

- [ ] **Step 2: Update `global.json` to the installed `.NET 10` SDK line**

Set the file content to:

```json
{
  "sdk": {
    "version": "10.0.201",
    "rollForward": "latestFeature",
    "allowPrerelease": false
  }
}
```

- [ ] **Step 3: Update launcher target frameworks**

Set the WPF launcher project to:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>WinExe</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <UseWPF>true</UseWPF>
    <AssemblyName>NetworkSpeaker</AssemblyName>
    <RootNamespace>NetworkSpeaker.Launcher</RootNamespace>
  </PropertyGroup>

  <ItemGroup>
    <ProjectReference Include="..\NetworkSpeaker.Launcher.Core\NetworkSpeaker.Launcher.Core.csproj" />
  </ItemGroup>
</Project>
```

Set the launcher core project to:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <LangVersion>latest</LangVersion>
  </PropertyGroup>
</Project>
```

Set the launcher test project to:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <IsPackable>false</IsPackable>
  </PropertyGroup>

  <ItemGroup>
    <PackageReference Include="Microsoft.NET.Test.Sdk" Version="17.10.0" />
    <PackageReference Include="xunit" Version="2.6.6" />
    <PackageReference Include="xunit.runner.visualstudio" Version="2.5.8">
      <IncludeAssets>runtime; build; native; contentfiles; analyzers; buildtransitive</IncludeAssets>
      <PrivateAssets>all</PrivateAssets>
    </PackageReference>
  </ItemGroup>

  <ItemGroup>
    <ProjectReference Include="..\NetworkSpeaker.Launcher.Core\NetworkSpeaker.Launcher.Core.csproj" />
  </ItemGroup>
</Project>
```

- [ ] **Step 4: Run launcher tests under `.NET 10`**

Run:

```powershell
& 'C:\Program Files\dotnet\dotnet.exe' test .\apps\windows-launcher\NetworkSpeaker.Launcher.Core.Tests\NetworkSpeaker.Launcher.Core.Tests.csproj --configuration Release
```

Expected: the `.NET 8 SDK not found` error is gone. If restore/build/test fails, capture the concrete failure and fix only what is required for `.NET 10`.

- [ ] **Step 5: Commit the baseline switch**

Run:

```powershell
git add global.json `
  apps/windows-launcher/NetworkSpeaker.Launcher/NetworkSpeaker.Launcher.csproj `
  apps/windows-launcher/NetworkSpeaker.Launcher.Core/NetworkSpeaker.Launcher.Core.csproj `
  apps/windows-launcher/NetworkSpeaker.Launcher.Core.Tests/NetworkSpeaker.Launcher.Core.Tests.csproj
git commit -m "build(windows): move launcher projects to dotnet 10"
```

### Task 2: Update packaging to require `.NET 10`

**Files:**
- Modify: `C:\Users\11822\Documents\Code\network_speaker\tools\package-windows-installer.ps1`
- Modify: `C:\Users\11822\Documents\Code\network_speaker\installer\windows\NetworkSpeaker.Installer.wixproj` only if `.NET 10` verification exposes a WiX-specific build fix

- [ ] **Step 1: Reproduce the current packaging-script SDK gate**

Run:

```powershell
& .\tools\package-windows-installer.ps1 -DotnetPath 'C:\Program Files\dotnet\dotnet.exe'
```

Expected: if Task 1 is not yet done, FAIL with a `.NET 8 SDK was not found` message; if Task 1 is done, any remaining failure should be later in the packaging pipeline.

- [ ] **Step 2: Update the SDK validation and messaging in the packaging script**

Change the `.NET 8` checks/messages to `.NET 10`. The key function should behave like:

```powershell
function Assert-DotnetSdk {
    param([string]$DotnetExecutable)

    $sdks = & $DotnetExecutable --list-sdks
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to inspect installed .NET SDKs via $DotnetExecutable"
    }

    if (-not ($sdks | Select-String -Pattern '^10\.' -Quiet)) {
        throw ".NET 10 SDK was not found. Install .NET 10 SDK before packaging."
    }
}
```

Also update any other help text that still tells the user to install `.NET 8 SDK`.

- [ ] **Step 3: Run the packaging script again**

Run:

```powershell
& .\tools\package-windows-installer.ps1 -DotnetPath 'C:\Program Files\dotnet\dotnet.exe'
```

Expected: the script gets past SDK validation. If it fails later, capture the exact stage:
- launcher restore/build
- WPF publish
- WiX build
- MSI output assertion

- [ ] **Step 4: Apply the minimal packaging fix if `.NET 10` exposes one**

If the failure is in WiX or publish, patch only the failing file with the smallest compatible change. Example shape if the WiX SDK itself needs to remain unchanged:

```xml
<Project Sdk="WixToolset.Sdk/4.0.6">
  <PropertyGroup>
    <OutputType>Package</OutputType>
    <PublishDir Condition="'$(PublishDir)' == ''">$(MSBuildThisFileDirectory)..\..\artifacts\windows-launcher\publish</PublishDir>
    <ProductVersion Condition="'$(ProductVersion)' == ''">0.1.0</ProductVersion>
    <DisplayVersion Condition="'$(DisplayVersion)' == ''">$(ProductVersion)</DisplayVersion>
    <PackageBaseName Condition="'$(PackageBaseName)' == ''">NetworkSpeaker-$(DisplayVersion)-win-x64</PackageBaseName>
    <InstallerOutputDir Condition="'$(InstallerOutputDir)' == ''">$(MSBuildThisFileDirectory)bin\$(Configuration)\</InstallerOutputDir>
    <OutputName>$(PackageBaseName)</OutputName>
    <OutputPath>$(InstallerOutputDir)</OutputPath>
    <DefineConstants>ProductVersion=$(ProductVersion);DisplayVersion=$(DisplayVersion);GuiExe=$(PublishDir)\NetworkSpeaker.exe;HostdExe=$(PublishDir)\hostd.exe</DefineConstants>
  </PropertyGroup>
</Project>
```

Only change this file if a real failure proves it is needed.

- [ ] **Step 5: Re-run the packaging script to verify the fix**

Run:

```powershell
& .\tools\package-windows-installer.ps1 -DotnetPath 'C:\Program Files\dotnet\dotnet.exe'
```

Expected: publish output exists, MSI exists, SHA256 exists, or the remaining blocker is now clearly outside the SDK migration scope.

- [ ] **Step 6: Commit the packaging migration**

Run:

```powershell
git add tools/package-windows-installer.ps1 installer/windows/NetworkSpeaker.Installer.wixproj
git commit -m "build(windows): align packaging with dotnet 10"
```

### Task 3: Update GitHub Release automation to `.NET 10`

**Files:**
- Modify: `C:\Users\11822\Documents\Code\network_speaker\.github\workflows\release.yml`

- [ ] **Step 1: Update the workflow to describe and use `.NET 10`**

Change the setup step to:

```yaml
      - name: Set up .NET 10
        uses: actions/setup-dotnet@v4
        with:
          global-json-file: global.json
```

Keep the current version-resolution, test, packaging, artifact upload, and release-publish steps unless a `.NET 10` migration issue requires a targeted tweak.

- [ ] **Step 2: Verify the workflow YAML references `.NET 10` only**

Run:

```powershell
Select-String -Path .\.github\workflows\release.yml -Pattern 'NET 8|NET 10|dotnet'
```

Expected: `.NET 10` appears where the human-facing setup label exists, and there are no stale `.NET 8` references.

- [ ] **Step 3: Commit the workflow update**

Run:

```powershell
git add .github/workflows/release.yml
git commit -m "ci(windows): move release workflow to dotnet 10"
```

### Task 4: Align Windows launcher docs with the new baseline

**Files:**
- Modify: `C:\Users\11822\Documents\Code\network_speaker\README.md`
- Modify: `C:\Users\11822\Documents\Code\network_speaker\docs\CONTRIBUTE.md`
- Modify: `C:\Users\11822\Documents\Code\network_speaker\docs\DESIGN.md`
- Modify: `C:\Users\11822\Documents\Code\network_speaker\plan-1.md`
- Modify: `C:\Users\11822\Documents\Code\network_speaker\progress-1.md`

- [ ] **Step 1: Find stale `.NET 8` wording in docs**

Run:

```powershell
Get-ChildItem -Recurse -File . | Select-String -Pattern '\.NET 8|NET 8|dotnet 8|dotnet8'
```

Expected: a short list of docs and scripts that still mention `.NET 8`.

- [ ] **Step 2: Update README and contributor docs**

Ensure the docs state these points explicitly:

```text
- .NET 10 SDK is required for Windows launcher build/test/package work.
- Visual Studio Community 2026 is the minimum Windows GUI development baseline.
- Visual Studio Installer is preferred for Visual Studio-managed components.
- NuGet restore is separate from Visual Studio Installer component management.
```

Keep the existing user-facing GUI usage instructions intact.

- [ ] **Step 3: Update plan/progress docs for the baseline change**

Adjust `plan-1.md` and `progress-1.md` so they no longer describe the current blocker as “missing `.NET 8 SDK`”. Replace that with the new state:

```text
- The Windows launcher baseline has been moved to .NET 10.
- Visual Studio Community 2026 + .NET 10 SDK is the supported local environment.
- Remaining blockers, if any, are real build/package issues rather than the old .NET 8 dependency.
```

- [ ] **Step 4: Verify docs are internally consistent**

Run:

```powershell
Get-ChildItem -Recurse -File . | Select-String -Pattern '\.NET 8|NET 8|dotnet 8|dotnet8'
Get-ChildItem -Recurse -File . | Select-String -Pattern '\.NET 10|NET 10|dotnet 10|dotnet10'
```

Expected: Windows launcher docs no longer instruct developers to install `.NET 8`, and the new `.NET 10` baseline is visible in the expected files.

- [ ] **Step 5: Commit the docs update**

Run:

```powershell
git add README.md docs/CONTRIBUTE.md docs/DESIGN.md plan-1.md progress-1.md
git commit -m "docs(windows): document dotnet 10 launcher baseline"
```

### Task 5: Final verification of the migrated Windows launcher pipeline

**Files:**
- Verify only: `C:\Users\11822\Documents\Code\network_speaker\global.json`
- Verify only: `C:\Users\11822\Documents\Code\network_speaker\apps\windows-launcher\...`
- Verify only: `C:\Users\11822\Documents\Code\network_speaker\tools\package-windows-installer.ps1`
- Verify only: `C:\Users\11822\Documents\Code\network_speaker\.github\workflows\release.yml`
- Verify only: `C:\Users\11822\Documents\Code\network_speaker\README.md`
- Verify only: `C:\Users\11822\Documents\Code\network_speaker\docs\CONTRIBUTE.md`
- Verify only: `C:\Users\11822\Documents\Code\network_speaker\docs\DESIGN.md`
- Verify only: `C:\Users\11822\Documents\Code\network_speaker\plan-1.md`
- Verify only: `C:\Users\11822\Documents\Code\network_speaker\progress-1.md`

- [ ] **Step 1: Run launcher tests**

Run:

```powershell
& 'C:\Program Files\dotnet\dotnet.exe' test .\apps\windows-launcher\NetworkSpeaker.Launcher.Core.Tests\NetworkSpeaker.Launcher.Core.Tests.csproj --configuration Release
```

Expected: PASS.

- [ ] **Step 2: Run WPF publish**

Run:

```powershell
& 'C:\Program Files\dotnet\dotnet.exe' publish .\apps\windows-launcher\NetworkSpeaker.Launcher\NetworkSpeaker.Launcher.csproj -c Release -r win-x64 --self-contained true
```

Expected: PASS and publish output created.

- [ ] **Step 3: Run the installer packaging flow**

Run:

```powershell
& .\tools\package-windows-installer.ps1 -DotnetPath 'C:\Program Files\dotnet\dotnet.exe'
```

Expected: MSI and SHA256 are produced in `artifacts\release\`, or a remaining non-migration blocker is captured explicitly.

- [ ] **Step 4: Check final repository state**

Run:

```powershell
git status --short
```

Expected: only the intended migration files are modified.

- [ ] **Step 5: Create the final migration commit**

Run:

```powershell
git add global.json `
  apps/windows-launcher `
  installer/windows `
  tools/package-windows-installer.ps1 `
  .github/workflows/release.yml `
  README.md `
  docs/CONTRIBUTE.md `
  docs/DESIGN.md `
  plan-1.md `
  progress-1.md
git commit -m "build(windows): migrate launcher pipeline to dotnet 10"
```
