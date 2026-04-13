using System.Diagnostics;
using System.Text.Json;

namespace NetworkSpeaker.Launcher.Core;

public interface IDeviceEnumerator
{
    Task<IReadOnlyList<AudioDeviceInfo>> EnumerateDevicesAsync(CancellationToken cancellationToken = default);
}

public sealed class HostdDeviceEnumerator : IDeviceEnumerator
{
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(5);

    private readonly string hostdPath;

    public HostdDeviceEnumerator(string hostdPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(hostdPath);
        this.hostdPath = hostdPath;
    }

    public async Task<IReadOnlyList<AudioDeviceInfo>> EnumerateDevicesAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            using var process = new Process();
            process.StartInfo = new ProcessStartInfo
            {
                FileName = hostdPath,
                Arguments = "--list-devices",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };

            process.Start();

            var stdoutTask = process.StandardOutput.ReadToEndAsync(cancellationToken);

            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(Timeout);

            try
            {
                await process.WaitForExitAsync(timeoutCts.Token);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                try
                {
                    process.Kill(entireProcessTree: true);
                }
                catch (InvalidOperationException)
                {
                }
                return [];
            }

            if (process.ExitCode != 0)
            {
                return [];
            }

            var stdout = await stdoutTask;
            if (string.IsNullOrWhiteSpace(stdout))
            {
                return [];
            }

            var devices = JsonSerializer.Deserialize<List<AudioDeviceInfo>>(stdout);
            return devices ?? [];
        }
        catch (Exception ex) when (
            ex is JsonException or
            IOException or
            InvalidOperationException or
            System.ComponentModel.Win32Exception)
        {
            return [];
        }
    }
}
