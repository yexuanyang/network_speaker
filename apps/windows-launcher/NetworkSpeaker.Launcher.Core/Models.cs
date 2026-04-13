using System.Net;
using System.Text.Json.Serialization;

namespace NetworkSpeaker.Launcher.Core;

public enum CaptureSource
{
    Wasapi,
    Sine,
}

public enum WasapiRoleOption
{
    Auto,
    Multimedia,
    Console,
    Communications,
}

public sealed record AudioDeviceInfo(
    [property: JsonPropertyName("id")] string Id,
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("description")] string Description,
    [property: JsonPropertyName("default")] bool IsDefault)
{
    public static AudioDeviceInfo DefaultSentinel { get; } = new(string.Empty, "(Default Device)", string.Empty, false);

    public override string ToString() => Name;
}

public sealed record LaunchConfiguration(
    string Host,
    int Port,
    CaptureSource Source,
    WasapiRoleOption WasapiRole,
    int? Seconds,
    string? DeviceId = null)
{
    public static LaunchConfiguration CreateDefault() =>
        new(string.Empty, 50000, CaptureSource.Wasapi, WasapiRoleOption.Multimedia, null, null);
}

public sealed record ValidationResult(bool IsValid, string? ErrorMessage)
{
    public static ValidationResult Success() => new(true, null);

    public static ValidationResult Failure(string message) => new(false, message);
}

public sealed record HostdCommand(
    string ExecutablePath,
    IReadOnlyList<string> Arguments,
    string DisplayCommandLine);

public enum ProcessControllerState
{
    Stopped,
    Starting,
    Running,
    Stopping,
    Faulted,
}

public sealed class ProcessStateChangedEventArgs : EventArgs
{
    public ProcessStateChangedEventArgs(ProcessControllerState state, string statusText)
    {
        State = state;
        StatusText = statusText;
    }

    public ProcessControllerState State { get; }

    public string StatusText { get; }
}

public sealed class ProcessLogEventArgs : EventArgs
{
    public ProcessLogEventArgs(string line, bool isError)
    {
        Line = line;
        IsError = isError;
    }

    public string Line { get; }

    public bool IsError { get; }
}

public readonly record struct HostdStartResult(bool Started, string? ErrorMessage);

public static class LaunchConfigurationValidator
{
    public static ValidationResult Validate(LaunchConfiguration configuration)
    {
        if (string.IsNullOrWhiteSpace(configuration.Host))
        {
            return ValidationResult.Failure("Target IP is required.");
        }

        if (!IPAddress.TryParse(configuration.Host.Trim(), out _))
        {
            return ValidationResult.Failure("Target IP must be a valid IP address.");
        }

        if (configuration.Port is < 1 or > 65535)
        {
            return ValidationResult.Failure("Port must be between 1 and 65535.");
        }

        if (configuration.Seconds is <= 0)
        {
            return ValidationResult.Failure("Seconds must be a positive integer when set.");
        }

        return ValidationResult.Success();
    }
}
