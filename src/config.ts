/**
 * 配置与路径模块
 * 读取 codexray.* 配置；工作区根、DB、解析器路径；不写配置、不弹窗。
 * 参考：主仓库详细功能与架构设计 §4.2
 */

import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import * as vscode from 'vscode';
import type { ParserOptions, LlmConfig } from './types';

const CONFIG_SECTION = 'codexray';

export class Config {
  private _context: vscode.ExtensionContext | undefined;

  /** 注入 ExtensionContext，用于 getParserPath */
  init(context: vscode.ExtensionContext): void {
    this._context = context;
  }

  /** 工作区根路径；基于当前工作区以支持 Remote */
  getWorkspaceRoot(): vscode.Uri | undefined {
    return vscode.workspace.workspaceFolders?.[0]?.uri;
  }

  /** 工作区根路径字符串，无工作区时返回空串 */
  getWorkspaceRootFsPath(): string {
    const root = this.getWorkspaceRoot();
    return root ? root.fsPath : '';
  }

  /** 数据库路径，默认 <workspace>/.codexray/codexray.db */
  getDatabasePath(): string {
    const cfg = vscode.workspace.getConfiguration(CONFIG_SECTION);
    const custom = cfg.get<string>('databasePath')?.trim();
    if (custom) {
      return custom;
    }
    const root = this.getWorkspaceRootFsPath();
    if (!root) return '';
    return path.join(root, '.codexray', 'codexray.db');
  }

  /** compile_commands.json 路径 */
  getCompileCommandsPath(): string {
    const cfg = vscode.workspace.getConfiguration(CONFIG_SECTION);
    return (cfg.get<string>('compileCommandsPath') ?? '').trim();
  }

  /** 解析器可执行体路径；优先 bin/，开发时若不存在则用 parser/build/ */
  getParserPath(): string {
    if (!this._context) {
      return path.join('bin', process.platform === 'win32' ? 'codexray-parser.exe' : 'codexray-parser');
    }
    const base = process.platform === 'win32' ? 'codexray-parser.exe' : 'codexray-parser';
    const binPath = this._context.asAbsolutePath(path.join('bin', base));
    if (fs.existsSync(binPath)) return binPath;
    const buildPath = this._context.asAbsolutePath(path.join('parser', 'build', base));
    if (fs.existsSync(buildPath)) return buildPath;
    return binPath;
  }

  /** Agent 可执行体路径（stdio 模式时 spawn 用），随扩展打包 */
  getAgentPath(): string {
    if (!this._context) {
      return path.join('bin', process.platform === 'win32' ? 'codexray-agent.exe' : 'codexray-agent');
    }
    const base = process.platform === 'win32' ? 'codexray-agent.exe' : 'codexray-agent';
    return this._context.asAbsolutePath(path.join('bin', base));
  }

  /** 图查询默认深度（调用链/类关系等），1–10，默认 2 */
  getQueryDepth(): number {
    const cfg = vscode.workspace.getConfiguration(CONFIG_SECTION);
    const d = cfg.get<number>('queryDepth') ?? 2;
    return Math.max(1, Math.min(10, Math.floor(d)));
  }

  /** 解析选项：并行度、懒解析、优先目录、增量 */
  getParserOptions(): ParserOptions {
    const cfg = vscode.workspace.getConfiguration(CONFIG_SECTION);
    let parallelism = cfg.get<number>('parserParallelism') ?? 0;
    if (parallelism <= 0) {
      const cpus = os.cpus().length;
      parallelism = Math.max(1, cpus - 2);
    }
    return {
      parallelism,
      lazy: cfg.get<boolean>('parserLazy') ?? true,
      priorityDirs: cfg.get<string[]>('parserPriorityDirs') ?? [],
      incremental: cfg.get<boolean>('parserIncremental') ?? true,
    };
  }

  /** Agent 服务地址（stdio / socket 路径 / HTTP URL） */
  getAgentEndpoint(): string {
    const cfg = vscode.workspace.getConfiguration(CONFIG_SECTION);
    return (cfg.get<string>('agentEndpoint') ?? '').trim();
  }

  /** 大模型配置，供 Agent 启动时写入 */
  getLlmConfig(): LlmConfig {
    const cfg = vscode.workspace.getConfiguration(CONFIG_SECTION);
    return {
      provider: (cfg.get<string>('llmProvider') ?? '').trim(),
      model: (cfg.get<string>('llmModel') ?? '').trim(),
      endpoint: (cfg.get<string>('llmEndpoint') ?? '').trim(),
      apiKey: (cfg.get<string>('llmApiKey') ?? '').trim(),
    };
  }

  /** 日志级别 */
  getLogLevel(): string {
    const cfg = vscode.workspace.getConfiguration(CONFIG_SECTION);
    return (cfg.get<string>('logLevel') ?? 'info').toLowerCase();
  }

  /** 日志落盘路径，空表示不落盘 */
  getLogPath(): string | undefined {
    const cfg = vscode.workspace.getConfiguration(CONFIG_SECTION);
    const p = (cfg.get<string>('logPath') ?? '').trim();
    return p || undefined;
  }
}
