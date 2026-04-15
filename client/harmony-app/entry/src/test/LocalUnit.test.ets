import { describe, it, expect } from '@ohos/hypium';
import {
  ConnectionConfig,
  DEFAULT_PORT,
  ValidationError,
  StatusStrings,
  ValidResult,
  InvalidResult
} from '../main/ets/model/ConnectionConfig';

export default function connectionConfigTest() {
  describe('ConnectionConfig.parsePort', () => {
    it('accepts valid range', 0, () => {
      expect(ConnectionConfig.parsePort('50000')).assertEqual(50000);
      expect(ConnectionConfig.parsePort('1')).assertEqual(1);
      expect(ConnectionConfig.parsePort('65535')).assertEqual(65535);
    });

    it('rejects empty string', 0, () => {
      expect(ConnectionConfig.parsePort('')).assertUndefined();
    });

    it('rejects zero', 0, () => {
      expect(ConnectionConfig.parsePort('0')).assertUndefined();
    });

    it('rejects value above 65535', 0, () => {
      expect(ConnectionConfig.parsePort('65536')).assertUndefined();
    });

    it('rejects non-numeric', 0, () => {
      expect(ConnectionConfig.parsePort('abc')).assertUndefined();
    });
  });

  describe('ConnectionConfig.isValidIpv4', () => {
    it('accepts valid dotted quad', 0, () => {
      expect(ConnectionConfig.isValidIpv4('192.168.1.10')).assertTrue();
      expect(ConnectionConfig.isValidIpv4('0.0.0.0')).assertTrue();
      expect(ConnectionConfig.isValidIpv4('255.255.255.255')).assertTrue();
    });

    it('rejects octet above 255', 0, () => {
      expect(ConnectionConfig.isValidIpv4('256.1.1.1')).assertFalse();
    });

    it('rejects incomplete address', 0, () => {
      expect(ConnectionConfig.isValidIpv4('192.168.1')).assertFalse();
    });

    it('rejects hostname', 0, () => {
      expect(ConnectionConfig.isValidIpv4('host.local')).assertFalse();
    });

    it('rejects empty string', 0, () => {
      expect(ConnectionConfig.isValidIpv4('')).assertFalse();
    });
  });

  describe('ConnectionConfig.validate', () => {
    it('trims sender host', 0, () => {
      const result = new ConnectionConfig(' 192.168.1.10 ', 50000).validate();
      expect(result.valid).assertTrue();
      const valid = result as ValidResult;
      expect(valid.config.senderHost).assertEqual('192.168.1.10');
      expect(valid.config.listenPort).assertEqual(50000);
    });

    it('accepts empty sender host', 0, () => {
      const result = new ConnectionConfig('', 50000).validate();
      expect(result.valid).assertTrue();
    });

    it('rejects invalid port below range', 0, () => {
      const result = new ConnectionConfig('', 0).validate();
      expect(result.valid).assertFalse();
      const invalid = result as InvalidResult;
      expect(invalid.error).assertEqual(ValidationError.INVALID_PORT);
    });

    it('rejects invalid port above range', 0, () => {
      const result = new ConnectionConfig('', 65536).validate();
      expect(result.valid).assertFalse();
      const invalid = result as InvalidResult;
      expect(invalid.error).assertEqual(ValidationError.INVALID_PORT);
    });

    it('rejects bad sender host', 0, () => {
      const result = new ConnectionConfig('300.1.1.1', 50000).validate();
      expect(result.valid).assertFalse();
      const invalid = result as InvalidResult;
      expect(invalid.error).assertEqual(ValidationError.INVALID_SENDER_IPV4);
    });
  });

  describe('ConnectionConfig.normalized', () => {
    it('trims whitespace', 0, () => {
      const config = new ConnectionConfig('  10.0.0.1  ', 8080).normalized();
      expect(config.senderHost).assertEqual('10.0.0.1');
      expect(config.listenPort).assertEqual(8080);
    });
  });

  describe('ConnectionConfig.describeStatus', () => {
    const strings: StatusStrings = {
      listeningAny(port: number): string {
        return `any:${port}`;
      },
      listeningFiltered(port: number, senderHost: string): string {
        return `one:${port}:${senderHost}`;
      },
    };

    it('uses any text for empty host', 0, () => {
      const status = new ConnectionConfig('', 50000).describeStatus(strings);
      expect(status).assertEqual('any:50000');
    });

    it('uses filtered text for specific host', 0, () => {
      const status = new ConnectionConfig('192.168.1.10', 50000).describeStatus(strings);
      expect(status).assertEqual('one:50000:192.168.1.10');
    });
  });

  describe('ConnectionConfig.DEFAULT_PORT', () => {
    it('is 50000', 0, () => {
      expect(DEFAULT_PORT).assertEqual(50000);
    });
  });
}
