/**
 * UI 与主仓库 postMessage 协议（与主仓库约定一致）
 */

// 侧边栏 UI -> host
export type SidebarAction =
  | 'runParse'
  | 'listParseHistory'
  | 'getProject'
  | 'setCompileCommands'
  | 'sendChat'
  | 'getContext';

// 侧边栏 host -> UI（message.action 或 message.type）
export type SidebarMessageAction =
  | 'projectInfo'
  | 'initState'
  | 'parseProgress'
  | 'parseResult'
  | 'parseDone'
  | 'parseHistory'
  | 'chatReply'
  | 'replyChunk'
  | 'replyDone'
  | 'context'
  | 'setCompileCommandsResult'
  | 'error';

export interface HostToSidebarMessage {
  action: SidebarMessageAction;
  project?: { root?: string; compileCommands?: string; databasePath?: string };
  percent?: number;
  result?: { status: string; message?: string; files_parsed?: number; files_failed?: number };
  runs?: unknown[];
  full?: string;
  chunk?: string;
  context?: { file?: string; symbol?: string };
  ok?: boolean;
  message?: string;
}

export interface SidebarToHostMessage {
  action: SidebarAction;
  payload?: { message?: string; context?: unknown; path?: string; limit?: number };
}
