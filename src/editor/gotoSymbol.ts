/**
 * 从可视化定位到代码：showTextDocument + revealRange
 * 参考：主仓库详细功能与架构设计 §4.11
 */

import * as vscode from 'vscode';
import { createLogger } from '../logger';

const log = createLogger('gotoSymbol');

/**
 * 在编辑器中打开文件并定位到指定行列（1-based）
 */
export async function execute(uri: vscode.Uri, line: number, column: number): Promise<void> {
  log.info('execute', { uri: uri.fsPath, line, column });
  const doc = await vscode.window.showTextDocument(uri, { preserveFocus: false });
  const zeroBasedLine = Math.max(0, line - 1);
  const zeroBasedCol = Math.max(0, column - 1);
  const range = new vscode.Range(zeroBasedLine, zeroBasedCol, zeroBasedLine, zeroBasedCol);
  doc.revealRange(range, vscode.TextEditorRevealType.InCenter);
}

/**
 * 注册 codexray.gotoSymbolInEditor 命令（接收 args: uri, line, column）
 */
export function registerGotoSymbolCommand(context: vscode.ExtensionContext): vscode.Disposable {
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
