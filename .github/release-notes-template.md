# Network Speaker {{VERSION}}

Stream your desktop audio over LAN to Android devices acting as wireless speakers. Supports Windows (WASAPI), Linux (PulseAudio), and Android receivers.

## Download

| Platform | File |
|----------|------|
| Windows x64 (Desktop App) | `NetworkSpeaker-Desktop-{{VERSION}}-win-x64.zip` |
| Windows x64 (MSI Installer) | `NetworkSpeaker-{{VERSION}}-win-x64.msi` |
| Linux x64 (Debian/Ubuntu) | `Network Speaker_{{VERSION}}_amd64.deb` |
| Linux x64 (Fedora/RHEL) | `Network Speaker-{{VERSION}}.x86_64.rpm` |
| Linux x64 (AppImage) | `Network Speaker_{{VERSION}}_amd64.AppImage` |
| Android arm64 | `NetworkSpeaker-{{VERSION}}-android-arm64.apk` |

Optionally verify Windows MSI checksum: `NetworkSpeaker-{{VERSION}}-win-x64.msi.sha256.txt`

## Install

### Windows
1. Run the MSI installer
2. Launch `Network Speaker` from the Start Menu
3. Enter the receiver device IP address and UDP port
4. Click `Start`

### Linux (Debian/Ubuntu)
1. Install the deb: `sudo dpkg -i Network\ Speaker_{{VERSION}}_amd64.deb`
2. Launch from the applications menu or run `network-speaker-desktop`
3. Enter the receiver device IP address and UDP port, click `Start`

### Linux (Fedora/RHEL)
1. Install the RPM: `sudo rpm -i "Network Speaker-{{VERSION}}.x86_64.rpm"`
2. Launch from the applications menu or run `network-speaker-desktop`
3. Enter the receiver device IP address and UDP port, click `Start`

### Linux (AppImage)
1. Make executable: `chmod +x "Network Speaker_{{VERSION}}_amd64.AppImage"`
2. Run: `./Network\ Speaker_{{VERSION}}_amd64.AppImage`
3. Enter the receiver device IP address and UDP port, click `Start`

### Android
1. Install the APK on an Android device (API 29+)
2. Open the app and tap "Start Receiving" to begin listening
3. Send audio from the desktop app targeting the device's IP and port

## First use

- Keep the receiver device and PC on the same LAN
- For browser audio on Windows, prefer `wasapi-role=multimedia`
- For a quick connectivity test, switch the source to `sine`

## Known limitations

- No code signing for Windows yet, so SmartScreen may show a warning
- Android APK is signed with a debug key; a release keystore will be used in a future version

## Changelog

See [CHANGELOG.md](https://github.com/yexuanyang/network_speaker/blob/main/CHANGELOG.md) for detailed version differences.
