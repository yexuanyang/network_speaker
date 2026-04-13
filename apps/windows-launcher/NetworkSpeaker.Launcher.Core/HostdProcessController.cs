using System.Diagnostics;

namespace NetworkSpeaker.Launcher.Core;

public interface IManagedProcess : IDisposable
{
    event EventHandler<string>? StandardOutputReceived;
    event EventHandler<string>? StandardErrorReceived;
    event EventHandler<int?>? Exited;

    bool HasExited { get; }

    int? ExitCode { get; }

    void Start(ProcessStartInfo startInfo);

    void BeginOutputReadLine();

    void BeginErrorReadLine();

    void Kill(bool entireProcessTree);
}

public interface IManagedProcessFactory
{
    IManagedProcess Create();
}

public sealed class HostdProcessController
{
    private readonly object gate = new();
    private readonly IManagedProcessFactory processFactory;
    private IManagedProcess? activeProcess;
    private TaskCompletionSource<int?>? exitCompletionSource;
    private bool stopRequested;

    public HostdProcessController(IManagedProcessFactory? processFactory = null)
    {
        this.processFactory = processFactory ?? new SystemManagedProcessFactory();
    }

    public event EventHandler<ProcessStateChangedEventArgs>? StateChanged;

    public event EventHandler<ProcessLogEventArgs>? LogReceived;

    public ProcessControllerState State { get; private set; } = ProcessControllerState.Stopped;

    public string? ActiveCommandLine { get; private set; }

    public bool IsRunning =>
        State is ProcessControllerState.Starting or ProcessControllerState.Running or ProcessControllerState.Stopping;

    public Task<HostdStartResult> StartAsync(HostdCommand command, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(command);
        cancellationToken.ThrowIfCancellationRequested();

        IManagedProcess process;

        lock (gate)
        {
            if (IsRunning)
            {
                return Task.FromResult(new HostdStartResult(false, "hostd is already running."));
            }

            process = processFactory.Create();
            activeProcess = process;
            exitCompletionSource = new TaskCompletionSource<int?>(TaskCreationOptions.RunContinuationsAsynchronously);
            stopRequested = false;
            ActiveCommandLine = command.DisplayCommandLine;
            State = ProcessControllerState.Starting;

            process.StandardOutputReceived += OnStandardOutputReceived;
            process.StandardErrorReceived += OnStandardErrorReceived;
            process.Exited += OnExited;
        }

        TransitionTo(ProcessControllerState.Starting, "Starting hostd...");

        try
        {
            var startInfo = new ProcessStartInfo(command.ExecutablePath)
            {
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                WorkingDirectory = Path.GetDirectoryName(command.ExecutablePath) ?? Environment.CurrentDirectory,
            };

            foreach (var argument in command.Arguments)
            {
                startInfo.ArgumentList.Add(argument);
            }

            process.Start(startInfo);
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();

            EmitLog($"Started: {command.DisplayCommandLine}", isError: false);
            TransitionTo(ProcessControllerState.Running, "Running");
            return Task.FromResult(new HostdStartResult(true, null));
        }
        catch (Exception ex)
        {
            CleanupActiveProcess(process, detachEvents: true);
            EmitLog($"Failed to start hostd: {ex.Message}", isError: true);
            TransitionTo(ProcessControllerState.Faulted, "Failed to start hostd");
            return Task.FromResult(new HostdStartResult(false, ex.Message));
        }
    }

    public async Task StopAsync(CancellationToken cancellationToken = default)
    {
        IManagedProcess? process;
        Task<int?>? waitForExitTask;

        lock (gate)
        {
            process = activeProcess;
            waitForExitTask = exitCompletionSource?.Task;

            if (process == null)
            {
                if (State != ProcessControllerState.Faulted)
                {
                    TransitionTo(ProcessControllerState.Stopped, "Stopped");
                }
                return;
            }

            if (process.HasExited)
            {
                return;
            }

            stopRequested = true;
            TransitionTo(ProcessControllerState.Stopping, "Stopping hostd...");
        }

        EmitLog("Stopping hostd...", isError: false);

        try
        {
            process.Kill(entireProcessTree: true);
        }
        catch (Exception ex)
        {
            EmitLog($"Failed to stop hostd cleanly: {ex.Message}", isError: true);
        }

        if (waitForExitTask == null)
        {
            return;
        }

        var timeoutTask = Task.Delay(TimeSpan.FromSeconds(5), cancellationToken);
        var completedTask = await Task.WhenAny(waitForExitTask, timeoutTask);
        cancellationToken.ThrowIfCancellationRequested();
        if (completedTask != waitForExitTask)
        {
            TransitionTo(ProcessControllerState.Faulted, "Timed out while stopping hostd");
            EmitLog("Timed out while waiting for hostd to exit.", isError: true);
        }
    }

