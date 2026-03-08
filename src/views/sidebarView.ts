/**
 * 侧边栏容器与双标签（解析管理 / AI 对话），postMessage 按 action 分发
 * 参考：主仓库详细功能与架构设计 §4.6；02-可视化界面/UI与交互逻辑
 */

import * as path from 'path';
import * as vscode from 'vscode';
import { createLogger } from '../logger';
import type { Config } from '../config';
import type { ParserService } from '../services/parserService';
import type { AgentService } from '../services/agentService';
import { handleMessage as parseManageHandle } from './parseManageTab';
import { handleMessage as chatTabHandle } from './chatTab';
import type { StatusBar } from '../statusBar';
import type { ParseManageTabDeps } from './parseManageTab';
import type { ChatTabDeps } from './chatTab';

const log = createLogger('sidebarView');

const PARSE_ACTIONS = new Set(['listParseHistory', 'getProject', 'setCompileCommands']);
const CHAT_ACTIONS = new Set(['sendChat', 'getContext']);

export interface SidebarViewDeps {
  config: Config;
  parserService: ParserService;
  agentService: AgentService;
  statusBar: StatusBar;
}

export class SidebarView implements vscode.WebviewViewProvider {
  private _deps: SidebarViewDeps | null = null;
  private _view: vscode.WebviewView | null = null;
  private _extensionUri: vscode.Uri;

  constructor(context: vscode.ExtensionContext) {
    this._extensionUri = context.extensionUri;
    log.debug('SidebarView 已创建');
  }

  setDeps(deps: SidebarViewDeps): void {
    this._deps = deps;
  }

  resolveWebviewView(
    webviewView: vscode.WebviewView,
    _context: vscode.WebviewViewResolveContext,
    _token: vscode.CancellationToken
  ): void {
    log.info('resolveWebviewView');
    this._view = webviewView;
    webviewView.webview.options = {
      enableScripts: true,
      localResourceRoots: [this._extensionUri],
    };

    const codiconCssUri = webviewView.webview.asWebviewUri(
      vscode.Uri.joinPath(this._extensionUri, 'node_modules', '@vscode/codicons', 'dist', 'codicon.css')
    );
    webviewView.webview.html = this._getHtml(webviewView.webview, codiconCssUri.toString());

    webviewView.webview.onDidReceiveMessage((msg: { action: string; payload?: Record<string, unknown> }) => {
      const action = msg.action ?? '';
      const payload = msg.payload ?? {};
      if (!this._deps) {
        log.warn('SidebarView 未注入 deps，忽略消息', action);
        return;
      }
      const post = (m: unknown) => {
        try {
          webviewView.webview.postMessage(m);
        } catch (e) {
          log.warn('postMessage 失败', e);
        }
      };
      if (action === 'runParse') {
        this._deps.statusBar.updateProgress(0);
        this._deps.parserService.parse().then((result) => {
          if (result.status === 'ok') {
            this._deps!.statusBar.setDone();
          } else {
            this._deps!.statusBar.setFailed(result.message);
          }
          post({ action: 'parseResult', result });
        }).catch((e) => {
          this._deps!.statusBar.setFailed(e instanceof Error ? e.message : String(e));
          post({ action: 'parseResult', result: { status: 'error', message: String(e) } });
        });
        return;
      }
      if (PARSE_ACTIONS.has(action)) {
        parseManageHandle(action, payload, this._deps as ParseManageTabDeps, post);
      } else if (CHAT_ACTIONS.has(action)) {
        chatTabHandle(action, payload, this._deps as ChatTabDeps, post);
      } else {
        log.warn('未知 action', action);
      }
    });

    this._deps?.agentService.onReply((full) => {
      try {
        webviewView.webview.postMessage({ action: 'chatReply', full });
      } catch (e) {
        log.warn('postMessage chatReply 失败', e);
      }
    });
  }

  /** 供 extension 在解析进度/完成时通知 Webview 更新 */
  postToWebview(msg: unknown): void {
    if (this._view?.webview) {
      try {
        this._view.webview.postMessage(msg);
      } catch (e) {
        log.warn('postToWebview 失败', e);
      }
    }
  }

