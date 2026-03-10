/**
 * 解析引擎服务：parse / query / list-runs CLI，进度回调
 * 参考：主仓库详细功能与架构设计 §4.4；01-解析引擎/接口约定
 */

import * as child_process from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import type { Config } from '../config';
import { createLogger } from '../logger';
import type { GraphData, ParseResult, ParseRun, QueryType } from '../types';

const log = createLogger('parserService');

const SUBCOMMAND_PARSE = 'parse';
const SUBCOMMAND_QUERY = 'query';
const SUBCOMMAND_LIST_RUNS = 'list-runs';

export interface QueryOptions {
  symbol?: string;
  file?: string;
  [key: string]: unknown;
}

export class ParserService {
  private _progressCallback: ((percent: number) => void) | undefined;
  private _disposed = false;

  constructor(private readonly config: Config) {
    log.debug('ParserService 已创建');
  }

  /** 注册解析进度回调（percent 0-100） */
  onProgress(callback: (percent: number) => void): void {
    log.debug('注册 onProgress 回调');
    this._progressCallback = callback;
  }

  /**
   * 执行解析；解析器不存在时返回错误态，不崩溃
   */
  async parse(options?: { incremental?: boolean }): Promise<ParseResult> {
    log.info('parse 开始', { incremental: options?.incremental });
    const parserPath = this.config.getParserPath();
    const project = this.config.getWorkspaceRootFsPath();
    const outputDb = this.config.getDatabasePath();
    const compileCommands = this.config.getCompileCommandsPath();
    const opts = this.config.getParserOptions();

    if (!project) {
      log.warn('无工作区根路径，跳过解析');
      return { status: 'error', message: '无工作区根路径' };
    }

    const compileCommandsResolved = compileCommands
      ? (path.isAbsolute(compileCommands) ? compileCommands : path.join(project, compileCommands))
      : '';
    if (!compileCommandsResolved) {
      log.warn('未配置 compile_commands 路径，无法解析');
      return {
        status: 'error',
        message: '请先设置 compile_commands.json 路径（设置 → CodeXray: compileCommandsPath，或执行命令 CodeXray: 设置 compile_commands.json 路径）',
      };
    }
    if (!fs.existsSync(compileCommandsResolved)) {
      log.warn('compile_commands 路径不存在', compileCommandsResolved);
      return {
        status: 'error',
        message: `compile_commands.json 路径不存在或不可读：${compileCommandsResolved}`,
      };
    }
    log.info('使用 compile_commands', compileCommandsResolved);
    const args = [
      SUBCOMMAND_PARSE,
      '--project', project,
      '--output-db', outputDb,
    ];
    if (compileCommandsResolved) {
      args.push('--compile-commands', compileCommandsResolved);
    }
    args.push('--parallel', String(opts.parallelism));
    if (opts.lazy) {
      args.push('--lazy');
      if (opts.priorityDirs.length > 0) {
        args.push('--priority-dirs', opts.priorityDirs.join(','));
      }
    }
    const useIncremental = options?.incremental ?? opts.incremental;
    if (useIncremental) {
      args.push('--incremental');
    }

    if (outputDb) {
      try {
        fs.mkdirSync(path.dirname(outputDb), { recursive: true });
      } catch (e) {
        log.warn('创建数据库目录失败', e instanceof Error ? e.message : String(e));
      }
    }

    return new Promise<ParseResult>((resolve) => {
      let lastSummary: ParseResult | undefined;
      try {
        const proc = child_process.spawn(parserPath, args, {
          cwd: project,
          stdio: ['ignore', 'pipe', 'pipe'],
        });

        proc.stdout?.on('data', (chunk: Buffer) => {
          const text = chunk.toString('utf8');
          const lines = text.split(/\r?\n/).filter((s) => s.trim());
          for (const line of lines) {
            try {
              const obj = JSON.parse(line) as Record<string, unknown>;
              const pct = typeof obj.percent === 'number' ? obj.percent : typeof obj.progress === 'number' ? obj.progress : undefined;
              if (pct !== undefined) {
                this._progressCallback?.(Math.min(100, Math.max(0, pct)));
              }
              if (obj.status === 'ok' || obj.status === 'error') {
                lastSummary = obj as unknown as ParseResult;
              }
            } catch {
              // 非 JSON 行忽略
            }
          }
        });

        proc.stderr?.on('data', (chunk: Buffer) => {
          log.warn('解析器 stderr', chunk.toString('utf8').trim());
        });

        proc.on('error', (err) => {
          log.error('解析器 spawn 失败', err.message);
          resolve({
            status: 'error',
            message: err.message || '未找到解析引擎，请构建并放置到扩展 bin 目录',
          });
        });

        proc.on('close', (code, signal) => {
          log.info('parse 进程结束', { code, signal });
          if (code === 0 && lastSummary) {
            resolve(lastSummary);
          } else if (code !== 0 && code != null) {
            const message = code === 4
              ? '解析失败：请确认工作区 .codexray 目录可写，且项目根或配置路径下存在有效的 compile_commands.json（如 CMake 生成）'
              : code === 2
                ? 'compile_commands.json 未找到或无效'
                : `解析退出码 ${code}`;
            resolve({
              status: 'error',
              message,
              errors: lastSummary?.errors,
            });
          } else {
            resolve(lastSummary ?? { status: code === 0 ? 'ok' : 'error', message: signal ? `信号 ${signal}` : undefined });
          }
        });
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        log.error('parse 异常', msg);
        resolve({ status: 'error', message: msg });
      }
    });
  }

