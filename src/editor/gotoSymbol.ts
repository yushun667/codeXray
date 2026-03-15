/**
 * 从可视化定位到代码：showTextDocument + revealRange
 * 参考：主仓库详细功能与架构设计 §4.11
 */

import * as vscode from 'vscode';
import { createLogger } from '../logger';

const log = createLogger('gotoSymbol');

/**
 * 在编辑器中打开文件并定位到指定行列（1-based），滚动到该位置并移动光标
 */
export async function execute(uri: vscode.Uri, line: number, column: number): Promise<void> {
  log.info('execute', { uri: uri.fsPath, line, column });
  await vscode.window.showTextDocument(uri, { preserveFocus: false });
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.uri.fsPath !== uri.fsPath) return;
  const zeroBasedLine = Math.max(0, line - 1);
  const zeroBasedCol = Math.max(0, column - 1);
  const range = new vscode.Range(zeroBasedLine, zeroBasedCol, zeroBasedLine, zeroBasedCol);
  editor.revealRange(range, vscode.TextEditorRevealType.InCenter);
  editor.selection = new vscode.Selection(range.start, range.end);
}

/**
 * 注册 codexray.gotoSymbolInEditor 命令（接收 args: uri, line, column）
 */
export function registerGotoSymbolCommand(_context: vscode.ExtensionContext): vscode.Disposable {
  log.debug('注册 gotoSymbolInEditor 命令');
  return vscode.commands.registerCommand(
    'codexray.gotoSymbolInEditor',
    async (uri: vscode.Uri | string, line?: number, column?: number) => {
      const u = typeof uri === 'string' ? vscode.Uri.file(uri) : uri;
      const l = typeof line === 'number' ? line : 1;
      const c = typeof column === 'number' ? column : 1;
      await execute(u, l, c);
    }
  );
}
