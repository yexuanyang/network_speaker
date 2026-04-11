param(
    [string]$AdbPath = "",
    [string]$HostdPath = "",
    [int]$Port = 50000,
    [int]$StreamSeconds = 4,
    [int]$ToneSeconds = 3,
    [double]$Frequency = 440.0
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$packageName = "com.example.networkspeaker"
$activityName = "$packageName/.MainActivity"
$startButtonResourceId = "${packageName}:id/startButton"
$statusResourceId = "${packageName}:id/statusView"
$tempWavePath = Join-Path ([System.IO.Path]::GetTempPath()) "nspeaker-windows-emulator-e2e.wav"

function Resolve-AdbPath {
    param([string]$ExplicitPath)

    if ($ExplicitPath -and (Test-Path $ExplicitPath)) {
        return (Resolve-Path $ExplicitPath).Path
    }

    $localProperties = Join-Path $repoRoot "client\android-app\local.properties"
    if (Test-Path $localProperties) {
        $sdkLine = Get-Content $localProperties | Where-Object { $_ -like "sdk.dir=*" } | Select-Object -First 1
        if ($sdkLine) {
            $sdkDir = $sdkLine.Substring("sdk.dir=".Length)
            $sdkDir = $sdkDir -replace "\\\\", "\"
            $sdkDir = $sdkDir -replace "\\:", ":"
            $candidate = Join-Path $sdkDir "platform-tools\adb.exe"
            if (Test-Path $candidate) {
                return (Resolve-Path $candidate).Path
            }
        }
    }

    $fallback = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
    if (Test-Path $fallback) {
        return (Resolve-Path $fallback).Path
    }

    throw "adb.exe not found. Set -AdbPath or configure client/android-app/local.properties."
}

function Resolve-HostdPath {
    param([string]$ExplicitPath)

    if ($ExplicitPath -and (Test-Path $ExplicitPath)) {
        return (Resolve-Path $ExplicitPath).Path
    }

    foreach ($candidate in @(
        (Join-Path $repoRoot "build-windows-verify\hostd.exe"),
        (Join-Path $repoRoot "build-windows\hostd.exe")
    )) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "hostd.exe not found. Build the Windows target first or pass -HostdPath."
}

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)

    & $script:ResolvedAdbPath @Arguments
}

function Write-TestWaveFile {
    param(
        [string]$Path,
        [int]$SampleRate,
        [int]$Channels,
        [int]$BitsPerSample,
        [int]$Seconds,
        [double]$ToneFrequency
    )

    $bytesPerSample = $BitsPerSample / 8
    $totalSamples = $SampleRate * $Seconds
    $dataSize = $totalSamples * $Channels * $bytesPerSample
    $stream = [System.IO.File]::Create($Path)

    try {
        $writer = [System.IO.BinaryWriter]::new($stream)
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
        $writer.Write([int](36 + $dataSize))
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))
        $writer.Write([int]16)
        $writer.Write([System.Int16]1)
        $writer.Write([System.Int16]$Channels)
        $writer.Write([int]$SampleRate)
        $writer.Write([int]($SampleRate * $Channels * $bytesPerSample))
        $writer.Write([System.Int16]($Channels * $bytesPerSample))
        $writer.Write([System.Int16]$BitsPerSample)
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
        $writer.Write([int]$dataSize)

        for ($i = 0; $i -lt $totalSamples; $i++) {
            $sample = [math]::Sin(2.0 * [math]::PI * $ToneFrequency * $i / $SampleRate)
            $value = [System.Int16][math]::Round(0.35 * 32767.0 * $sample)
            for ($channel = 0; $channel -lt $Channels; $channel++) {
                $writer.Write($value)
            }
        }

        $writer.Flush()
    }
    finally {
        $stream.Dispose()
    }
}

function Get-UiXml {
    Invoke-Adb shell uiautomator dump /sdcard/nspeaker-ui.xml | Out-Null
    return (Invoke-Adb shell cat /sdcard/nspeaker-ui.xml) -join ""
}

function Get-NodeCenterFromUi {
    param(
        [string]$UiXml,
        [string]$ResourceId
    )

    $escapedResourceId = [regex]::Escape($ResourceId)
    $pattern = "resource-id=""$escapedResourceId""[^>]*bounds=""\[(\d+),(\d+)\]\[(\d+),(\d+)\]"""
    $match = [regex]::Match($UiXml, $pattern)
    if (-not $match.Success) {
        throw "Failed to locate UI node: $ResourceId"
    }

    $x1 = [int]$match.Groups[1].Value
    $y1 = [int]$match.Groups[2].Value
    $x2 = [int]$match.Groups[3].Value
    $y2 = [int]$match.Groups[4].Value

    return @{
        X = [int](($x1 + $x2) / 2)
        Y = [int](($y1 + $y2) / 2)
    }
}

