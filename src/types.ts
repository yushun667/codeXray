/**
 * 主仓库共享类型定义
 * 与解析引擎接口约定、Agent 协议保持一致
 */

/** 解析引擎选项 */
export interface ParserOptions {
  parallelism: number;
  lazy: boolean;
  priorityDirs: string[];
  incremental: boolean;
}

/** 大模型配置（供 Agent 启动时写入环境或配置文件） */
export interface LlmConfig {
  provider: string;
  model: string;
  endpoint: string;
  apiKey: string;
}

/** 查询类型 */
export type QueryType = 'call_graph' | 'class_graph' | 'data_flow' | 'control_flow';

/** 节点定义位置 */
export interface DefinitionLocation {
  file: string;
  line: number;
  column: number;
}

/** 定义范围 */
export interface DefinitionRange {
  start_line: number;
  start_column: number;
  end_line: number;
  end_column: number;
}

/** 图节点（与解析引擎输出格式一致） */
export interface GraphNode {
  id: string;
  usr?: string;
  name: string;
  definition?: DefinitionLocation;
  definition_range?: DefinitionRange;
  file?: string;
  line?: number;
  [key: string]: unknown;
}

/** 图边 */
export interface GraphEdge {
  caller?: string;
  callee?: string;
  call_site?: string;
  edge_type?: string;
  [key: string]: unknown;
}

/** 查询返回的图数据 */
export interface GraphData {
  nodes?: GraphNode[];
  edges?: GraphEdge[];
  [key: string]: unknown;
}

/** 单次解析记录 */
export interface ParseRun {
  run_id: string;
  started_at: string;
  finished_at?: string;
  mode: 'full' | 'incremental';
  files_parsed?: number;
  status: string;
  [key: string]: unknown;
}

/** 解析结果摘要 */
export interface ParseResult {
  status: 'ok' | 'error';
  run_id?: string;
  mode?: 'full' | 'incremental';
  files_parsed?: number;
  symbols?: number;
  errors?: string[];
  message?: string;
}

/** AI 对话上下文（当前文件/符号/选区） */
export interface ChatContext {
  file?: string;
  symbol?: string;
  selection?: string;
}
