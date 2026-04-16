# Network Speaker {{VERSION}}

Stream your desktop audio over LAN to Android devices acting as wireless speakers. Supports Windows (WASAPI), Linux (PulseAudio), and Android receivers.

## Download

| Platform | File |
|----------|------|
| Windows x64 | `{{PACKAGE_BASENAME}}.msi` |
| Android arm64 | `NetworkSpeaker-{{VERSION}}-android-arm64.apk` |
| Linux x64 (Ubuntu 22.04) | `NetworkSpeaker-{{VERSION}}-linux-amd64-ubuntu2204.tar.gz` |
| Linux x64 (Ubuntu 24.04) | `NetworkSpeaker-{{VERSION}}-linux-amd64-ubuntu2404.tar.gz` |
| Linux x64 (RPM) | `NetworkSpeaker-{{VERSION}}-linux-x86_64.rpm` |

Optionally verify Windows checksum: `{{PACKAGE_BASENAME}}.sha256.txt`

## Install

### Windows
1. Run the MSI
2. Launch `Network Speaker` from the Start Menu
3. Enter the receiver device IP address and UDP port
4. Click `Start`

### Android
1. Install the APK on an Android device (API 29+)
2. Open the app and enter the sender IP address and port

### Linux (Ubuntu / Debian)
1. Download and extract the tarball matching your distribution version
2. Run `./hostd --host <RECEIVER_IP> --port <PORT> --source pulse`

### Linux (Fedora / RHEL)
1. Install the RPM: `sudo rpm -i NetworkSpeaker-{{VERSION}}-linux-x86_64.rpm`
2. Run `hostd --host <RECEIVER_IP> --port <PORT> --source pulse`

## First use

- Keep the receiver device and PC on the same LAN
- For browser audio on Windows, prefer `wasapi-role=multimedia`
- For a quick connectivity test, switch the source to `sine`

## Known limitations

- No code signing for Windows yet, so SmartScreen may show a warning
- Android APK is signed with a debug key; a release keystore will be used in a future version

## Changelog

See [CHANGELOG.md](https://github.com/yexuanyang/network_speaker/blob/main/CHANGELOG.md) for detailed version differences.
