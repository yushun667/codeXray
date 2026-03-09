/**
 * 可视化编辑区标签：加载 resources/ui/dist/graph.html（React Flow），
 * 通过 postMessage 下发 initGraph，处理 gotoSymbol / queryPredecessors / querySuccessors，graphAppend 回传
 * 参考：doc/02-可视化界面；01-解析引擎/接口约定 §3
 */

import * as path from 'path';
import * as fs from 'fs';
import * as vscode from 'vscode';
import { createLogger } from '../logger';
import { execute as gotoSymbolExecute } from '../editor/gotoSymbol';
import type { ParserService } from '../services/parserService';
import type { GraphData, QueryType } from '../types';

const log = createLogger('visualizationProvider');

const VIEW_TYPE = 'codexray.graph';
const TITLES: Record<QueryType, string> = {
  call_graph: '调用链',
  class_graph: '类关系图',
  data_flow: '数据流',
  control_flow: '控制流',
};

export class VisualizationProvider {
  private _panels: Map<string, vscode.WebviewPanel> = new Map();
  private _extensionUri: vscode.Uri;
  private _parserService: ParserService | null = null;

  constructor(context: vscode.ExtensionContext) {
    this._extensionUri = context.extensionUri;
  }

  setParserService(service: ParserService): void {
    this._parserService = service;
  }

  /**
   * 在编辑区打开新标签，加载 dist/graph.html 并下发 initGraph；支持右键扩展查询与 graphAppend
   */
  openGraph(type: QueryType, data: GraphData): void {
    log.info('openGraph', type);
    const title = `${TITLES[type] || type}`;
    const key = `${type}-${Date.now()}`;

    const panel = vscode.window.createWebviewPanel(
      VIEW_TYPE,
      `CodeXray: ${title}`,
      vscode.ViewColumn.Beside,
      { enableScripts: true, retainContextWhenHidden: true, localResourceRoots: [this._extensionUri] }
    );

    this._setGraphHtml(panel.webview);
    this._panels.set(key, panel);

    const sendInitGraph = () => {
      try {
        panel.webview.postMessage({ type: 'initGraph', graphType: type, data });
      } catch (e) {
        log.warn('postMessage initGraph 失败', e);
      }
    };

    panel.webview.onDidReceiveMessage(
      async (msg: {
        action?: string;
        uri?: string;
        line?: number;
        column?: number;
        graphType?: string;
        nodeId?: string;
        symbol?: string;
      }) => {
        if (msg.action === 'graphReady') {
          clearTimeout(fallbackTimer);
          sendInitGraph();
          return;
        }
        if (msg.action === 'gotoSymbol' && msg.uri != null) {
          const line = typeof msg.line === 'number' ? msg.line : 1;
          const column = typeof msg.column === 'number' ? msg.column : 1;
          const uri = msg.uri.startsWith('file:') ? vscode.Uri.parse(msg.uri) : vscode.Uri.file(msg.uri as string);
          gotoSymbolExecute(uri, line, column).catch((e) => log.error('gotoSymbol 失败', e));
          return;
        }
        if ((msg.action === 'queryPredecessors' || msg.action === 'querySuccessors') && msg.graphType && msg.nodeId) {
          const graphType = msg.graphType as QueryType;
          if (!this._parserService) {
            log.warn('parserService 未注入，无法执行扩展查询');
            return;
          }
          try {
            const append = await this._parserService.query(graphType, { symbol: msg.nodeId });
            const nodes = append.nodes ?? [];
            const edges = append.edges ?? [];
            if (nodes.length || edges.length) {
              panel.webview.postMessage({ type: 'graphAppend', nodes, edges });
              log.debug('graphAppend 已发送', { nodes: nodes.length, edges: edges.length });
            }
          } catch (e) {
            log.error('扩展查询失败', e instanceof Error ? e.message : String(e));
          }
        }
      }
    );

    /* 若 2s 内未收到 graphReady（如旧版 UI 或加载失败），仍尝试下发一次；收到 graphReady 后会清除 */
    let fallbackTimer: ReturnType<typeof setTimeout> = setTimeout(sendInitGraph, 2000);
    panel.onDidDispose(() => {
      clearTimeout(fallbackTimer);
      this._panels.delete(key);
    });
  }

  /** 优先加载 dist/graph.html 并替换资源为 webview URI；失败则用内联 SVG 占位 */
  private _setGraphHtml(webview: vscode.Webview): void {
    const distPath = path.join(this._extensionUri.fsPath, 'resources', 'ui', 'dist', 'graph.html');
    try {
      if (fs.existsSync(distPath)) {
        let html = fs.readFileSync(distPath, 'utf8');
        const baseUri = vscode.Uri.joinPath(this._extensionUri, 'resources', 'ui', 'dist');
        html = html.replace(/(href|src)="([^"]+)"/g, (_: string, attr: string, value: string) => {
          const rel = value.startsWith('./') ? value.slice(1) : value;
          const parts = rel.split('/').filter(Boolean);
          const full = parts.length ? vscode.Uri.joinPath(baseUri, ...parts) : baseUri;
          const uri = webview.asWebviewUri(full);
          return `${attr}="${uri}"`;
        });
        const csp = `default-src 'none'; style-src ${webview.cspSource} 'unsafe-inline'; script-src ${webview.cspSource}; font-src ${webview.cspSource};`;
        if (!html.includes('Content-Security-Policy')) {
          html = html.replace('<head>', `<head>\n  <meta http-equiv="Content-Security-Policy" content="${csp}">`);
        }
        webview.html = html;
        log.info('图面板已加载 dist/graph.html');
        return;
      }
    } catch (e) {
      log.warn('加载 dist/graph.html 失败，使用内联 HTML', e instanceof Error ? e.message : String(e));
    }
    webview.html = this._buildInlineHtml();
  }

  private _buildInlineHtml(): string {
    return `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <style>
    body { font-family: var(--vscode-font-family); padding: 12px; margin: 0; background: var(--vscode-editor-background); color: var(--vscode-editor-foreground); }
    h2 { margin: 0 0 12px 0; font-size: 14px; }
    #graph { min-height: 300px; }
  </style>
</head>
<body>
  <h2>图（请先执行 npm run build:ui 构建 resources/ui）</h2>
  <div id="graph"></div>
</body>
</html>`;
  }

  dispose(): void {
    log.debug('VisualizationProvider dispose');
    this._panels.forEach((p) => p.dispose());
    this._panels.clear();
  }
}
