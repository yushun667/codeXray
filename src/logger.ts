/**
 * 日志模块：OutputChannel + 可选落盘，按 logLevel 过滤，带时间戳与 module 前缀
 * 参考：主仓库详细功能与架构设计 §4.3
 */

import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';

const CHANNEL_NAME = 'CodeXray';
const LEVEL_PRIORITY: Record<string, number> = { debug: 0, info: 1, warn: 2, error: 3 };

let outputChannel: vscode.OutputChannel | undefined;
let logPath: string | undefined;
let minLevel: number = LEVEL_PRIORITY.info;

/** 获取当前配置的日志级别对应的优先级数字 */
function getMinLevelPriority(levelName: string): number {
  return LEVEL_PRIORITY[levelName] ?? LEVEL_PRIORITY.info;
}

/** 设置日志落盘路径（由 extension 根据 config.getLogPath() 调用） */
export function setLogPath(path: string | undefined): void {
  logPath = path;
}

/** 设置最小日志级别（由 extension 根据 config.getLogLevel() 调用） */
export function setLogLevel(levelName: string): void {
  minLevel = getMinLevelPriority(levelName.toLowerCase());
}

function ensureChannel(): vscode.OutputChannel {
  if (!outputChannel) {
    outputChannel = vscode.window.createOutputChannel(CHANNEL_NAME);
  }
  return outputChannel;
}

function formatMessage(level: string, module: string, msg: string, ...args: unknown[]): string {
  const ts = new Date().toISOString();
  const rest = args.length ? ' ' + args.map((a) => (typeof a === 'object' ? JSON.stringify(a) : String(a))).join(' ') : '';
  return `[${ts}] [${level}] [${module}] ${msg}${rest}`;
}

function shouldLog(level: string): boolean {
  const p = LEVEL_PRIORITY[level] ?? LEVEL_PRIORITY.info;
  return p >= minLevel;
}

function appendToFile(line: string): void {
  if (!logPath) return;
  try {
    const dir = path.dirname(logPath);
    if (!fs.existsSync(dir)) {
      fs.mkdirSync(dir, { recursive: true });
    }
    fs.appendFileSync(logPath, line + '\n', 'utf8');
  } catch {
    // 落盘失败不阻塞，仅忽略
  }
}

export type LogFn = (msg: string, ...args: unknown[]) => void;

/**
 * 创建模块级 logger，返回 info/debug/warn/error
 * 每条带时间戳与 module 前缀；输出到 OutputChannel，可选落盘；按 setLogLevel 过滤
 */
export function createLogger(module: string): { info: LogFn; debug: LogFn; warn: LogFn; error: LogFn } {
  function log(level: string, msg: string, ...args: unknown[]): void {
    if (!shouldLog(level)) return;
    const line = formatMessage(level, module, msg, ...args);
    ensureChannel().appendLine(line);
    appendToFile(line);
  }
  return {
    info: (msg: string, ...args: unknown[]) => log('info', msg, ...args),
    debug: (msg: string, ...args: unknown[]) => log('debug', msg, ...args),
    warn: (msg: string, ...args: unknown[]) => log('warn', msg, ...args),
    error: (msg: string, ...args: unknown[]) => log('error', msg, ...args),
  };
}
