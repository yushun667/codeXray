/**
 * 编辑器集成：当前符号、query* 命令、右键菜单对应
 * 查询入口在代码编辑区；调用 parserService.query → visualizationProvider.openGraph
 * 参考：主仓库详细功能与架构设计 §4.9
 */

import * as vscode from 'vscode';
import { createLogger } from '../logger';
import type { ParserService } from '../services/parserService';
import type { QueryType } from '../types';
import type { VisualizationProvider } from '../views/visualizationProvider';

const log = createLogger('editorIntegration');

export interface CurrentSymbol {
  name: string;
  file: string;
  range: vscode.Range;
}

/**
 * 从当前活动编辑器与选区/光标解析当前符号（简单基于选区或光标处单词）
 */
export function getCurrentSymbol(): CurrentSymbol | undefined {
  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    log.debug('getCurrentSymbol: 无活动编辑器');
    return undefined;
  }
  const doc = editor.document;
  const file = doc.uri.fsPath;
  const selection = editor.selection;
  let name: string;
  let range: vscode.Range;
  if (!selection.isEmpty) {
    name = doc.getText(selection).trim();
    range = selection;
  } else {
    const wordRange = doc.getWordRangeAtPosition(selection.active);
    if (!wordRange) {
      log.debug('getCurrentSymbol: 光标处无单词');
      return undefined;
    }
    name = doc.getText(wordRange).trim();
    range = wordRange;
  }
  if (!name) {
    log.debug('getCurrentSymbol: 符号名为空');
    return undefined;
  }
  log.debug('getCurrentSymbol', { name, file });
  return { name, file, range };
}

/**
 * 注册编辑器侧 query* 命令；由 extension 注入 parserService 与 visualizationProvider
 */
export function registerEditorCommands(
  context: vscode.ExtensionContext,
  parserService: ParserService,
  visualizationProvider: VisualizationProvider
): void {
  log.info('registerEditorCommands 开始');

  const runQuery = async (type: QueryType): Promise<void> => {
    const sym = getCurrentSymbol();
    const file = vscode.window.activeTextEditor?.document.uri.fsPath;
    const symbol = sym?.name;
    if (!file) {
      log.warn('runQuery: 无打开文件');
      void vscode.window.showWarningMessage('请先打开 C/C++ 文件并聚焦到符号');
      return;
    }
    log.info('runQuery', { type, symbol, file });
    const data = await parserService.query(type, { symbol, file });
    if (!data || (Array.isArray(data.nodes) && data.nodes.length === 0 && (!data.edges || data.edges.length === 0))) {
      void vscode.window.showInformationMessage('未查询到图数据，请先执行解析或检查符号');
      return;
    }
    visualizationProvider.openGraph(type, data);
  };

  const reg = (id: string, type: QueryType): vscode.Disposable =>
    vscode.commands.registerCommand(id, () => runQuery(type));

  context.subscriptions.push(
    reg('codexray.queryCallGraph', 'call_graph'),
    reg('codexray.queryClassGraph', 'class_graph'),
    reg('codexray.queryDataFlow', 'data_flow'),
    reg('codexray.queryControlFlow', 'control_flow')
  );
  log.debug('registerEditorCommands 完成');
}
