import { hilog } from '@kit.PerformanceAnalysisKit';
import { common, wantAgent } from '@kit.AbilityKit';
import { connection } from '@kit.NetworkKit';
import nativeBridge from 'libentry.so';
import { ConnectionConfig, DEFAULT_PORT, ValidationError } from '../model/ConnectionConfig';
import { SettingsManager } from '../model/SettingsManager';
import { ValidResult, InvalidResult } from '../model/ConnectionConfig';

const LOG_DOMAIN: number = 0x0000;
const LOG_TAG: string = 'IndexPage';

@Entry
@Component
struct Index {
  @State senderHost: string = '';
  @State listenPort: string = DEFAULT_PORT.toString();
  @State routeHint: string = '';
  @State statusText: string = 'Receiver stopped.';
  @State isRunning: boolean = false;
  @State portError: string = '';
  @State hostError: string = '';
  private uiAbilityContext: common.UIAbilityContext | undefined = undefined;

  aboutToAppear(): void {
    try {
      this.uiAbilityContext = this.getUIContext().getHostContext() as common.UIAbilityContext;
    } catch (err) {
      hilog.warn(LOG_DOMAIN, LOG_TAG, 'Failed to get UIAbilityContext: %{public}s', JSON.stringify(err));
    }
    this.loadSettings();
    this.refreshRouteHint();
  }

  aboutToDisappear(): void {
    if (this.isRunning) {
      this.stopReceiver();
    }
  }

  build() {
    Scroll() {
      Column() {
        // Title
        Text('NetworkSpeaker')
          .fontSize(28)
          .fontWeight(FontWeight.Bold)
          .margin({ top: 24, bottom: 8 })

        // Description
        Text($r('app.string.connection_summary'))
          .fontSize(14)
          .fontColor('#666666')
          .margin({ bottom: 24 })
          .textAlign(TextAlign.Start)
          .width('100%')

        // Sender IPv4 Filter
        Text($r('app.string.sender_host_label'))
          .fontSize(14)
          .fontWeight(FontWeight.Medium)
          .width('100%')
          .margin({ bottom: 4 })

        TextInput({ text: this.senderHost, placeholder: 'Optional, for example 192.168.1.10' })
          .type(InputType.Normal)
          .width('100%')
          .onChange((value: string) => {
            this.senderHost = value;
            this.hostError = '';
          })
          .margin({ bottom: 4 })

        if (this.hostError.length > 0) {
          Text(this.hostError)
            .fontSize(12)
            .fontColor(Color.Red)
            .width('100%')
            .margin({ bottom: 8 })
        }

        // Listen Port
        Text($r('app.string.listen_port_label'))
          .fontSize(14)
          .fontWeight(FontWeight.Medium)
          .width('100%')
          .margin({ top: 12, bottom: 4 })

        TextInput({ text: this.listenPort, placeholder: '50000' })
          .type(InputType.Number)
          .width('100%')
          .onChange((value: string) => {
            this.listenPort = value;
            this.portError = '';
            this.refreshRouteHint();
          })
          .margin({ bottom: 4 })

        if (this.portError.length > 0) {
          Text(this.portError)
            .fontSize(12)
            .fontColor(Color.Red)
            .width('100%')
            .margin({ bottom: 8 })
        }

        // Route hint
        Text($r('app.string.route_hint_label'))
          .fontSize(14)
          .fontWeight(FontWeight.Medium)
          .width('100%')
          .margin({ top: 16, bottom: 4 })

        Text(this.routeHint)
          .fontSize(14)
          .fontColor('#333333')
          .backgroundColor('#F5F5F5')
          .borderRadius(8)
          .padding(12)
          .width('100%')
          .margin({ bottom: 16 })

        // Status
        Text($r('app.string.status_label'))
          .fontSize(14)
          .fontWeight(FontWeight.Medium)
          .width('100%')
          .margin({ bottom: 4 })

        Text(this.statusText)
          .fontSize(14)
          .fontColor('#333333')
          .width('100%')
          .margin({ bottom: 24 })

        // Start button
        Button($r('app.string.start_receiver'))
          .width('100%')
          .height(48)
          .type(ButtonType.Capsule)
          .enabled(!this.isRunning)
          .onClick(() => {
            this.startReceiver();
          })
          .margin({ bottom: 12 })

        // Stop button
        Button($r('app.string.stop_receiver'))
          .width('100%')
          .height(48)
          .type(ButtonType.Capsule)
          .backgroundColor('#FF4444')
          .enabled(this.isRunning)
          .onClick(() => {
            this.stopReceiver();
          })
      }
      .padding({ left: 16, right: 16 })
      .width('100%')
    }
    .width('100%')
    .height('100%')
  }

