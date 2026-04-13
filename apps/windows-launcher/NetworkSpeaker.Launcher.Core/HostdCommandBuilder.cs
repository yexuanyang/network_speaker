using System.Text;

namespace NetworkSpeaker.Launcher.Core;

public static class HostdCommandBuilder
{
    public static HostdCommand Build(string executablePath, LaunchConfiguration configuration)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(executablePath);

        var validation = LaunchConfigurationValidator.Validate(configuration);
        if (!validation.IsValid)
        {
            throw new InvalidOperationException(validation.ErrorMessage);
        }

        var arguments = new List<string>
        {
            "--host",
            configuration.Host.Trim(),
            "--port",
            configuration.Port.ToString(),
            "--source",
            ToSourceArgument(configuration.Source),
        };

        if (configuration.Source == CaptureSource.Wasapi)
        {
            arguments.Add("--wasapi-role");
            arguments.Add(ToWasapiRoleArgument(configuration.WasapiRole));
        }

        if (configuration.Seconds.HasValue)
        {
            arguments.Add("--seconds");
            arguments.Add(configuration.Seconds.Value.ToString());
        }

        return new HostdCommand(executablePath, arguments, BuildDisplayCommand(executablePath, arguments));
    }

    public static string BuildPreview(string? executablePath, LaunchConfiguration configuration)
    {
        if (string.IsNullOrWhiteSpace(executablePath))
        {
            return "hostd.exe not found";
        }

        var validation = LaunchConfigurationValidator.Validate(configuration);
        if (!validation.IsValid)
        {
            return validation.ErrorMessage ?? "Configuration is invalid.";
        }

        return Build(executablePath, configuration).DisplayCommandLine;
    }

    private static string ToSourceArgument(CaptureSource source) =>
        source switch
        {
            CaptureSource.Wasapi => "wasapi",
            CaptureSource.Sine => "sine",
            _ => throw new ArgumentOutOfRangeException(nameof(source), source, null),
        };

    private static string ToWasapiRoleArgument(WasapiRoleOption role) =>
        role switch
        {
            WasapiRoleOption.Auto => "auto",
            WasapiRoleOption.Multimedia => "multimedia",
            WasapiRoleOption.Console => "console",
            WasapiRoleOption.Communications => "communications",
            _ => throw new ArgumentOutOfRangeException(nameof(role), role, null),
        };

    private static string BuildDisplayCommand(string executablePath, IReadOnlyList<string> arguments)
    {
        var builder = new StringBuilder();
        builder.Append(Quote(executablePath));
        foreach (var argument in arguments)
        {
            builder.Append(' ');
            builder.Append(Quote(argument));
        }
        return builder.ToString();
    }

    private static string Quote(string value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return "\"\"";
        }

        return value.Contains(' ') || value.Contains('"')
            ? $"\"{value.Replace("\"", "\\\"", StringComparison.Ordinal)}\""
            : value;
    }
}
