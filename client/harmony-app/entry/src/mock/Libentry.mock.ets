let mockState: string = 'idle';
let mockStartResult: boolean = true;

const NativeMock: Record<string, Object> = {
  'nativeStart': (host: string, port: number): boolean => {
    if (mockStartResult) {
      mockState = 'running';
    } else {
      mockState = 'failed';
    }
    return mockStartResult;
  },
  'nativeStop': (): void => {
    mockState = 'idle';
  },
  'getReceiverState': (): string => {
    return mockState;
  },
};

export default NativeMock;