  /**
   * 查询图数据；解析器不可用时返回空图并打日志
   */
  async query(type: QueryType, options: QueryOptions): Promise<GraphData> {
    log.info('query', { type, symbol: options.symbol, file: options.file });
    const parserPath = this.config.getParserPath();
    const project = this.config.getWorkspaceRootFsPath();
    const dbPath = this.config.getDatabasePath();
    const opts = this.config.getParserOptions();

    if (!project || !dbPath) {
      log.warn('无工作区或数据库路径');
      return {};
    }

    const args = [
      SUBCOMMAND_QUERY,
      '--db', dbPath,
      '--project', project,
      '--type', type,
    ];
    if (opts.lazy) {
      args.push('--lazy');
      if (opts.priorityDirs.length > 0) {
        args.push('--priority-dirs', opts.priorityDirs.join(','));
      }
    }
    args.push('--parallel', String(opts.parallelism));
    if (type === 'call_graph') {
      const depth = typeof options.depth === 'number' && options.depth > 0 ? options.depth : this._config.getQueryDepth();
      args.push('--depth', String(depth));
    }
    if (options.symbol) args.push('--symbol', options.symbol);
    if (options.file) args.push('--file', path.isAbsolute(options.file) ? options.file : path.join(project, options.file));

    return new Promise<GraphData>((resolve) => {
      let stdout = '';
      try {
        const proc = child_process.spawn(parserPath, args, {
          cwd: project,
          stdio: ['ignore', 'pipe', 'pipe'],
        });

        proc.stdout?.on('data', (chunk: Buffer) => {
          stdout += chunk.toString('utf8');
        });
        proc.stderr?.on('data', (chunk: Buffer) => {
          log.warn('query stderr', chunk.toString('utf8').trim());
        });
        proc.on('error', (err) => {
          log.error('query spawn 失败', err.message);
          resolve({});
        });
        proc.on('close', (code) => {
          if (code !== 0) {
            log.warn('query 退出码', code);
            resolve({});
            return;
          }
          try {
            const data = JSON.parse(stdout.trim() || '{}') as GraphData;
            const nodeCount = data.nodes?.length ?? 0;
            const edgeCount = data.edges?.length ?? 0;
            log.info('query 结果', {
              nodes: nodeCount,
              edges: edgeCount,
              nodeIds: data.nodes?.slice(0, 5).map((n) => n.id ?? n.name) ?? [],
            });
            resolve(data);
          } catch {
            log.warn('query 输出非 JSON');
            resolve({});
          }
        });
      } catch (err) {
        log.error('query 异常', err instanceof Error ? err.message : String(err));
        resolve({});
      }
    });
  }

  /**
   * 历史解析记录列表
   */
  async listRuns(limit?: number): Promise<ParseRun[]> {
    log.info('listRuns', { limit });
    const parserPath = this.config.getParserPath();
    const project = this.config.getWorkspaceRootFsPath();
    const dbPath = this.config.getDatabasePath();

    if (!dbPath) {
      log.warn('无数据库路径');
      return [];
    }

    const args = [SUBCOMMAND_LIST_RUNS, '--db', dbPath];
    if (project) args.push('--project', project);
    if (limit != null && limit > 0) args.push('--limit', String(limit));

    return new Promise<ParseRun[]>((resolve) => {
      let stdout = '';
      try {
        const proc = child_process.spawn(parserPath, args, {
          cwd: project || undefined,
          stdio: ['ignore', 'pipe', 'pipe'],
        });
        proc.stdout?.on('data', (chunk: Buffer) => {
          stdout += chunk.toString('utf8');
        });
        proc.stderr?.on('data', (chunk: Buffer) => {
          log.warn('list-runs stderr', chunk.toString('utf8').trim());
        });
        proc.on('error', (err) => {
          log.error('listRuns spawn 失败', err.message);
          resolve([]);
        });
        proc.on('close', (code) => {
          if (code !== 0) {
            log.warn('listRuns 退出码', code);
            resolve([]);
            return;
          }
          try {
            const arr = JSON.parse(stdout.trim() || '[]') as ParseRun[];
            resolve(Array.isArray(arr) ? arr : []);
          } catch {
            resolve([]);
          }
        });
      } catch (err) {
        log.error('listRuns 异常', err instanceof Error ? err.message : String(err));
        resolve([]);
      }
    });
  }

  dispose(): void {
    log.debug('ParserService dispose');
    this._progressCallback = undefined;
    this._disposed = true;
  }
}
