using System.Text;
using System.Windows;
using NetworkSpeaker.Launcher.Core;

namespace NetworkSpeaker.Launcher;

public sealed class MainViewModel : ObservableObject, IDisposable
{
    private readonly StringBuilder logBuilder = new();
    private readonly HostdProcessController processController;
    private readonly LauncherSettingsStore settingsStore;
    private readonly string baseDirectory;

    private string host = string.Empty;
    private string port = "50000";
    private CaptureSource selectedSource = CaptureSource.Wasapi;
    private WasapiRoleOption selectedWasapiRole = WasapiRoleOption.Multimedia;
    private string seconds = string.Empty;
    private string statusText = "Ready";
    private string logsText = string.Empty;
    private string hostdPath = string.Empty;
    private string commandPreview = "hostd.exe not found";
    private bool isRunning;

    public MainViewModel(HostdProcessController processController, LauncherSettingsStore settingsStore, string baseDirectory)
    {
        this.processController = processController;
        this.settingsStore = settingsStore;
        this.baseDirectory = baseDirectory;

        AvailableSources = Enum.GetValues<CaptureSource>();
        AvailableWasapiRoles = Enum.GetValues<WasapiRoleOption>();

        this.processController.StateChanged += OnStateChanged;
        this.processController.LogReceived += OnLogReceived;
    }

    public IEnumerable<CaptureSource> AvailableSources { get; }

    public IEnumerable<WasapiRoleOption> AvailableWasapiRoles { get; }

    public string Host
    {
        get => host;
        set
        {
            if (SetProperty(ref host, value))
            {
                UpdateCommandPreview();
            }
        }
    }

    public string Port
    {
        get => port;
        set
        {
            if (SetProperty(ref port, value))
            {
                UpdateCommandPreview();
            }
        }
    }

    public CaptureSource SelectedSource
    {
        get => selectedSource;
        set
        {
            if (SetProperty(ref selectedSource, value))
            {
                RaisePropertyChanged(nameof(IsWasapiRoleEnabled));
                UpdateCommandPreview();
            }
        }
    }

    public WasapiRoleOption SelectedWasapiRole
    {
        get => selectedWasapiRole;
        set
        {
            if (SetProperty(ref selectedWasapiRole, value))
            {
                UpdateCommandPreview();
            }
        }
    }

    public string Seconds
    {
        get => seconds;
        set
        {
            if (SetProperty(ref seconds, value))
            {
                UpdateCommandPreview();
            }
        }
    }

    public bool IsWasapiRoleEnabled => SelectedSource == CaptureSource.Wasapi;

    public string StatusText
    {
        get => statusText;
        private set => SetProperty(ref statusText, value);
    }

    public string LogsText
    {
        get => logsText;
        private set => SetProperty(ref logsText, value);
    }

    public string HostdPath
    {
        get => hostdPath;
        private set => SetProperty(ref hostdPath, value);
    }

    public string CommandPreview
    {
        get => commandPreview;
        private set => SetProperty(ref commandPreview, value);
    }

    public bool IsRunning
    {
        get => isRunning;
        private set
        {
            if (SetProperty(ref isRunning, value))
            {
                RaisePropertyChanged(nameof(CanStart));
                RaisePropertyChanged(nameof(CanStop));
            }
        }
    }

    public bool CanStart => !IsRunning;

    public bool CanStop => IsRunning;

    public async Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        HostdPath = HostdLocator.Locate(baseDirectory) ?? string.Empty;