    private void OnStandardOutputReceived(object? sender, string line)
    {
        if (!string.IsNullOrWhiteSpace(line))
        {
            EmitLog(line, isError: false);
        }
    }

    private void OnStandardErrorReceived(object? sender, string line)
    {
        if (!string.IsNullOrWhiteSpace(line))
        {
            EmitLog(line, isError: true);
        }
    }

    private void OnExited(object? sender, int? exitCode)
    {
        var wasStopRequested = false;

        lock (gate)
        {
            wasStopRequested = stopRequested;
            exitCompletionSource?.TrySetResult(exitCode);
        }

        EmitLog(
            $"hostd exited with code {exitCode?.ToString() ?? "unknown"}.",
            isError: !wasStopRequested && exitCode is not 0);

        if (wasStopRequested || exitCode == 0)
        {
            TransitionTo(ProcessControllerState.Stopped, "Stopped");
        }
        else
        {
            TransitionTo(ProcessControllerState.Faulted, $"hostd exited unexpectedly ({exitCode})");
        }

        if (sender is IManagedProcess process)
        {
            CleanupActiveProcess(process, detachEvents: true);
        }
    }

    private void TransitionTo(ProcessControllerState newState, string statusText)
    {
        State = newState;
        StateChanged?.Invoke(this, new ProcessStateChangedEventArgs(newState, statusText));
    }

    private void EmitLog(string line, bool isError)
    {
        LogReceived?.Invoke(this, new ProcessLogEventArgs(line, isError));
    }

    private void CleanupActiveProcess(IManagedProcess process, bool detachEvents)
    {
        lock (gate)
        {
            if (ReferenceEquals(activeProcess, process))
            {
                activeProcess = null;
                exitCompletionSource = null;
                stopRequested = false;
                ActiveCommandLine = null;
            }
        }

        if (detachEvents)
        {
            process.StandardOutputReceived -= OnStandardOutputReceived;
            process.StandardErrorReceived -= OnStandardErrorReceived;
            process.Exited -= OnExited;
        }

        process.Dispose();
    }
}

internal sealed class SystemManagedProcessFactory : IManagedProcessFactory
{
    public IManagedProcess Create() => new SystemManagedProcess();
}

internal sealed class SystemManagedProcess : IManagedProcess
{
    private readonly Process process = new() { EnableRaisingEvents = true };

    public SystemManagedProcess()
    {
        process.OutputDataReceived += (_, args) =>
        {
            if (!string.IsNullOrWhiteSpace(args.Data))
            {
                StandardOutputReceived?.Invoke(this, args.Data);
            }
        };
        process.ErrorDataReceived += (_, args) =>
        {
            if (!string.IsNullOrWhiteSpace(args.Data))
            {
                StandardErrorReceived?.Invoke(this, args.Data);
            }
        };
        process.Exited += (_, _) =>
        {
            int? exitCode = null;
            try
            {
                exitCode = process.ExitCode;
            }
            catch (InvalidOperationException)
            {
                exitCode = null;
            }

            Exited?.Invoke(this, exitCode);
        };
    }

    public event EventHandler<string>? StandardOutputReceived;

    public event EventHandler<string>? StandardErrorReceived;

    public event EventHandler<int?>? Exited;

    public bool HasExited => process.HasExited;

    public int? ExitCode => process.HasExited ? process.ExitCode : null;

    public void Start(ProcessStartInfo startInfo)
    {
        process.StartInfo = startInfo;
        if (!process.Start())
        {
            throw new InvalidOperationException("Process did not start.");
        }
    }

    public void BeginOutputReadLine() => process.BeginOutputReadLine();

    public void BeginErrorReadLine() => process.BeginErrorReadLine();

    public void Kill(bool entireProcessTree) => process.Kill(entireProcessTree);

    public void Dispose() => process.Dispose();
}
