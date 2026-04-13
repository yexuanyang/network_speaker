using System.ComponentModel;
using System.IO;
using System.Windows;
using NetworkSpeaker.Launcher.Core;

namespace NetworkSpeaker.Launcher;

public partial class MainWindow : Window
{
    private readonly MainViewModel viewModel;
    private bool isShutdownComplete;
    private bool isShuttingDown;

    public MainWindow()
    {
        InitializeComponent();

        var settingsPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "NetworkSpeaker",
            "settings.json");

        viewModel = new MainViewModel(
            new HostdProcessController(),
            new LauncherSettingsStore(settingsPath),
            AppContext.BaseDirectory);

        DataContext = viewModel;
    }

    private async void Window_OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
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

    protected override async void OnClosing(CancelEventArgs e)
    {
        if (isShutdownComplete)
        {
            viewModel.Dispose();
            base.OnClosing(e);
            return;
        }

        e.Cancel = true;
        if (isShuttingDown)
        {
            return;
        }

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
            MessageBox.Show(
                $"Failed to shut down cleanly: {ex.Message}",
                "Network Speaker",
                MessageBoxButton.OK,
                MessageBoxImage.Warning);
        }
    }
}
