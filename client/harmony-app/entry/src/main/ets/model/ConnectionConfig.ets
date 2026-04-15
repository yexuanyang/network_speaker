export const DEFAULT_PORT: number = 50000;

const MIN_PORT: number = 1;
const MAX_PORT: number = 65535;

export enum ValidationError {
  INVALID_PORT,
  INVALID_SENDER_IPV4,
}

export interface ValidResult {
  valid: true;
  config: ConnectionConfig;
}

export interface InvalidResult {
  valid: false;
  error: ValidationError;
}

export type ValidationResult = ValidResult | InvalidResult;

export interface StatusStrings {
  listeningAny(port: number): string;
  listeningFiltered(port: number, senderHost: string): string;
}

export class ConnectionConfig {
  senderHost: string;
  listenPort: number;

  constructor(senderHost: string, listenPort: number) {
    this.senderHost = senderHost;
    this.listenPort = listenPort;
  }

  normalized(): ConnectionConfig {
    return new ConnectionConfig(this.senderHost.trim(), this.listenPort);
  }

  validate(): ValidationResult {
    const normalizedHost = this.senderHost.trim();
    if (this.listenPort < MIN_PORT || this.listenPort > MAX_PORT) {
      return { valid: false, error: ValidationError.INVALID_PORT } as InvalidResult;
    }
    if (normalizedHost.length > 0 && !ConnectionConfig.isValidIpv4(normalizedHost)) {
      return { valid: false, error: ValidationError.INVALID_SENDER_IPV4 } as InvalidResult;
    }
    return {
      valid: true,
      config: new ConnectionConfig(normalizedHost, this.listenPort)
    } as ValidResult;
  }

  describeStatus(strings: StatusStrings): string {
    const normalizedHost = this.senderHost.trim();
    if (normalizedHost.length === 0) {
      return strings.listeningAny(this.listenPort);
    }
    return strings.listeningFiltered(this.listenPort, normalizedHost);
  }

  static parsePort(rawValue: string): number | undefined {
    const trimmed = rawValue.trim();
    if (trimmed.length === 0) {
      return undefined;
    }
    const value = parseInt(trimmed, 10);
    if (isNaN(value)) {
      return undefined;
    }
    if (value < MIN_PORT || value > MAX_PORT) {
      return undefined;
    }
    return value;
  }

  static isValidIpv4(value: string): boolean {
    const parts = value.split('.');
    if (parts.length !== 4) {
      return false;
    }
    for (const part of parts) {
      if (part.length === 0) {
        return false;
      }
      const num = parseInt(part, 10);
      if (isNaN(num) || num < 0 || num > 255) {
        return false;
      }
      if (num.toString() !== part) {
        return false;
      }
    }
    return true;
  }
}
