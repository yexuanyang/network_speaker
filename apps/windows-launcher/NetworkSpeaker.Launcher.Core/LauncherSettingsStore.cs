using System.Text.Json;
using System.Text.Json.Serialization;

namespace NetworkSpeaker.Launcher.Core;

public sealed class LauncherSettingsStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() },
    };

    public LauncherSettingsStore(string settingsPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(settingsPath);
        SettingsPath = settingsPath;
    }

    public string SettingsPath { get; }

    public async Task<LaunchConfiguration> LoadAsync(CancellationToken cancellationToken = default)
    {
        if (!File.Exists(SettingsPath))
        {
            return LaunchConfiguration.CreateDefault();
        }

        try
        {
            await using var stream = File.OpenRead(SettingsPath);
            var loaded = await JsonSerializer.DeserializeAsync<LaunchConfiguration>(stream, JsonOptions, cancellationToken);
            return loaded ?? LaunchConfiguration.CreateDefault();
        }
        catch (JsonException)
        {
            return LaunchConfiguration.CreateDefault();
        }
        catch (NotSupportedException)
        {
            return LaunchConfiguration.CreateDefault();
        }
        catch (IOException)
        {
            return LaunchConfiguration.CreateDefault();
        }
        catch (UnauthorizedAccessException)
        {
            return LaunchConfiguration.CreateDefault();
        }
    }

    public async Task SaveAsync(LaunchConfiguration configuration, CancellationToken cancellationToken = default)
    {
        var directory = Path.GetDirectoryName(SettingsPath);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        await using var stream = File.Create(SettingsPath);
        await JsonSerializer.SerializeAsync(stream, configuration, JsonOptions, cancellationToken);
    }
}
