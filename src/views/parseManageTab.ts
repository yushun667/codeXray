/**
 * 解析管理标签页逻辑：runParse、listParseHistory、getProject、setCompileCommands
 * 参考：主仓库详细功能与架构设计 §4.7
 */

import * as vscode from 'vscode';
import { createLogger } from '../logger';
import type { Config } from '../config';
import type { ParserService } from '../services/parserService';
const log = createLogger('parseManageTab');

export interface ParseManageTabDeps {
  parserService: ParserService;
  config: Config;
}

export type PostToWebview = (msg: unknown) => void;

/**
 * 处理来自侧边栏 Webview 的解析管理相关消息
 */
export function handleMessage(
  action: string,
  payload: Record<string, unknown>,
  deps: ParseManageTabDeps,
  post: PostToWebview
): void {
  log.debug('handleMessage', action);
  switch (action) {
    case 'runParse':
      runParse(deps, post);
      break;
    case 'listParseHistory':
      listParseHistory(payload, deps, post);
      break;
    case 'getProject':
      getProject(deps, post);
      break;
    case 'setCompileCommands':
      setCompileCommands(payload, deps, post);
      break;
    default:
      log.warn('未知 action', action);
  }
}

function runParse(deps: ParseManageTabDeps, post: PostToWebview): void {
  log.info('runParse 执行');
  deps.parserService.parse().then((result) => {
    post({ action: 'parseResult', result });
  }).catch((e) => {
    log.error('runParse 异常', e instanceof Error ? e.message : String(e));
    post({ action: 'parseResult', result: { status: 'error', message: String(e) } });
  });
}

function listParseHistory(payload: Record<string, unknown>, deps: ParseManageTabDeps, post: PostToWebview): void {
  const limit = typeof payload.limit === 'number' ? payload.limit : 20;
  log.info('listParseHistory 执行', limit);
  deps.parserService.listRuns(limit).then((runs) => {
    post({ action: 'parseHistory', runs });
  }).catch((e) => {
    log.error('listParseHistory 异常', e instanceof Error ? e.message : String(e));
    post({ action: 'parseHistory', runs: [] });
  });
}

function getProject(deps: ParseManageTabDeps, post: PostToWebview): void {
  const root = deps.config.getWorkspaceRootFsPath();
  const compileCommands = deps.config.getCompileCommandsPath();
  const databasePath = deps.config.getDatabasePath();
  post({ action: 'projectInfo', project: { root, compileCommands, databasePath } });
}

function setCompileCommands(payload: Record<string, unknown>, deps: ParseManageTabDeps, post: PostToWebview): void {
  const path = payload.path as string | undefined;
  if (typeof path !== 'string') {
    post({ action: 'setCompileCommandsResult', ok: false, message: '无效路径' });
    return;
  }
  const cfg = vscode.workspace.getConfiguration('codexray');
  void cfg.update('compileCommandsPath', path, vscode.ConfigurationTarget.Workspace).then(
    () => post({ action: 'setCompileCommandsResult', ok: true }),
    (e: unknown) => post({ action: 'setCompileCommandsResult', ok: false, message: String(e) })
  );
}