        var configuration = await settingsStore.LoadAsync(cancellationToken);
        Host = configuration.Host;
        Port = configuration.Port.ToString();
        SelectedSource = configuration.Source;
        SelectedWasapiRole = configuration.WasapiRole;
        Seconds = configuration.Seconds?.ToString() ?? string.Empty;
        StatusText = string.IsNullOrWhiteSpace(HostdPath)
            ? "hostd.exe not found. Build or install the bundled launcher."
            : "Ready";
        UpdateCommandPreview();
    }

    public async Task StartAsync(CancellationToken cancellationToken = default)
    {
        HostdPath = HostdLocator.Locate(baseDirectory) ?? string.Empty;
        if (string.IsNullOrWhiteSpace(HostdPath))
        {
            StatusText = "hostd.exe not found. Build or install the bundled launcher.";
            return;
        }

        var parseResult = TryCreateConfiguration(out var configuration, out var errorMessage);
        if (!parseResult)
        {
            StatusText = errorMessage!;
            MessageBox.Show(errorMessage!, "Invalid settings", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        try
        {
            await settingsStore.SaveAsync(configuration, cancellationToken);
        }
        catch (Exception ex)
        {
            StatusText = $"Failed to save settings: {ex.Message}";
            MessageBox.Show(
                StatusText,
                "Settings error",
                MessageBoxButton.OK,
                MessageBoxImage.Warning);
            return;
        }
        var command = HostdCommandBuilder.Build(HostdPath, configuration);
        CommandPreview = command.DisplayCommandLine;

        var result = await processController.StartAsync(command, cancellationToken);
        if (!result.Started && !string.IsNullOrWhiteSpace(result.ErrorMessage))
        {
            StatusText = result.ErrorMessage;
        }
    }

    public async Task StopAsync(CancellationToken cancellationToken = default) =>
        await processController.StopAsync(cancellationToken);

    public async Task ShutdownAsync(CancellationToken cancellationToken = default)
    {
        if (TryCreateConfiguration(out var configuration, out _))
        {
            try
            {
                await settingsStore.SaveAsync(configuration, cancellationToken);
            }
            catch (Exception ex)
            {
                AppendLogLine($"Failed to save settings: {ex.Message}", isError: true);
            }
        }

        if (IsRunning)
        {
            await StopAsync(cancellationToken);
        }
    }

    public void Dispose()
    {
        processController.StateChanged -= OnStateChanged;
        processController.LogReceived -= OnLogReceived;
    }

    private bool TryCreateConfiguration(out LaunchConfiguration configuration, out string? errorMessage)
    {
        configuration = LaunchConfiguration.CreateDefault();

        if (!int.TryParse(Port, out var parsedPort))
        {
            errorMessage = "Port must be a number between 1 and 65535.";
            return false;
        }

        int? parsedSeconds = null;
        if (!string.IsNullOrWhiteSpace(Seconds))
        {
            if (!int.TryParse(Seconds, out var secondsValue))
            {
                errorMessage = "Seconds must be a positive integer when set.";
                return false;
            }
            parsedSeconds = secondsValue;
        }

        configuration = new LaunchConfiguration(
            Host.Trim(),
            parsedPort,
            SelectedSource,
            SelectedWasapiRole,
            parsedSeconds);

        var validation = LaunchConfigurationValidator.Validate(configuration);
        errorMessage = validation.ErrorMessage;
        return validation.IsValid;
    }

    private void UpdateCommandPreview()
    {
        if (!TryCreateConfiguration(out var configuration, out var errorMessage))
        {
            CommandPreview = errorMessage ?? "Configuration is invalid.";
            return;
        }

        CommandPreview = HostdCommandBuilder.BuildPreview(HostdPath, configuration);
    }

    private void OnStateChanged(object? sender, ProcessStateChangedEventArgs e)
    {
        RunOnUiThread(() =>
        {
            StatusText = e.StatusText;
            IsRunning = e.State is ProcessControllerState.Starting or ProcessControllerState.Running or ProcessControllerState.Stopping;
        });
    }

    private void OnLogReceived(object? sender, ProcessLogEventArgs e)
    {
        RunOnUiThread(() => AppendLogLine(e.Line, e.IsError));
    }

    private void AppendLogLine(string line, bool isError)
    {
        var prefix = isError ? "[stderr]" : "[stdout]";
        logBuilder.Append('[')
            .Append(DateTime.Now.ToString("HH:mm:ss"))
            .Append("] ")
            .Append(prefix)
            .Append(' ')
            .AppendLine(line);

        LogsText = logBuilder.ToString();
    }

    private static void RunOnUiThread(Action action)
    {
        var dispatcher = Application.Current?.Dispatcher;
        if (dispatcher == null || dispatcher.CheckAccess())
        {
            action();
            return;
        }

        _ = dispatcher.BeginInvoke(action);
    }
}
