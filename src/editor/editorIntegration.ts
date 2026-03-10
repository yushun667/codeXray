/**
 * 编辑器集成：当前符号、右键菜单与 query* 命令注册；
 * 命令执行时取符号 → parserService.query → visualizationProvider.openGraph
 */

import * as vscode from 'vscode';
import { createLogger } from '../logger';
import { getCurrentSymbol } from './currentSymbol';
import type { ParserService } from '../services/parserService';
import type { VisualizationProvider } from '../views/visualizationProvider';
import type { QueryType } from '../types';

const log = createLogger('editorIntegration');

export interface EditorIntegrationDeps {
  parserService: ParserService;
  visualizationProvider: VisualizationProvider;
}

const QUERY_COMMANDS: { id: string; title: string; type: QueryType }[] = [
  { id: 'codexray.queryCallGraph', title: '查看调用链', type: 'call_graph' },
  { id: 'codexray.queryClassGraph', title: '查看类关系', type: 'class_graph' },
  { id: 'codexray.queryDataFlow', title: '查看数据流', type: 'data_flow' },
  { id: 'codexray.queryControlFlow', title: '查看控制流', type: 'control_flow' },
];

/**
 * 注册编辑器查询命令（queryCallGraph / queryClassGraph / queryDataFlow / queryControlFlow）
 */
export function registerEditorCommands(
  context: vscode.ExtensionContext,
  deps: EditorIntegrationDeps
): vscode.Disposable[] {
  const disposables: vscode.Disposable[] = [];
  for (const { id, title, type } of QUERY_COMMANDS) {
    const d = vscode.commands.registerCommand(id, async () => {
      const sym = getCurrentSymbol();
      if (!sym) {
        void vscode.window.showWarningMessage('请将光标置于符号上或选中符号后再执行「' + title + '」');
        return;
      }
      log.info('query', { type, symbol: sym.name, file: sym.file });
      const data = await deps.parserService.query(type, {
        symbol: sym.name,
        file: sym.file,
      });
      const nodeCount = data.nodes?.length ?? 0;
      const edgeCount = data.edges?.length ?? 0;
      log.info('查询结果', { type, nodes: nodeCount, edges: edgeCount });
      deps.visualizationProvider.openGraph(type, data);
    });
    disposables.push(d);
  }
  log.debug('已注册编辑器查询命令', QUERY_COMMANDS.length);
  return disposables;
}
