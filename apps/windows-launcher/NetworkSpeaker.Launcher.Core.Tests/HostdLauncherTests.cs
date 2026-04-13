using System.Diagnostics;
using NetworkSpeaker.Launcher.Core;
using Xunit;

namespace NetworkSpeaker.Launcher.Core.Tests;

public sealed class HostdCommandBuilderTests
{
    [Fact]
    public void BuildsWasapiArgumentsInExpectedOrder()
    {
        var configuration = new LaunchConfiguration(
            "10.29.25.86",
            50000,
            CaptureSource.Wasapi,
            WasapiRoleOption.Multimedia,
            4);

        var command = HostdCommandBuilder.Build(@"C:\bundle\hostd.exe", configuration);

        Assert.Equal(
            new[]
            {
                "--host",
                "10.29.25.86",
                "--port",
                "50000",
                "--source",
                "wasapi",
                "--wasapi-role",
                "multimedia",
                "--seconds",
                "4",
            },
            command.Arguments);
        Assert.Contains("--wasapi-role multimedia", command.DisplayCommandLine);
    }

    [Fact]
    public void OmitsWasapiRoleForSine()
    {
        var configuration = new LaunchConfiguration(
            "127.0.0.1",
            50000,
            CaptureSource.Sine,
            WasapiRoleOption.Console,
            3);

        var command = HostdCommandBuilder.Build(@"C:\bundle\hostd.exe", configuration);

        Assert.DoesNotContain("--wasapi-role", command.Arguments);
        Assert.Contains("--source sine", command.DisplayCommandLine);
    }
}

public sealed class LauncherSettingsStoreTests
{
    [Fact]
    public async Task PersistsAndLoadsSettings()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        var store = new LauncherSettingsStore(Path.Combine(root, "settings.json"));
        var expected = new LaunchConfiguration(
            "192.168.1.20",
            51000,
            CaptureSource.Wasapi,
            WasapiRoleOption.Auto,
            null);

