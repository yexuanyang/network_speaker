# Network Speaker {{VERSION}}

## Download

| Platform | File |
|----------|------|
| Windows x64 | `{{PACKAGE_BASENAME}}.msi` |
| Android arm64 | `NetworkSpeaker-{{VERSION}}-android-arm64.apk` |
| HarmonyOS arm64 | `NetworkSpeaker-{{VERSION}}-harmonyos-arm64.hap` |

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

### HarmonyOS
1. Install the HAP via DevEco Studio or `hdc install`
2. Open the app and enter the sender IP address and port

## First use

- Keep the receiver device and PC on the same LAN
- For browser audio on Windows, prefer `wasapi-role=multimedia`
- For a quick connectivity test, switch the source to `sine`

## Known limitations

- No code signing for Windows yet, so SmartScreen may show a warning
- No system tray or auto-start in this release