  private _getHtml(webview: vscode.Webview, codiconCssUri: string): string {
    const nonce = Date.now().toString(36);
    return `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src ${webview.cspSource} 'unsafe-inline'; script-src 'nonce-${nonce}'; font-src ${webview.cspSource};">
  <link rel="stylesheet" href="${codiconCssUri}">
  <style>
    body { font-family: var(--vscode-font-family); margin: 0; padding: 8px; box-sizing: border-box; }
    .tabs { display: flex; gap: 4px; margin-bottom: 8px; }
    .tab { padding: 6px 10px; cursor: pointer; border: 1px solid var(--vscode-button-border); background: var(--vscode-button-secondaryBackground); color: var(--vscode-button-secondaryForeground); border-radius: 4px; font-size: 12px; }
    .tab.active { background: var(--vscode-button-background); color: var(--vscode-button.foreground); }
    .panel { display: none; }
    .panel.active { display: block; }
    .section { margin-bottom: 12px; }
    .section label { display: block; font-size: 11px; color: var(--vscode-descriptionForeground); margin-bottom: 4px; }
    input, button { font-family: inherit; }
    button { padding: 6px 10px; cursor: pointer; background: var(--vscode-button-background); color: var(--vscode-button.foreground); border: 1px solid var(--vscode-button-border); border-radius: 4px; margin-right: 6px; margin-bottom: 6px; }
    button .codicon { margin-right: 4px; }
    #historyList { max-height: 200px; overflow-y: auto; font-size: 11px; }
    #chatMessages { max-height: 300px; overflow-y: auto; padding: 8px 0; }
    .msg { margin-bottom: 8px; padding: 6px; border-radius: 4px; font-size: 12px; }
    .msg.user { background: var(--vscode-input-background); }
    .msg.assistant { background: var(--vscode-editor-inactiveSelectionBackground); }
    #chatInput { width: 100%; padding: 6px; margin-top: 8px; box-sizing: border-box; }
  </style>
</head>
<body>
  <div class="tabs">
    <button class="tab active" data-tab="parse"><span class="codicon codicon-project"></span> 解析管理</button>
    <button class="tab" data-tab="chat"><span class="codicon codicon-comment-discussion"></span> AI 对话</button>
  </div>
  <div id="parsePanel" class="panel active">
    <div class="section">
      <label>工程路径</label>
      <div id="projectRoot" style="font-size:11px; word-break:break-all;"></div>
    </div>
    <div class="section">
      <button id="btnParse"><span class="codicon codicon-play"></span> 解析</button>
      <button id="btnHistory"><span class="codicon codicon-history"></span> 历史</button>
    </div>
    <div class="section">
      <label>历史解析记录</label>
      <div id="historyList"></div>
    </div>
  </div>
  <div id="chatPanel" class="panel">
    <div id="chatMessages"></div>
    <div class="section">
      <button id="btnContext"><span class="codicon codicon-symbol-reference"></span> 引用当前符号</button>
    </div>
    <textarea id="chatInput" rows="3" placeholder="输入消息…"></textarea>
    <button id="btnSend"><span class="codicon codicon-send"></span> 发送</button>
  </div>
  <script nonce="${nonce}">
    const vscode = acquireVsCodeApi();
    document.querySelectorAll('.tab').forEach(el => {
      el.addEventListener('click', () => {
        document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
        document.querySelectorAll('.panel').forEach(p => p.classList.remove('active'));
        el.classList.add('active');
        document.getElementById(el.dataset.tab + 'Panel').classList.add('active');
      });
    });
    document.getElementById('btnParse').addEventListener('click', () => vscode.postMessage({ action: 'runParse' }));
    document.getElementById('btnHistory').addEventListener('click', () => vscode.postMessage({ action: 'listParseHistory' }));
    document.getElementById('btnSend').addEventListener('click', () => {
      const input = document.getElementById('chatInput');
      const msg = input.value.trim();
      if (!msg) return;
      vscode.postMessage({ action: 'sendChat', payload: { message: msg } });
      document.getElementById('chatMessages').insertAdjacentHTML('beforeend', '<div class="msg user">' + escapeHtml(msg) + '</div>');
      input.value = '';
    });
    document.getElementById('btnContext').addEventListener('click', () => vscode.postMessage({ action: 'getContext' }));
    window.addEventListener('message', e => {
      const m = e.data;
      if (m.action === 'projectInfo' && m.project) {
        document.getElementById('projectRoot').textContent = m.project.root || '(无工作区)';
      }
      if (m.action === 'parseHistory' && Array.isArray(m.runs)) {
        const el = document.getElementById('historyList');
        el.innerHTML = m.runs.length ? m.runs.map(r => '<div>' + (r.run_id || '') + ' ' + (r.started_at || '') + ' ' + (r.mode || '') + ' ' + (r.status || '') + '</div>').join('') : '<div>无记录</div>';
      }
      if (m.action === 'parseResult' && m.result) {
        if (m.result.status === 'ok') { document.getElementById('projectRoot').textContent = '解析完成'; }
        else { document.getElementById('projectRoot').textContent = '解析失败: ' + (m.result.message || ''); }
      }
      if (m.action === 'chatReply' && m.full) {
        document.getElementById('chatMessages').insertAdjacentHTML('beforeend', '<div class="msg assistant">' + escapeHtml(m.full) + '</div>');
      }
      if (m.action === 'context' && m.context) {
        document.getElementById('chatInput').placeholder = m.context.symbol ? '当前符号: ' + m.context.symbol : '输入消息…';
      }
    });
    function escapeHtml(s) { const d = document.createElement('div'); d.textContent = s; return d.innerHTML; }
    vscode.postMessage({ action: 'getProject' });
  </script>
</body>
</html>`;
  }
}
