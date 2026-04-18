use std::sync::Arc;

use tauri::Emitter;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;
use tokio::sync::Mutex;

use crate::models::{HostdCommand, HostdStatus, LogEntry, ProcessState};

struct ProcessInner {
    state: ProcessState,
    status_text: String,
    stop_requested: bool,
    child_kill_tx: Option<tokio::sync::oneshot::Sender<()>>,
}

pub struct HostdProcessManager {
    inner: Mutex<ProcessInner>,
    app_handle: tauri::AppHandle,
}

impl HostdProcessManager {
    pub fn new(app_handle: tauri::AppHandle) -> Self {
        Self {
            inner: Mutex::new(ProcessInner {
                state: ProcessState::Stopped,
                status_text: "Ready".into(),
                stop_requested: false,
                child_kill_tx: None,
            }),
            app_handle,
        }
    }

    pub async fn state(&self) -> ProcessState {
        self.inner.lock().await.state
    }

    pub async fn is_running(&self) -> bool {
        matches!(
            self.inner.lock().await.state,
            ProcessState::Starting | ProcessState::Running | ProcessState::Stopping
        )
    }

    pub async fn status(&self, hostd_path: Option<String>, command_preview: String) -> HostdStatus {
        let inner = self.inner.lock().await;
        HostdStatus {
            state: inner.state,
            status_text: inner.status_text.clone(),
            hostd_path,
            command_preview,
        }
    }

    pub async fn start(self: &Arc<Self>, command: HostdCommand) -> Result<(), String> {
        {
            let inner = self.inner.lock().await;
            if matches!(
                inner.state,
                ProcessState::Starting | ProcessState::Running | ProcessState::Stopping
            ) {
                return Err("hostd is already running.".into());
            }
        }

        self.transition_to(ProcessState::Starting, "Starting hostd...")
            .await;
        self.emit_log(&format!("Started: {}", command.display_command_line), false);

        let mut cmd = Command::new(&command.executable_path);
        cmd.args(&command.arguments)
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .kill_on_drop(true);

        #[cfg(windows)]
        {
            const CREATE_NO_WINDOW: u32 = 0x08000000;
            cmd.creation_flags(CREATE_NO_WINDOW);
        }

        let mut child = cmd.spawn()
            .map_err(|e| {
                let msg = format!("Failed to start hostd: {e}");
                // We'll transition to faulted in the caller
                msg
            })?;

        let stdout = child.stdout.take();
        let stderr = child.stderr.take();

        let (kill_tx, kill_rx) = tokio::sync::oneshot::channel::<()>();

        {
            let mut inner = self.inner.lock().await;
            inner.state = ProcessState::Running;
            inner.status_text = "Running".into();
            inner.stop_requested = false;
            inner.child_kill_tx = Some(kill_tx);
        }
        self.emit_state_changed().await;

        // Spawn background task for stdout
        if let Some(stdout) = stdout {
            let mgr = Arc::clone(self);
            tokio::spawn(async move {
                let reader = BufReader::new(stdout);
                let mut lines = reader.lines();
                while let Ok(Some(line)) = lines.next_line().await {
                    if !line.trim().is_empty() {
                        mgr.emit_log(&line, false);
                    }
                }
            });
        }

        // Spawn background task for stderr
        if let Some(stderr) = stderr {
            let mgr = Arc::clone(self);
            tokio::spawn(async move {
                let reader = BufReader::new(stderr);
                let mut lines = reader.lines();
                while let Ok(Some(line)) = lines.next_line().await {
                    if !line.trim().is_empty() {
                        mgr.emit_log(&line, true);
                    }
                }
            });
        }

        // Spawn background task to wait for process exit
        let mgr = Arc::clone(self);
        tokio::spawn(async move {
            tokio::select! {
                status = child.wait() => {
                    let exit_code = status.ok().and_then(|s| s.code());
                    mgr.on_process_exited(exit_code).await;
                }
                _ = kill_rx => {
                    let _ = child.kill().await;
                    let exit_code = child.wait().await.ok().and_then(|s| s.code());
                    mgr.on_process_exited(exit_code).await;
                }
            }
        });

        Ok(())
    }

    pub async fn stop(&self) -> Result<(), String> {
        let kill_tx = {
            let mut inner = self.inner.lock().await;
            if !matches!(
                inner.state,
                ProcessState::Starting | ProcessState::Running
            ) {
                if inner.state != ProcessState::Faulted {
                    inner.state = ProcessState::Stopped;
                    inner.status_text = "Stopped".into();
                }
                return Ok(());
            }
            inner.stop_requested = true;
            inner.state = ProcessState::Stopping;
            inner.status_text = "Stopping hostd...".into();
            inner.child_kill_tx.take()
        };

        self.emit_log("Stopping hostd...", false);
        self.emit_state_changed().await;

        if let Some(tx) = kill_tx {
            let _ = tx.send(());
        }

        // Wait up to 5 seconds for the process to exit
        tokio::time::sleep(std::time::Duration::from_secs(5)).await;

        let mut inner = self.inner.lock().await;
        if inner.state == ProcessState::Stopping {
            inner.state = ProcessState::Faulted;
            inner.status_text = "Timed out while stopping hostd".into();
            drop(inner);
            self.emit_log("Timed out while waiting for hostd to exit.", true);
            self.emit_state_changed().await;
        }

        Ok(())
    }

    async fn on_process_exited(&self, exit_code: Option<i32>) {
        let was_stop_requested;
        {
            let inner = self.inner.lock().await;
            was_stop_requested = inner.stop_requested;
        }

        let code_str = exit_code
            .map(|c| c.to_string())
            .unwrap_or_else(|| "unknown".into());

        let is_error = !was_stop_requested && exit_code != Some(0);
        self.emit_log(
            &format!("hostd exited with code {code_str}."),
            is_error,
        );

        if was_stop_requested || exit_code == Some(0) {
            self.transition_to(ProcessState::Stopped, "Stopped").await;
        } else {
            self.transition_to(
                ProcessState::Faulted,
                &format!("hostd exited unexpectedly ({code_str})"),
            )
            .await;
        }

        // Cleanup
        let mut inner = self.inner.lock().await;
        inner.child_kill_tx = None;
        inner.stop_requested = false;
    }

    async fn transition_to(&self, state: ProcessState, status_text: &str) {
        {
            let mut inner = self.inner.lock().await;
            inner.state = state;
            inner.status_text = status_text.into();
        }
        self.emit_state_changed().await;
    }

    fn emit_log(&self, message: &str, is_error: bool) {
        let entry = LogEntry {
            timestamp: chrono::Local::now().format("%H:%M:%S").to_string(),
            message: message.to_string(),
            is_error,
        };
        let _ = self.app_handle.emit("hostd-log", &entry);
    }

    async fn emit_state_changed(&self) {
        let inner = self.inner.lock().await;
        let status = HostdStatus {
            state: inner.state,
            status_text: inner.status_text.clone(),
            hostd_path: None,
            command_preview: String::new(),
        };
        let _ = self.app_handle.emit("hostd-state-changed", &status);
    }
}
