/**
 * 编辑区图标签：接收 type + data，在 WebviewPanel 中加载 graph.html，
 * 节点点击 postMessage(gotoSymbol) 时调用 gotoSymbol；处理 queryPredecessors/querySuccessors 并 graphAppend
 */

import * as path from 'path';
import * as fs from 'fs';
import * as os from 'os';
import * as vscode from 'vscode';
import { createLogger } from '../logger';
import type { QueryType, GraphData } from '../types';
import type { ParserService } from '../services/parserService';

const log = createLogger('visualizationProvider');

export interface VisualizationProviderDeps {
  extensionUri: vscode.Uri;
  parserService: ParserService;
  gotoSymbolExecute: GotoSymbolExecute;
}

export type GotoSymbolExecute = (uri: vscode.Uri, line: number, column: number) => Promise<void>;

interface PendingInit {
  type: QueryType;
  data: GraphData;
  querySymbol?: string;
  queryFile?: string;
}

export class VisualizationProvider {
  private _deps: VisualizationProviderDeps | null = null;
  private _panel: vscode.WebviewPanel | null = null;
  private _currentType: QueryType = 'call_graph';
  private _pendingInit: PendingInit | null = null;

  constructor(deps: VisualizationProviderDeps) {
    this._deps = deps;
    log.debug('VisualizationProvider 已创建');
  }

  setDeps(deps: VisualizationProviderDeps): void {
    this._deps = deps;
  }

  /**
   * 在编辑区打开图标签，传入 type 与 data；若无 panel 则创建并加载 graph.html
   * @param querySymbol 查询入口符号名（右键选中的函数名），用于标识根节点
   * @param queryFile 查询入口文件路径
   */
  openGraph(type: QueryType, data: GraphData, querySymbol?: string, queryFile?: string): void {
    if (!this._deps) {
      log.warn('VisualizationProvider 未注入 deps');
      return;
    }
    this._currentType = type;
    if (this._panel) {
      this._panel.reveal();
      this._postInitGraph(type, data, querySymbol, queryFile);
      return;
    }
    this._panel = vscode.window.createWebviewPanel(
      'codexray.graph',
      `CodeXray · ${this._titleForType(type)}`,
      vscode.ViewColumn.Beside,
      {
        enableScripts: true,
        localResourceRoots: [this._deps.extensionUri],
        retainContextWhenHidden: true,
      }
    );
    this._setGraphHtml(this._panel.webview);
    this._panel.reveal();
    this._panel.webview.onDidReceiveMessage((msg: { action: string; file?: string; uri?: string; line?: number; column?: number; graphType?: string; nodeId?: string; symbol?: string; report?: unknown; queryDepth?: number }) => {
      if (!this._deps) return;
      if (msg.action === 'graphReady') {
        const pending = this._pendingInit;
        if (pending) {
          log.info('收到 graphReady，发送 initGraph', { nodes: pending.data.nodes?.length ?? 0, edges: pending.data.edges?.length ?? 0 });
          this._postInitGraph(pending.type, pending.data, pending.querySymbol, pending.queryFile);
        }
        return;
      }
      if (msg.action === 'gotoSymbol') {
        const file = msg.file ?? msg.uri;
        const line = typeof msg.line === 'number' ? msg.line : 1;
        const column = typeof msg.column === 'number' ? msg.column : 1;
        if (file) {
          const base = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? this._deps.extensionUri.fsPath;
          const absPath = path.isAbsolute(file) ? file : path.join(base, file);
          const uri = vscode.Uri.file(absPath);
          this._deps.gotoSymbolExecute(uri, line, column).catch((e) => log.warn('gotoSymbol 失败', e));
        }
      }
      if (msg.action === 'perfReport' && msg.report) {
        this._writePerfLog(msg.report);
        return;
      }
      if (msg.action === 'queryPredecessors' || msg.action === 'querySuccessors') {
        const graphType = (msg.graphType as QueryType) ?? this._currentType;
        const symbol = msg.symbol ?? '';
        const file = msg.file ?? '';
        const queryDepth = typeof msg.queryDepth === 'number' ? msg.queryDepth : undefined;
        const direction = msg.action === 'queryPredecessors' ? 'callers' as const : 'callees' as const;
        this._deps.parserService
          .query(graphType, { symbol, file, direction, ...(queryDepth !== undefined ? { depth: queryDepth } : {}) })
          .then((appendData) => {
            const filtered = this._filterToWorkspace(appendData);
            if (this._panel?.webview && (filtered.nodes?.length || filtered.edges?.length)) {
              this._panel.webview.postMessage({
                action: 'graphAppend',
                nodes: filtered.nodes ?? [],
                edges: filtered.edges ?? [],
              });
            }
          })
          .catch((e) => log.warn('query 扩展失败', e));
      }
    });
    this._pendingInit = { type, data, querySymbol, queryFile };
    log.info('已创建图 Webview，将多次发送 initGraph 直至收到 graphReady', { nodes: data.nodes?.length ?? 0, edges: data.edges?.length ?? 0 });
    const delays = [300, 800, 1500, 2500];
    const retryTimers: NodeJS.Timeout[] = [];
    delays.forEach((ms) => {
      const t = setTimeout(() => {
        const pending = this._pendingInit;
        if (pending && this._panel?.webview) {
          log.info('发送 initGraph', { delay: ms, nodes: pending.data.nodes?.length ?? 0, edges: pending.data.edges?.length ?? 0 });
          this._postInitGraph(pending.type, pending.data, pending.querySymbol, pending.queryFile);
        }
      }, ms);
      retryTimers.push(t);
    });
    this._panel.onDidDispose(() => {
      retryTimers.forEach((t) => clearTimeout(t));
      this._panel = null;
      this._pendingInit = null;
    });
  }

