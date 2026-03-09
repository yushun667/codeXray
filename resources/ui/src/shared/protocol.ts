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

// 图 UI -> host
export type GraphAction = 'gotoSymbol' | 'queryPredecessors' | 'querySuccessors';

// 图 host -> UI
export type GraphMessageAction = 'initGraph' | 'graphAppend';

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

export interface HostToGraphMessage {
  type: GraphMessageAction;
  graphType?: string;
  data?: { nodes?: unknown[]; edges?: unknown[] };
  nodes?: unknown[];
  edges?: unknown[];
}

export interface GraphToHostMessage {
  action: GraphAction;
  uri?: string;
  line?: number;
  column?: number;
  graphType?: string;
  nodeId?: string;
  symbol?: string;
}
