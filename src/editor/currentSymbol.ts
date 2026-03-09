/**
 * 从当前活动编辑器与选区/光标解析当前符号（供 AI 对话等使用）
 */

import * as vscode from 'vscode';
import { createLogger } from '../logger';

const log = createLogger('currentSymbol');

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