  private _writePerfLog(report: unknown): void {
    try {
      const r = report as {
        trigger: string; nodeCount: number; edgeCount: number;
        phases: { computeLayout: number; resolveCollisions: number; buildG6Data: number; setData: number; render: number; fitView: number; total: number };
      };
      const date = new Date().toISOString().slice(0, 10);
      const logFile = path.join(os.tmpdir(), `codexray-perf-${date}.log`);
      const ts = new Date().toISOString();
      const line = [
        `[${ts}] trigger=${r.trigger} nodes=${r.nodeCount} edges=${r.edgeCount}`,
        `  computeLayout     ${r.phases.computeLayout} ms`,
        `  resolveCollisions ${r.phases.resolveCollisions} ms`,
        `  buildG6Data       ${r.phases.buildG6Data} ms`,
        `  setData           ${r.phases.setData} ms`,
        `  render            ${r.phases.render} ms`,
        `  fitView           ${r.phases.fitView} ms`,
        `  ── total          ${r.phases.total} ms`,
        '',
      ].join('\n');
      fs.appendFileSync(logFile, line, 'utf8');
      log.info('perf', `total=${r.phases.total}ms render=${r.phases.render}ms fitView=${r.phases.fitView}ms nodes=${r.nodeCount} → ${logFile}`);
    } catch (e) {
      log.warn('perfLog write failed', e instanceof Error ? e.message : String(e));
    }
  }

  private _titleForType(type: QueryType): string {
    const t: Record<QueryType, string> = {
      call_graph: '调用链图',
      class_graph: '类关系图',
      data_flow: '数据流图',
      control_flow: '控制流图',
    };
    return t[type] ?? type;
  }

  private _postInitGraph(type: QueryType, data: GraphData, querySymbol?: string, queryFile?: string): void {
    if (!this._panel?.webview) return;
    const filtered = this._filterToWorkspace(data);
    const nodes = filtered.nodes ?? [];
    const edges = filtered.edges ?? [];
    log.info('postMessage initGraph', { graphType: type, nodes: nodes.length, edges: edges.length });
    this._panel.webview.postMessage({
      action: 'initGraph',
      graphType: type,
      querySymbol,
      queryFile,
      nodes,
      edges,
    });
  }

