/**
 * AI 对话标签页逻辑：sendChat、getContext，流式回复通过 post 回传 Webview
 * 参考：主仓库详细功能与架构设计 §4.8
 */

import { createLogger } from '../logger';
import { getCurrentSymbol } from '../editor/currentSymbol';
import type { AgentService } from '../services/agentService';
import type { ChatContext } from '../types';

const log = createLogger('chatTab');

export interface ChatTabDeps {
  agentService: AgentService;
}

export type PostToWebview = (msg: unknown) => void;

/**
 * 处理来自侧边栏 Webview 的 AI 对话相关消息
 */
export function handleMessage(
  action: string,
  payload: Record<string, unknown>,
  deps: ChatTabDeps,
  post: PostToWebview
): void {
  log.debug('handleMessage', action);
  switch (action) {
    case 'sendChat':
      sendChat(payload, deps, post);
      break;
    case 'getContext':
      getContext(post);
      break;
    default:
      log.warn('未知 action', action);
  }
}

function sendChat(payload: Record<string, unknown>, deps: ChatTabDeps, post: PostToWebview): void {
  const message = (payload.message as string) ?? '';
  const context = (payload.context as ChatContext) ?? {};
  log.info('sendChat', { messageLen: message.length });
  deps.agentService.sendChat(message, context).then(() => {
    // 回复通过 onReply 回调由 extension 侧 post 到 webview
  }).catch((e) => {
    log.error('sendChat 异常', e instanceof Error ? e.message : String(e));
    post({ action: 'chatReply', chunk: null, full: '（发送失败）' });
  });
}

function getContext(post: PostToWebview): void {
  const sym = getCurrentSymbol();
  const context: ChatContext = {};
  if (sym) {
    context.file = sym.file;
    context.symbol = sym.name;
  }
  post({ action: 'context', context });
}
