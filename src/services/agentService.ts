/**
 * Agent 服务：连接（stdio/socket/HTTP）、chat、流式回复、session_id
 * 参考：主仓库详细功能与架构设计 §4.5；03-Agent模块/接口与协议
 */

import * as child_process from 'child_process';
import * as crypto from 'crypto';
import type { Config } from '../config';
import { createLogger } from '../logger';
import type { ChatContext } from '../types';

const log = createLogger('agentService');

type ConnectionKind = 'stdio' | 'http' | 'socket' | 'none';

export class AgentService {
  private _sessionId: string;
  private _streamChunkCb: ((chunk: string) => void) | undefined;
  private _replyCb: ((full: string) => void) | undefined;
  private _proc: child_process.ChildProcess | null = null;
  private _connected = false;
  private _kind: ConnectionKind = 'none';
  private _disposed = false;
  private _outBuffer = '';

  constructor(private readonly config: Config) {
    this._sessionId = crypto.randomUUID();
    log.debug('AgentService 已创建', { sessionId: this._sessionId });
  }

  /** 当前会话 ID */
  getSessionId(): string {
    return this._sessionId;
  }

  /** 是否已连接 */
  isConnected(): boolean {
    return this._connected;
  }

  /**
   * 建立连接。endpoint 为空则不连接；stdio 时 spawn 子进程并注入 LLM 环境变量
   */
  async connect(): Promise<void> {
    log.info('connect 开始');
    const endpoint = this.config.getAgentEndpoint().trim().toLowerCase();
    if (!endpoint) {
      log.info('Agent endpoint 未配置，跳过连接');
      this._connected = false;
      this._kind = 'none';
      return;
    }
    if (endpoint === 'stdio' || endpoint === '') {
      return this._connectStdio();
    }
    if (endpoint.startsWith('http://') || endpoint.startsWith('https://')) {
      this._kind = 'http';
      this._connected = true;
      log.info('Agent HTTP 模式（发送时请求）', endpoint);
      return;
    }
    log.warn('暂不支持的 endpoint 类型', endpoint);
    this._connected = false;
    this._kind = 'none';
  }

  private async _connectStdio(): Promise<void> {
    const agentPath = this.config.getAgentPath();
    const llm = this.config.getLlmConfig();
    const env = { ...process.env };
    if (llm.provider) env['CODEXRAY_LLM_PROVIDER'] = llm.provider;
    if (llm.model) env['CODEXRAY_LLM_MODEL'] = llm.model;
    if (llm.endpoint) env['CODEXRAY_LLM_ENDPOINT'] = llm.endpoint;
    if (llm.apiKey) env['CODEXRAY_LLM_API_KEY'] = llm.apiKey;

    return new Promise((resolve) => {
      try {
        const proc = child_process.spawn(agentPath, [], { env, stdio: ['pipe', 'pipe', 'pipe'] });
        this._proc = proc;
        this._outBuffer = '';

        proc.stdout?.on('data', (chunk: Buffer) => {
          this._outBuffer += chunk.toString('utf8');
          const lines = this._outBuffer.split(/\r?\n/);
          this._outBuffer = lines.pop() ?? '';
          for (const line of lines) {
            if (!line.trim()) continue;
            try {
              const obj = JSON.parse(line) as Record<string, unknown>;
              if (typeof obj.chunk === 'string') {
                this._streamChunkCb?.(obj.chunk);
              }
              if (typeof obj.reply === 'string') {
                this._replyCb?.(obj.reply);
              }
            } catch {
              // 非 JSON 行忽略
            }
          }
        });
        proc.stderr?.on('data', (chunk: Buffer) => {
          log.warn('Agent stderr', chunk.toString('utf8').trim());
        });
        proc.on('error', (err) => {
          log.error('Agent spawn 失败', err.message);
          this._connected = false;
          this._proc = null;
          resolve();
        });
        proc.on('close', (code) => {
          log.info('Agent 进程退出', code);
          this._connected = false;
          this._proc = null;
        });
        this._connected = true;
        this._kind = 'stdio';
        log.info('Agent stdio 已连接');
        resolve();
      } catch (err) {
        log.error('Agent connect 异常', err instanceof Error ? err.message : String(err));
        this._connected = false;
        resolve();
      }
    });
  }

  /** 发送对话请求；未连接时仅打日志，不抛错 */
  async sendChat(message: string, context?: ChatContext): Promise<void> {
    log.info('sendChat', { messageLen: message.length, hasContext: !!context });
    if (!this._connected) {
      log.warn('Agent 未连接，请配置 Agent 并启动服务');
      this._replyCb?.('（未连接：请配置 codexray.agentEndpoint 并启动 Agent 服务）');
      return;
    }
    const payload = {
      action: 'chat',
      session_id: this._sessionId,
      message,
      context: context ?? {},
    };
    if (this._kind === 'stdio' && this._proc?.stdin?.writable) {
      try {
        this._proc.stdin.write(JSON.stringify(payload) + '\n', 'utf8');
      } catch (err) {
        log.error('sendChat 写入失败', err instanceof Error ? err.message : String(err));
        this._replyCb?.('（发送失败）');
      }
      return;
    }
    if (this._kind === 'http') {
      const endpoint = this.config.getAgentEndpoint().trim();
      try {
        const res = await fetch(endpoint, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload),
        });
        if (!res.ok) {
          this._replyCb?.(`（请求失败: ${res.status}）`);
          return;
        }
        const text = await res.text();
        try {
          const data = JSON.parse(text) as { reply?: string };
          if (typeof data.reply === 'string') {
            this._replyCb?.(data.reply);
          } else {
            this._replyCb?.(text);
          }
        } catch {
          this._replyCb?.(text);
        }
      } catch (err) {
        log.error('sendChat HTTP 失败', err instanceof Error ? err.message : String(err));
        this._replyCb?.('（连接失败：请检查 Agent 服务地址）');
      }
    }
  }

  onStreamChunk(callback: (chunk: string) => void): void {
    this._streamChunkCb = callback;
  }

  onReply(callback: (full: string) => void): void {
    this._replyCb = callback;
  }

  disconnect(): void {
    log.info('disconnect');
    if (this._proc) {
      this._proc.kill();
      this._proc = null;
    }
    this._connected = false;
    this._kind = 'none';
  }

  dispose(): void {
    log.debug('AgentService dispose');
    this.disconnect();
    this._streamChunkCb = undefined;
    this._replyCb = undefined;
    this._disposed = true;
  }
}