  private startReceiver(): void {
    const portValue = ConnectionConfig.parsePort(this.listenPort);
    if (portValue === undefined) {
      this.portError = 'Enter a UDP port between 1 and 65535.';
      return;
    }

    const config = new ConnectionConfig(this.senderHost, portValue);
    const result = config.validate();
    if (!result.valid) {
      const invalid = result as InvalidResult;
      if (invalid.error === ValidationError.INVALID_PORT) {
        this.portError = 'Enter a UDP port between 1 and 65535.';
      } else if (invalid.error === ValidationError.INVALID_SENDER_IPV4) {
        this.hostError = 'Enter a valid IPv4 address or leave it blank.';
      }
      return;
    }

    const valid = result as ValidResult;
    this.saveSettings(valid.config);

    this.statusText = `Starting receiver on UDP ${valid.config.listenPort}...`;

    const ok: boolean = nativeBridge.nativeStart(valid.config.senderHost, valid.config.listenPort);
    if (ok) {
      this.isRunning = true;
      const host = valid.config.senderHost;
      const port = valid.config.listenPort;
      if (host.length === 0) {
        this.statusText = `Listening on UDP ${port} from any sender.`;
      } else {
        this.statusText = `Listening on UDP ${port} from ${host}.`;
      }
      hilog.info(LOG_DOMAIN, LOG_TAG, 'Receiver started on port %{public}d', port);
    } else {
      this.isRunning = false;
      this.statusText = `Receiver failed to start on UDP ${valid.config.listenPort}.`;
      hilog.error(LOG_DOMAIN, LOG_TAG, 'Failed to start receiver');
    }
  }

  private stopReceiver(): void {
    nativeBridge.nativeStop();
    this.isRunning = false;
    this.statusText = 'Receiver stopped.';
    hilog.info(LOG_DOMAIN, LOG_TAG, 'Receiver stopped');
  }

  private refreshRouteHint(): void {
    const port = ConnectionConfig.parsePort(this.listenPort) ?? DEFAULT_PORT;
    try {
      const netHandle = connection.getDefaultNetSync();
      const properties = connection.getConnectionPropertiesSync(netHandle);
      const linkAddresses = properties.linkAddresses;
      for (const addr of linkAddresses) {
        const ip = addr.address.address;
        if (ip && !ip.startsWith('127.') && ip.indexOf(':') === -1) {
          this.routeHint = `Run hostd with --host ${ip} --port ${port}`;
          return;
        }
      }
      this.routeHint = `Current device IPv4 is unavailable. Use this device's LAN IPv4 with port ${port}.`;
    } catch (err) {
      this.routeHint = `Current device IPv4 is unavailable. Use this device's LAN IPv4 with port ${port}.`;
      hilog.warn(LOG_DOMAIN, LOG_TAG, 'Failed to get network info: %{public}s', JSON.stringify(err));
    }
  }

  private async loadSettings(): Promise<void> {
    if (!this.uiAbilityContext) {
      return;
    }
    try {
      const config = await SettingsManager.loadConfig(this.uiAbilityContext);
      this.senderHost = config.senderHost;
      this.listenPort = config.listenPort.toString();
    } catch (err) {
      hilog.warn(LOG_DOMAIN, LOG_TAG, 'Failed to load settings: %{public}s', JSON.stringify(err));
    }
  }

  private async saveSettings(config: ConnectionConfig): Promise<void> {
    if (!this.uiAbilityContext) {
      return;
    }
    try {
      await SettingsManager.saveConfig(this.uiAbilityContext, config);
    } catch (err) {
      hilog.warn(LOG_DOMAIN, LOG_TAG, 'Failed to save settings: %{public}s', JSON.stringify(err));
    }
  }
}
