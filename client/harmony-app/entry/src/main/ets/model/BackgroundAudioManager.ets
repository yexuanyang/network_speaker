import { backgroundTaskManager } from '@kit.BackgroundTasksKit';
import { avSession } from '@kit.AVSessionKit';
import { wantAgent, WantAgent, common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

const LOG_DOMAIN: number = 0x0000;
const LOG_TAG: string = 'BgAudioMgr';

class BackgroundAudioManagerImpl {
  private context: common.UIAbilityContext | undefined = undefined;
  private session: avSession.AVSession | undefined = undefined;
  private isBackgroundRunning: boolean = false;
  private onPauseCallback: (() => void) | undefined = undefined;
  private onPlayCallback: (() => void) | undefined = undefined;

  initialize(context: common.UIAbilityContext): void {
    this.context = context;
    hilog.info(LOG_DOMAIN, LOG_TAG, 'BackgroundAudioManager initialized');
  }

  setPlaybackCommandCallback(onPause: () => void, onPlay: () => void): void {
    this.onPauseCallback = onPause;
    this.onPlayCallback = onPlay;
  }

  async startBackgroundPlayback(host: string, port: number): Promise<void> {
    if (!this.context) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'Cannot start background playback: context not initialized');
      return;
    }

    if (this.isBackgroundRunning) {
      hilog.info(LOG_DOMAIN, LOG_TAG, 'Background playback already running, restarting');
      await this.stopBackgroundPlayback();
    }

    try {
      // 1. Create WantAgent for notification tap (returns to app)
      const wantAgentInfo: wantAgent.WantAgentInfo = {
        wants: [{
          bundleName: 'com.example.networkspeaker',
          abilityName: 'EntryAbility'
        }],
        actionType: wantAgent.OperationType.START_ABILITY,
        requestCode: 0,
        actionFlags: [wantAgent.WantAgentFlags.UPDATE_PRESENT_FLAG]
      };
      const agent: WantAgent = await wantAgent.getWantAgent(wantAgentInfo);

      // 2. Start background running (long-time task)
      await backgroundTaskManager.startBackgroundRunning(
        this.context,
        backgroundTaskManager.BackgroundMode.AUDIO_PLAYBACK,
        agent
      );
      this.isBackgroundRunning = true;
      hilog.info(LOG_DOMAIN, LOG_TAG, 'Background running started');

      // 3. Create and activate AVSession
      await this.createAvSession(host, port);
    } catch (err) {
      const error = err as BusinessError;
      hilog.error(LOG_DOMAIN, LOG_TAG,
        'Failed to start background playback: code=%{public}d, message=%{public}s',
        error.code, error.message);
    }
  }

  async stopBackgroundPlayback(): Promise<void> {
    // Destroy AVSession first
    await this.destroyAvSession();

    // Then stop background running
    if (this.isBackgroundRunning && this.context) {
      try {
        await backgroundTaskManager.stopBackgroundRunning(this.context);
        hilog.info(LOG_DOMAIN, LOG_TAG, 'Background running stopped');
      } catch (err) {
        const error = err as BusinessError;
        hilog.error(LOG_DOMAIN, LOG_TAG,
          'Failed to stop background running: code=%{public}d, message=%{public}s',
          error.code, error.message);
      }
      this.isBackgroundRunning = false;
    }
  }

  async cleanup(): Promise<void> {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'Cleaning up BackgroundAudioManager');
    await this.stopBackgroundPlayback();
    this.context = undefined;
  }

  private async createAvSession(host: string, port: number): Promise<void> {
    if (!this.context) {
      return;
    }

    try {
      this.session = await avSession.createAVSession(this.context, 'NetworkSpeaker', 'audio');

      // Set metadata (must be done before activate)
      const subtitle = host.length > 0 ? `UDP :${port} from ${host}` : `UDP :${port} from any`;
      await this.session.setAVMetadata({
        assetId: 'network_speaker_receiver',
        title: 'NetworkSpeaker',
        artist: subtitle
      });

      // Register media control commands
      this.session.on('pause', () => {
        hilog.info(LOG_DOMAIN, LOG_TAG, 'AVSession pause command received');
        this.onPauseCallback?.();
      });
      this.session.on('play', () => {
        hilog.info(LOG_DOMAIN, LOG_TAG, 'AVSession play command received');
        this.onPlayCallback?.();
      });

      // Set playback state to playing
      await this.session.setAVPlaybackState({
        state: avSession.PlaybackState.PLAYBACK_STATE_PLAY
      });

      // Activate session (must be last)
      await this.session.activate();
      hilog.info(LOG_DOMAIN, LOG_TAG, 'AVSession created and activated');
    } catch (err) {
      const error = err as BusinessError;
      hilog.error(LOG_DOMAIN, LOG_TAG,
        'Failed to create AVSession: code=%{public}d, message=%{public}s',
        error.code, error.message);
      this.session = undefined;
    }
  }

  private async destroyAvSession(): Promise<void> {
    if (!this.session) {
      return;
    }

    try {
      await this.session.setAVPlaybackState({
        state: avSession.PlaybackState.PLAYBACK_STATE_STOP
      });
      await this.session.deactivate();
      await this.session.destroy();
      hilog.info(LOG_DOMAIN, LOG_TAG, 'AVSession destroyed');
    } catch (err) {
      const error = err as BusinessError;
      hilog.error(LOG_DOMAIN, LOG_TAG,
        'Failed to destroy AVSession: code=%{public}d, message=%{public}s',
        error.code, error.message);
    }
    this.session = undefined;
  }
}

export const BackgroundAudioManager = new BackgroundAudioManagerImpl();
