namespace NetworkSpeaker.Launcher.Core;

public static class HostdLocator
{
    private const string EnvironmentVariableName = "NSPEAKER_HOSTD_PATH";

    public static string? Locate(string baseDirectory)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var candidate in EnumerateCandidates(baseDirectory))
        {
            if (!seen.Add(candidate))
            {
                continue;
            }

            if (File.Exists(candidate))
            {
                return Path.GetFullPath(candidate);
            }
        }

        return null;
    }

    private static IEnumerable<string> EnumerateCandidates(string baseDirectory)
    {
        var environmentOverride = Environment.GetEnvironmentVariable(EnvironmentVariableName);
        if (!string.IsNullOrWhiteSpace(environmentOverride))
        {
            yield return environmentOverride;
        }

        if (!string.IsNullOrWhiteSpace(baseDirectory))
        {
            yield return Path.Combine(baseDirectory, "hostd.exe");
        }

        var current = Directory.Exists(baseDirectory)
            ? new DirectoryInfo(baseDirectory)
            : null;

        while (current != null)
        {
            if (File.Exists(Path.Combine(current.FullName, "CMakeLists.txt")))
            {
                yield return Path.Combine(current.FullName, "out", "build", "windows-ninja-vcpkg", "hostd.exe");
                yield return Path.Combine(current.FullName, "build-windows-release", "hostd.exe");
                yield return Path.Combine(current.FullName, "build-windows-verify", "hostd.exe");
                yield return Path.Combine(current.FullName, "build-windows", "hostd.exe");
                yield return Path.Combine(current.FullName, "build", "hostd.exe");
            }

            current = current.Parent;
        }
    }
}
