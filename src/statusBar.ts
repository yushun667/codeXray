/**
 * 状态栏：解析进度百分比、完成/失败态
 * 参考：主仓库详细功能与架构设计 §4.12
 */

import * as vscode from 'vscode';
import { createLogger } from './logger';

const log = createLogger('statusBar');

export class StatusBar {
  private _item: vscode.StatusBarItem;
  private _visible = false;

  constructor() {
    this._item = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right);
    log.debug('StatusBar 已创建');
  }

  /** 更新解析进度文案，如「CodeXray 解析 45%」 */
  updateProgress(percent: number): void {
    const n = Math.min(100, Math.max(0, Math.round(percent)));
    this._item.text = `$(sync~spin) CodeXray 解析 ${n}%`;
    this._item.tooltip = 'CodeXray 正在解析工程…';
    this._show();
    log.debug('updateProgress', n);
  }

  /** 解析完成 */
  setDone(): void {
    this._item.text = '$(check) CodeXray 解析完成';
    this._item.tooltip = 'CodeXray 解析已完成';
    this._show();
    log.debug('setDone');
  }

  /** 解析失败 */
  setFailed(message?: string): void {
    this._item.text = '$(error) CodeXray 解析失败';
    this._item.tooltip = message ?? 'CodeXray 解析失败';
    this._show();
    log.debug('setFailed', message);
  }

  /** 隐藏状态栏项 */
  hide(): void {
    this._item.hide();
    this._visible = false;
  }

  private _show(): void {
    this._item.show();
    this._visible = true;
  }

  dispose(): void {
    log.debug('StatusBar dispose');
    this._item.dispose();
  }
}
