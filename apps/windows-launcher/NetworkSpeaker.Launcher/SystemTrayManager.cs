using System.Drawing;
using System.Windows.Forms;

namespace NetworkSpeaker.Launcher;

public sealed class SystemTrayManager : IDisposable
{
    private readonly NotifyIcon notifyIcon;
    private readonly ToolStripMenuItem streamingMenuItem;
    private bool disposed;

    public SystemTrayManager(
        Icon icon,
        Action showWindow,
        Func<Task> toggleStreaming,
        Action exitApp)
    {
        streamingMenuItem = new ToolStripMenuItem("Start Streaming");
        streamingMenuItem.Click += async (_, _) => await toggleStreaming();

        var showMenuItem = new ToolStripMenuItem("Show Window");
        showMenuItem.Font = new Font(showMenuItem.Font, FontStyle.Bold);
        showMenuItem.Click += (_, _) => showWindow();

        var exitMenuItem = new ToolStripMenuItem("Exit");
        exitMenuItem.Click += (_, _) => exitApp();

        var contextMenu = new ContextMenuStrip();
        contextMenu.Items.Add(showMenuItem);
        contextMenu.Items.Add(streamingMenuItem);
        contextMenu.Items.Add(new ToolStripSeparator());
        contextMenu.Items.Add(exitMenuItem);

        notifyIcon = new NotifyIcon
        {
            Icon = icon,
            Text = "Network Speaker",
            ContextMenuStrip = contextMenu,
            Visible = true
        };

        notifyIcon.DoubleClick += (_, _) => showWindow();
    }

    public void UpdateStreamingState(bool isRunning)
    {
        if (disposed)
            return;

        streamingMenuItem.Text = isRunning ? "Stop Streaming" : "Start Streaming";
    }

    public void Dispose()
    {
        if (disposed)
            return;

        disposed = true;
        notifyIcon.Visible = false;
        notifyIcon.Dispose();
    }
}