function Get-StatusTextFromUi {
    param([string]$UiXml)

    $escapedResourceId = [regex]::Escape($statusResourceId)
    $pattern = "text=""([^""]*)""\s+resource-id=""$escapedResourceId"""
    $match = [regex]::Match($UiXml, $pattern)
    if (-not $match.Success) {
        throw "Failed to read receiver status text from UI."
    }

    return $match.Groups[1].Value
}

function Ensure-ReceiverReady {
    $uiBeforeTap = Get-UiXml
    $center = Get-NodeCenterFromUi -UiXml $uiBeforeTap -ResourceId $startButtonResourceId
    Invoke-Adb shell input tap $center.X $center.Y | Out-Null
    Start-Sleep -Seconds 2

    $uiAfterTap = Get-UiXml
    $statusText = Get-StatusTextFromUi -UiXml $uiAfterTap
    if ($statusText -notlike "Listening on UDP $Port*") {
        throw "Receiver did not enter listening state. Current status: $statusText"
    }

    return $statusText
}

function Ensure-EmulatorRedirection {
    $redirList = (Invoke-Adb emu redir list) -join "`n"
    if ($redirList -notmatch "udp:$Port\s+=>\s+$Port") {
        Invoke-Adb emu redir add "udp:$Port`:$Port" | Out-Null
    }
}

function Start-HostdProcess {
    param(
        [string]$ExePath,
        [int]$ListenPort,
        [int]$Seconds
    )

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $ExePath
    $psi.Arguments = "--host 127.0.0.1 --port $ListenPort --source wasapi --seconds $Seconds"
    $psi.WorkingDirectory = Split-Path $ExePath -Parent
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $psi
    [void]$process.Start()
    return $process
}

$script:ResolvedAdbPath = Resolve-AdbPath -ExplicitPath $AdbPath
$resolvedHostdPath = Resolve-HostdPath -ExplicitPath $HostdPath

$packageInstalled = (Invoke-Adb shell pm list packages) -join "`n"
if ($packageInstalled -notmatch [regex]::Escape("package:$packageName")) {
    throw "$packageName is not installed on the emulator. Install the debug APK first."
}

$hostdProcess = $null

try {
    Write-TestWaveFile -Path $tempWavePath -SampleRate 48000 -Channels 2 -BitsPerSample 16 `
        -Seconds $ToneSeconds -ToneFrequency $Frequency

    Invoke-Adb logcat -c | Out-Null
    Ensure-EmulatorRedirection
    Invoke-Adb shell am force-stop $packageName | Out-Null
    Invoke-Adb shell am start -n $activityName | Out-Null
    Start-Sleep -Seconds 2
    $statusText = Ensure-ReceiverReady

    $hostdProcess = Start-HostdProcess -ExePath $resolvedHostdPath -ListenPort $Port -Seconds $StreamSeconds

    Start-Sleep -Seconds 2
    $player = [System.Media.SoundPlayer]::new($tempWavePath)
    $player.PlaySync()

    if (-not $hostdProcess.WaitForExit(($StreamSeconds + 15) * 1000)) {
        try {
            $hostdProcess.Kill()
        }
        catch {
        }
        throw "hostd.exe did not exit within the expected timeout."
    }

    $hostdOutput = $hostdProcess.StandardOutput.ReadToEnd().Trim()
    $hostdError = $hostdProcess.StandardError.ReadToEnd().Trim()

    $logcat = (Invoke-Adb logcat -d -s NetworkSpeaker NetworkSpeakerNative AndroidRuntime) -join "`n"

    if ($hostdOutput -notmatch "Sent \d+ frames to 127\.0\.0\.1:$Port") {
        throw "hostd did not report successful UDP streaming.`n$hostdOutput`n$hostdError"
    }
    if ($logcat -notmatch "ClientSession started on UDP port $Port") {
        throw "Android client never reported ClientSession start.`n$logcat"
    }
    if ($logcat -notmatch "PCM write #1 samplesPerChannel=480") {
        throw "Android AudioTrack never reported a PCM write.`n$logcat"
    }

    Write-Host "Receiver status: $statusText"
    Write-Host $hostdOutput
    Write-Host "Validated Windows audio playback -> WASAPI loopback -> hostd UDP -> Android emulator AudioTrack."
    Write-Host "Relevant logcat lines:"
    $logcat.Split([Environment]::NewLine) |
        Where-Object { $_ -match "ClientSession started|AudioTrack started|PCM write #1|PCM write #50|PCM write #100" } |
        ForEach-Object { Write-Host $_ }
}
finally {
    if ($hostdProcess) {
        if (-not $hostdProcess.HasExited) {
            try {
                $hostdProcess.Kill()
            }
            catch {
            }
        }
        $hostdProcess.Dispose()
    }
    Invoke-Adb shell am force-stop $packageName | Out-Null
    if (Test-Path $tempWavePath) {
        Remove-Item -LiteralPath $tempWavePath -Force
    }
}
