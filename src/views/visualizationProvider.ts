/**
 * 可视化编辑区标签：按 type+data 打开 Webview 渲染图，节点点击触发 gotoSymbol
 * 参考：主仓库详细功能与架构设计 §4.10；01-解析引擎/接口约定 §3
 */

import * as vscode from 'vscode';
import { createLogger } from '../logger';
import { execute as gotoSymbolExecute } from '../editor/gotoSymbol';
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

  /**
   * 在编辑区打开新标签，渲染图；节点点击时 postMessage → gotoSymbol
   */
  openGraph(type: QueryType, data: GraphData): void {
    log.info('openGraph', type);
    const title = `${TITLES[type] || type}`;
    const key = `${type}-${Date.now()}`;

    const panel = vscode.window.createWebviewPanel(
      VIEW_TYPE,
      `CodeXray: ${title}`,
      vscode.ViewColumn.Beside,
      { enableScripts: true, retainContextWhenHidden: true }
    );

    panel.webview.html = this._buildHtml(type, data);
    panel.webview.onDidReceiveMessage((msg: { action: string; uri?: string; line?: number; column?: number }) => {
      if (msg.action === 'gotoSymbol' && msg.uri != null) {
        const line = typeof msg.line === 'number' ? msg.line : 1;
        const column = typeof msg.column === 'number' ? msg.column : 1;
        const uri = msg.uri.startsWith('file:') ? vscode.Uri.parse(msg.uri) : vscode.Uri.file(msg.uri as string);
        gotoSymbolExecute(uri, line, column).catch((e) => log.error('gotoSymbol 失败', e));
      }
    });

    this._panels.set(key, panel);
    panel.onDidDispose(() => {
      this._panels.delete(key);
    });
  }

  private _buildHtml(type: QueryType, data: GraphData): string {
    const nodes = (data.nodes ?? []) as Array<{ id: string; name: string; file?: string; line?: number; column?: number; definition?: { file: string; line: number; column: number } }>;
    const edges = (data.edges ?? []) as Array<{ caller?: string; callee?: string; [k: string]: unknown }>;
    const dataStr = JSON.stringify({ type, nodes, edges })
      .replace(/\\/g, '\\\\')
      .replace(/"/g, '\\"')
      .replace(/</g, '\\u003c')
      .replace(/>/g, '\\u003e');
    return `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <style>
    body { font-family: var(--vscode-font-family); padding: 12px; margin: 0; background: var(--vscode-editor-background); color: var(--vscode-editor-foreground); }
    h2 { margin: 0 0 12px 0; font-size: 14px; }
    #graph { min-height: 300px; }
    .node { cursor: pointer; fill: var(--vscode-button-background); stroke: var(--vscode-button-border); }
    .node:hover { fill: var(--vscode-button-hoverBackground); }
    .node text { fill: var(--vscode-editor-foreground); font-size: 12px; }
    .link { stroke: var(--vscode-foreground); opacity: 0.6; }
  </style>
</head>
<body>
  <h2>${TITLES[type] ?? type}</h2>
  <div id="graph"></div>
  <script>
    const graphData = "${dataStr}";
    const payload = JSON.parse(graphData);
    const nodes = payload.nodes || [];
    const edges = payload.edges || [];
    const vscode = acquireVsCodeApi();
    const width = 600, height = 400;
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('width', width);
    svg.setAttribute('height', height);
    const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    const nodeMap = {};
    nodes.forEach((n, i) => { nodeMap[n.id] = n; });
    const positions = {};
    const cols = Math.ceil(Math.sqrt(nodes.length)) || 1;
    nodes.forEach((n, i) => {
      const x = 80 + (i % cols) * 140;
      const y = 60 + Math.floor(i / cols) * 80;
      positions[n.id] = { x, y };
    });
    edges.forEach(e => {
      const from = positions[e.caller] || positions[e.callee];
      const to = positions[e.callee] || positions[e.caller];
      if (from && to) {
        const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
        line.setAttribute('x1', from.x); line.setAttribute('y1', from.y);
        line.setAttribute('x2', to.x); line.setAttribute('y2', to.y);
        line.setAttribute('class', 'link');
        line.setAttribute('stroke-width', '1');
        g.appendChild(line);
      }
    });
    nodes.forEach((n, i) => {
      const pos = positions[n.id] || { x: 80 + (i % cols) * 140, y: 60 + Math.floor(i / cols) * 80 };
      const gnode = document.createElementNS('http://www.w3.org/2000/svg', 'g');
      gnode.setAttribute('class', 'node');
      const def = n.definition || { file: n.file || '', line: n.line || 1, column: n.column || 1 };
      gnode.onclick = () => vscode.postMessage({ action: 'gotoSymbol', uri: def.file, line: def.line, column: def.column });
      const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
      rect.setAttribute('x', pos.x - 40); rect.setAttribute('y', pos.y - 12);
      rect.setAttribute('width', 80); rect.setAttribute('height', 24);
      rect.setAttribute('rx', 4);
      const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
      text.setAttribute('x', pos.x); text.setAttribute('y', pos.y + 4);
      text.setAttribute('text-anchor', 'middle');
      text.textContent = (n.name || n.id).substring(0, 12);
      gnode.appendChild(rect); gnode.appendChild(text);
      g.appendChild(gnode);
    });
    svg.appendChild(g);
    document.getElementById('graph').appendChild(svg);
  </script>
</body>
</html>`;
  }

  dispose(): void {
    log.debug('VisualizationProvider dispose');
    this._panels.forEach((p) => p.dispose());
    this._panels.clear();
  }
}
