import { preferences } from '@kit.ArkData';
import { ConnectionConfig, DEFAULT_PORT } from './ConnectionConfig';
import { common } from '@kit.AbilityKit';

const PREFERENCES_NAME: string = 'connection_settings';
const KEY_SENDER_HOST: string = 'sender_host';
const KEY_LISTEN_PORT: string = 'listen_port';

export class SettingsManager {
  static async loadConfig(context: common.UIAbilityContext): Promise<ConnectionConfig> {
    const store = preferences.getPreferencesSync(context, { name: PREFERENCES_NAME });
    const host = store.getSync(KEY_SENDER_HOST, '') as string;
    const port = store.getSync(KEY_LISTEN_PORT, DEFAULT_PORT) as number;
    return new ConnectionConfig(host, port);
  }

  static async saveConfig(context: common.UIAbilityContext, config: ConnectionConfig): Promise<void> {
    const store = preferences.getPreferencesSync(context, { name: PREFERENCES_NAME });
    store.putSync(KEY_SENDER_HOST, config.senderHost);
    store.putSync(KEY_LISTEN_PORT, config.listenPort);
    await store.flush();
  }
}