  /**
   * 过滤掉工作区目录以外的符号节点及其关联边。
   * 节点文件路径来自 node.definition.file 或 node.file；
   * 相对路径会基于工作区根目录解析为绝对路径再判断。
   */
  private _filterToWorkspace(data: GraphData): GraphData {
    const wsRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (!wsRoot) return data;  // 无工作区则不过滤

    const normalizedRoot = path.normalize(wsRoot) + path.sep;

    const allNodes = data.nodes ?? [];
    const allEdges = data.edges ?? [];

    const keptNodes = allNodes.filter((n) => {
      const filePath = n.definition?.file ?? n.file;
      if (!filePath) return true;  // 无文件信息的节点保留（保守策略）
      const abs = path.isAbsolute(filePath) ? filePath : path.resolve(wsRoot, filePath);
      const norm = path.normalize(abs);
      return norm.startsWith(normalizedRoot) || norm === path.normalize(wsRoot);
    });

    if (keptNodes.length === allNodes.length) return data;  // 无需过滤

    const keptIds = new Set(keptNodes.map((n) => n.id));
    const keptEdges = allEdges.filter((e) => {
      const src = e.caller ?? (e as Record<string, unknown>).source as string ?? '';
      const tgt = e.callee ?? (e as Record<string, unknown>).target as string ?? '';
      return keptIds.has(src) && keptIds.has(tgt);
    });

    const removed = allNodes.length - keptNodes.length;
    log.info('过滤工作区外符号', { total: allNodes.length, kept: keptNodes.length, removed, removedEdges: allEdges.length - keptEdges.length });

    return { ...data, nodes: keptNodes, edges: keptEdges };
  }

  private _setGraphHtml(webview: vscode.Webview): void {
    const distPath = path.join(this._deps!.extensionUri.fsPath, 'resources', 'ui', 'dist', 'graph.html');
    try {
      if (fs.existsSync(distPath)) {
        let html = fs.readFileSync(distPath, 'utf8');
        const baseUri = vscode.Uri.joinPath(this._deps!.extensionUri, 'resources', 'ui', 'dist');
        html = html.replace(/(href|src)="([^"]+)"/g, (_: string, attr: string, value: string) => {
          const rel = value.startsWith('./') ? value.slice(1) : value.startsWith('/') ? value.slice(1) : value;
          const parts = rel.split('/').filter(Boolean);
          const full = parts.length ? vscode.Uri.joinPath(baseUri, ...parts) : baseUri;
          const uri = webview.asWebviewUri(full);
          return `${attr}="${uri}"`;
        });
        // G6 v5 uses a Web Worker loaded via blob: URL; worker-src and script-src need blob:
        const csp = `default-src 'none'; style-src ${webview.cspSource} 'unsafe-inline'; script-src ${webview.cspSource} 'unsafe-eval' blob:; worker-src blob:; font-src ${webview.cspSource} data:; connect-src ${webview.cspSource} blob:; img-src ${webview.cspSource} data: blob:;`;
        if (!html.includes('Content-Security-Policy')) {
          html = html.replace('<head>', `<head>\n  <meta http-equiv="Content-Security-Policy" content="${csp}">`);
        }
        if (!html.includes('graph-webview-layout')) {
          const themeKind = vscode.window.activeColorTheme.kind;
          const isDark = themeKind === 2 || themeKind === 3;
          const bg = isDark ? '#1e1e1e' : '#fffffe';
          const fg = isDark ? '#d4d4d4' : '#333333';
          html = html.replace('</head>', `<style id="graph-webview-layout">html, body { margin: 0; min-height: 100%; height: 100%; background: ${bg}; color: ${fg}; } #root { min-height: 100%; height: 100%; }</style>\n</head>`);
        }
        webview.html = html;
        log.info('图 Webview 已加载 dist/graph.html');
        return;
      }
    } catch (e) {
      log.warn('加载 graph.html 失败', e instanceof Error ? e.message : String(e));
    }
    const codiconCssUri = webview.asWebviewUri(
      vscode.Uri.joinPath(this._deps!.extensionUri, 'node_modules', '@vscode/codicons', 'dist', 'codicon.css')
    );
    webview.html = `<!DOCTYPE html><html><head><meta charset="UTF-8"><link rel="stylesheet" href="${codiconCssUri}"></head><body style="padding:16px;background:#1e1e1e;color:#d4d4d4;"><p>图资源加载失败，请执行 npm run build:ui</p></body></html>`;
    log.warn('图 Webview 使用 fallback 页面');
  }
}
