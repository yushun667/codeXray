/**
 * 与主仓库及 01-解析引擎/接口约定 一致的共享类型
 */

export interface DefinitionLocation {
  file: string;
  line: number;
  column: number;
}

export interface DefinitionRange {
  start_line: number;
  start_column: number;
  end_line: number;
  end_column: number;
}

export interface GraphNode {
  id: string;
  usr?: string;
  name: string;
  /** 带作用域前缀的符号名，如 Namespace::Class::function */
  qualified_name?: string;
  definition?: DefinitionLocation;
  definition_range?: DefinitionRange;
  file?: string;
  line?: number;
  [key: string]: unknown;
}

export interface GraphEdge {
  caller?: string;
  callee?: string;
  call_site?: string;
  edge_type?: string;
  /** class_graph */
  parent?: string;
  child?: string;
  relation_type?: string;
  /** control_flow */
  from?: string;
  to?: string;
  /** data_flow */
  var?: string;
  reader?: string;
  writer?: string;
  [key: string]: unknown;
}

export interface GraphData {
  nodes?: GraphNode[];
  edges?: GraphEdge[];
  [key: string]: unknown;
}

export interface ParseRun {
  run_id: string | number;
  started_at: string;
  finished_at?: string;
  mode: 'full' | 'incremental';
  files_parsed?: number;
  files_failed?: number;
  status: string;
  [key: string]: unknown;
}

export interface ChatContext {
  file?: string;
  symbol?: string;
  selection?: string;
}

export type GraphType = 'call_graph' | 'class_graph' | 'data_flow' | 'control_flow';