        try
        {
            await store.SaveAsync(expected);
            var loaded = await store.LoadAsync();
            Assert.Equal(expected, loaded);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public async Task ReturnsDefaultsWhenSettingsFileIsInvalidJson()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        var settingsPath = Path.Combine(root, "settings.json");
        await File.WriteAllTextAsync(settingsPath, "{ invalid json");
        var store = new LauncherSettingsStore(settingsPath);

        try
        {
            var loaded = await store.LoadAsync();
            Assert.Equal(LaunchConfiguration.CreateDefault(), loaded);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}

public sealed class HostdProcessControllerTests
{
    [Fact]
    public async Task MovesThroughRunningAndStoppedWhenUserStopsProcess()
    {
        var factory = new FakeManagedProcessFactory();
        var controller = new HostdProcessController(factory);
        var states = new List<ProcessControllerState>();
        controller.StateChanged += (_, args) => states.Add(args.State);

        var result = await controller.StartAsync(new HostdCommand(
            @"C:\bundle\hostd.exe",
            new[] { "--host", "127.0.0.1" },
            "\"C:\\bundle\\hostd.exe\" --host 127.0.0.1"));

        Assert.True(result.Started);
        Assert.NotNull(factory.LastCreated);

        await controller.StopAsync();

        Assert.Equal(
            new[]
            {
                ProcessControllerState.Starting,
                ProcessControllerState.Running,
                ProcessControllerState.Stopping,
                ProcessControllerState.Stopped,
            },
            states);
        Assert.False(controller.IsRunning);
    }

    [Fact]
    public async Task MovesToFaultedWhenProcessExitsUnexpectedly()
    {
        var factory = new FakeManagedProcessFactory();
        var controller = new HostdProcessController(factory);
        var states = new List<ProcessControllerState>();
        controller.StateChanged += (_, args) => states.Add(args.State);

        var result = await controller.StartAsync(new HostdCommand(
            @"C:\bundle\hostd.exe",
            new[] { "--host", "127.0.0.1" },
            "\"C:\\bundle\\hostd.exe\" --host 127.0.0.1"));

        Assert.True(result.Started);
        factory.LastCreated!.EmitExit(42);

        Assert.Equal(ProcessControllerState.Faulted, controller.State);
        Assert.Contains(ProcessControllerState.Faulted, states);
    }

    [Fact]
    public async Task RejectsSecondStartWhileProcessIsAlreadyRunning()
    {
        var factory = new FakeManagedProcessFactory();
        var controller = new HostdProcessController(factory);
        var command = new HostdCommand(
            @"C:\bundle\hostd.exe",
            new[] { "--host", "127.0.0.1" },
            "\"C:\\bundle\\hostd.exe\" --host 127.0.0.1");

        var first = await controller.StartAsync(command);
        var second = await controller.StartAsync(command);

        Assert.True(first.Started);
        Assert.False(second.Started);
        Assert.Equal("hostd is already running.", second.ErrorMessage);
    }

    private sealed class FakeManagedProcessFactory : IManagedProcessFactory
    {
        public FakeManagedProcess? LastCreated { get; private set; }

        public IManagedProcess Create()
        {
            LastCreated = new FakeManagedProcess();
            return LastCreated;
        }
    }

    private sealed class FakeManagedProcess : IManagedProcess
    {
        public event EventHandler<string>? StandardOutputReceived;

        public event EventHandler<string>? StandardErrorReceived;

        public event EventHandler<int?>? Exited;

        public bool HasExited { get; private set; }

        public int? ExitCode { get; private set; }

        public void Start(ProcessStartInfo startInfo)
        {
            Started = true;
            StartInfo = startInfo;
        }

        public void BeginOutputReadLine()
        {
        }

        public void BeginErrorReadLine()
        {
        }

        public void Kill(bool entireProcessTree)
        {
            Killed = true;
            EmitExit(0);
        }

        public void Dispose()
        {
        }

        public bool Started { get; private set; }

        public bool Killed { get; private set; }

        public ProcessStartInfo? StartInfo { get; private set; }

        public void EmitStdOut(string line) => StandardOutputReceived?.Invoke(this, line);

        public void EmitStdErr(string line) => StandardErrorReceived?.Invoke(this, line);

        public void EmitExit(int exitCode)
        {
            HasExited = true;
            ExitCode = exitCode;
            Exited?.Invoke(this, exitCode);
        }
    }
}

public sealed class HostdLocatorTests
{
    [Fact]
    public void ReturnsHostdFromBaseDirectoryWhenBundledExecutableExists()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        var hostdPath = Path.Combine(root, "hostd.exe");
        File.WriteAllBytes(hostdPath, Array.Empty<byte>());

        try
        {
            var located = HostdLocator.Locate(root);
            Assert.Equal(Path.GetFullPath(hostdPath), located);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}

public sealed class HostdCommandBuilderDeviceTests
{
    [Fact]
    public void IncludesDeviceArgumentWhenDeviceIdIsSet()
    {
        var configuration = new LaunchConfiguration(
            "10.29.25.86",
            50000,
            CaptureSource.Wasapi,
            WasapiRoleOption.Multimedia,
            null,
            "{0.0.0.00000000}.{some-guid}");

        var command = HostdCommandBuilder.Build(@"C:\bundle\hostd.exe", configuration);

        Assert.Contains("--device", command.Arguments);
        Assert.Contains("{0.0.0.00000000}.{some-guid}", command.Arguments);
        Assert.Contains("--device", command.DisplayCommandLine);
    }

    [Fact]
    public void OmitsDeviceArgumentWhenDeviceIdIsNull()
    {
        var configuration = new LaunchConfiguration(
            "127.0.0.1",
            50000,
            CaptureSource.Wasapi,
            WasapiRoleOption.Multimedia,
            null);

        var command = HostdCommandBuilder.Build(@"C:\bundle\hostd.exe", configuration);

        Assert.DoesNotContain("--device", command.Arguments);
    }

    [Fact]
    public void OmitsDeviceArgumentWhenSourceIsSine()
    {
        var configuration = new LaunchConfiguration(
            "127.0.0.1",
            50000,
            CaptureSource.Sine,
            WasapiRoleOption.Multimedia,
            null,
            "{0.0.0.00000000}.{some-guid}");

        var command = HostdCommandBuilder.Build(@"C:\bundle\hostd.exe", configuration);

        Assert.DoesNotContain("--device", command.Arguments);
    }
}

public sealed class VirtualAudioDetectorTests
{
    [Fact]
    public void ReturnsTrueWhenVbCablePresent()
    {
        var devices = new List<AudioDeviceInfo>
        {
            new("id1", "Speakers (Realtek Audio)", "Realtek Audio", true),
            new("id2", "CABLE Input (VB-Audio Virtual Cable)", "VB-Audio", false),
        };

        Assert.True(VirtualAudioDetector.ContainsVirtualAudioDevice(devices));
    }

    [Fact]
    public void ReturnsTrueWhenVoicemeeterPresent()
    {
        var devices = new List<AudioDeviceInfo>
        {
            new("id1", "Voicemeeter Input (VB-Audio Voicemeeter VAIO)", "VB-Audio", false),
        };

        Assert.True(VirtualAudioDetector.ContainsVirtualAudioDevice(devices));
    }

    [Fact]
    public void ReturnsFalseWhenOnlyPhysicalDevicesPresent()
    {
        var devices = new List<AudioDeviceInfo>
        {
            new("id1", "Speakers (Realtek Audio)", "Realtek Audio", true),
            new("id2", "Headphones (USB Audio)", "USB Audio", false),
        };

        Assert.False(VirtualAudioDetector.ContainsVirtualAudioDevice(devices));
    }

    [Fact]
    public void ReturnsFalseForEmptyList()
    {
        Assert.False(VirtualAudioDetector.ContainsVirtualAudioDevice([]));
    }

    [Fact]
    public void MatchesCaseInsensitively()
    {
        var devices = new List<AudioDeviceInfo>
        {
            new("id1", "Some virtual Audio Device", "Test", false),
        };

        Assert.True(VirtualAudioDetector.ContainsVirtualAudioDevice(devices));
    }
}

public sealed class LauncherSettingsStoreDeviceTests
{
    [Fact]
    public async Task PersistsAndLoadsDeviceId()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        var store = new LauncherSettingsStore(Path.Combine(root, "settings.json"));
        var expected = new LaunchConfiguration(
            "192.168.1.20",
            50000,
            CaptureSource.Wasapi,
            WasapiRoleOption.Auto,
            null,
            "{0.0.0.00000000}.{test-device-guid}");

        try
        {
            await store.SaveAsync(expected);
            var loaded = await store.LoadAsync();
            Assert.Equal(expected.DeviceId, loaded.DeviceId);
            Assert.Equal(expected, loaded);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public async Task LoadsLegacySettingsWithoutDeviceIdAsNull()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        var settingsPath = Path.Combine(root, "settings.json");
        await File.WriteAllTextAsync(settingsPath,
            """{"Host":"10.0.0.1","Port":50000,"Source":"Wasapi","WasapiRole":"Multimedia","Seconds":null}""");
        var store = new LauncherSettingsStore(settingsPath);

        try
        {
            var loaded = await store.LoadAsync();
            Assert.Null(loaded.DeviceId);
            Assert.Equal("10.0.0.1", loaded.Host);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}
