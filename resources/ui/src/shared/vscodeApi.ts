/**
 * 单例获取 VS Code Webview API，避免多处调用 acquireVsCodeApi() 导致 "already been acquired" 错误。
 */

declare const acquireVsCodeApi: () => {
  postMessage: (msg: unknown) => void;
  getState: () => unknown;
  setState: (state: unknown) => void;
};

let _api: ReturnType<typeof acquireVsCodeApi> | null = null;

export function getVscodeApi(): ReturnType<typeof acquireVsCodeApi> | null {
  if (_api === null && typeof acquireVsCodeApi !== 'undefined') {
    _api = acquireVsCodeApi();
  }
  return _api;
}
