namespace NetworkSpeaker.Launcher.Core;

public static class VirtualAudioDetector
{
    private static readonly string[] VirtualDeviceKeywords =
    [
        "CABLE",
        "VB-Audio",
        "Virtual",
        "Voicemeeter",
        "VoiceMeeter",
        "VBVMAUX",
        "BlackHole",
    ];

    public static bool ContainsVirtualAudioDevice(IReadOnlyList<AudioDeviceInfo> devices)
    {
        foreach (var device in devices)
        {
            if (IsVirtualDevice(device))
            {
                return true;
            }
        }
        return false;
    }

    private static bool IsVirtualDevice(AudioDeviceInfo device)
    {
        foreach (var keyword in VirtualDeviceKeywords)
        {
            if (device.Name.Contains(keyword, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }
}
