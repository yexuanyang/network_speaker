using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Documents;
using NetworkSpeaker.Launcher.Core;

namespace NetworkSpeaker.Launcher;

public partial class MainWindow : Window
{
    private readonly MainViewModel viewModel;
    private SystemTrayManager? trayManager;
    private bool isShutdownComplete;
    private bool isShuttingDown;
    private bool isExitRequested;

    public MainWindow()
    {
        InitializeComponent();

        var settingsPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "NetworkSpeaker",
            "settings.json");

        var hostdPath = HostdLocator.Locate(AppContext.BaseDirectory);
        IDeviceEnumerator? deviceEnumerator = null;
        if (!string.IsNullOrWhiteSpace(hostdPath))
        {
            deviceEnumerator = new HostdDeviceEnumerator(hostdPath);
        }

        viewModel = new MainViewModel(
            new HostdProcessController(),
            new LauncherSettingsStore(settingsPath),
            AppContext.BaseDirectory,
            deviceEnumerator);

        DataContext = viewModel;
    }

    private async void Window_OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            InitializeTrayIcon();
            await viewModel.InitializeAsync();
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                $"Failed to initialize Network Speaker: {ex.Message}",
                "Network Speaker",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            isShutdownComplete = true;
            viewModel.Dispose();
            Close();
        }
    }

    private void InitializeTrayIcon()
    {
        System.Drawing.Icon? icon = null;

        var exePath = Environment.ProcessPath;
        if (!string.IsNullOrEmpty(exePath))
            icon = System.Drawing.Icon.ExtractAssociatedIcon(exePath);

        icon ??= System.Drawing.SystemIcons.Application;

        trayManager = new SystemTrayManager(
            icon,
            showWindow: RestoreWindow,
            toggleStreaming: ToggleStreamingAsync,
            exitApp: RequestExit);

        viewModel.PropertyChanged += OnViewModelPropertyChanged;
    }

    private void RestoreWindow()
    {
        Show();
        WindowState = WindowState.Normal;
        ShowInTaskbar = true;
        Activate();
    }

    private async Task ToggleStreamingAsync()
    {
        try
        {
            if (viewModel.IsRunning)
                await viewModel.StopAsync();
            else
                await viewModel.StartAsync();
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                $"Failed to toggle streaming: {ex.Message}",
                "Network Speaker",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
        }
    }

    private void RequestExit()
    {
        isExitRequested = true;
        Dispatcher.Invoke(() => Close());
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MainViewModel.IsRunning))
        {
            trayManager?.UpdateStreamingState(viewModel.IsRunning);
        }
    }

    private void Window_OnStateChanged(object sender, EventArgs e)
    {
        if (WindowState == WindowState.Minimized)
        {
            Hide();
            ShowInTaskbar = false;
        }
    }

    private async void StartButton_OnClick(object sender, RoutedEventArgs e)
    {
        try
        {
            await viewModel.StartAsync();
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                $"Failed to start hostd: {ex.Message}",
                "Network Speaker",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
        }
    }

    private async void StopButton_OnClick(object sender, RoutedEventArgs e)
    {
        try
        {
            await viewModel.StopAsync();
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                $"Failed to stop hostd: {ex.Message}",
                "Network Speaker",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
        }
    }

    private async void RefreshDevicesButton_OnClick(object sender, RoutedEventArgs e)
    {
        try
        {
            await viewModel.RefreshDeviceListAsync();
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                $"Failed to enumerate audio devices: {ex.Message}",
                "Network Speaker",
                MessageBoxButton.OK,
                MessageBoxImage.Warning);
        }
    }

    private void VbCableLink_OnClick(object sender, RoutedEventArgs e)
    {
        try
        {
            Process.Start(new ProcessStartInfo("https://vb-audio.com/Cable/")
            {
                UseShellExecute = true,
            });
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                $"Failed to open browser: {ex.Message}",
                "Network Speaker",
                MessageBoxButton.OK,
                MessageBoxImage.Warning);
        }
    }

    protected override async void OnClosing(CancelEventArgs e)
    {
        // If shutdown already completed (second pass), finalize and exit.
        if (isShutdownComplete)
        {
            viewModel.PropertyChanged -= OnViewModelPropertyChanged;
            trayManager?.Dispose();
            viewModel.Dispose();
            base.OnClosing(e);
            System.Windows.Application.Current.Shutdown();
            return;
        }

        // If hostd is running and exit was not explicitly requested, minimize to tray.
        if (!isExitRequested && viewModel.IsRunning)
        {
            e.Cancel = true;
            WindowState = WindowState.Minimized;
            return;
        }

        // Proceed with graceful shutdown.
        e.Cancel = true;
        if (isShuttingDown)
            return;

        isShuttingDown = true;
        IsEnabled = false;

        try
        {
            await viewModel.ShutdownAsync();
            isShutdownComplete = true;
            Close();
        }
        catch (Exception ex)
        {
            IsEnabled = true;
            isShuttingDown = false;
            isExitRequested = false;
            MessageBox.Show(
                $"Failed to shut down cleanly: {ex.Message}",
                "Network Speaker",
                MessageBoxButton.OK,
                MessageBoxImage.Warning);
        }
    }
}
