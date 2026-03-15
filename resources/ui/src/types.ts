// ── Shared type definitions ──

/** Node definition location in source code */
export interface DefinitionLocation {
  file: string;
  line: number;
  column: number;
}

/** Graph node from the parser backend */
export interface GraphNode {
  id: string;
  usr?: string;
  name: string;
  definition?: DefinitionLocation;
  definition_range?: { start: DefinitionLocation; end: DefinitionLocation };
  file?: string;
  line?: number;
}

/** Graph edge from the parser backend */
export interface GraphEdge {
  id?: string;
  caller: string;
  callee: string;
  call_site?: DefinitionLocation;
  edge_type?: string;
  source?: string;
  target?: string;
}

/** Layout configuration */
export interface LayoutConfig {
  rankSep: number;
  nodeSep: number;
  nodeWidth: number;
  nodeHeight: number;
  padding: number;
  maxCollisionIter?: number;
}

/** Internal layout node with computed position */
export interface LayoutNode {
  id: string;
  rank: number;
  x: number;
  y: number;
  width: number;
  height: number;
  raw: GraphNode;
}

/** Port direction */
export type PortKey = 'top' | 'bottom' | 'left' | 'right';

/** Port selection result for an edge */
export interface PortSelection {
  sourcePort: PortKey;
  targetPort: PortKey;
}

/** Query type for the parser backend */
export type QueryType = 'call_graph' | 'class_graph' | 'data_flow' | 'control_flow';

/** Performance report */
export interface PerfReport {
  trigger: string;
  nodeCount: number;
  edgeCount: number;
  phases: {
    computeLayout: number;
    resolveCollisions: number;
    layoutWorkerTime: number;
    buildG6Data: number;
    setData: number;
    render: number;
    fitView: number;
    total: number;
  };
}

// ── Messages: Extension → WebView ──

export interface InitGraphMessage {
  action: 'initGraph';
  graphType: QueryType;
  querySymbol?: string;
  queryFile?: string;
  queryDepth?: number;
  nodes: GraphNode[];
  edges: GraphEdge[];
}

export interface GraphAppendMessage {
  action: 'graphAppend';
  nodes: GraphNode[];
  edges: GraphEdge[];
}

export type GraphMessage = InitGraphMessage | GraphAppendMessage;

// ── Messages: WebView → Extension ──

export interface GraphReadyMessage {
  action: 'graphReady';
}

export interface GotoSymbolMessage {
  action: 'gotoSymbol';
  file: string;
  line: number;
  column: number;
}

export interface QueryMessage {
  action: 'queryPredecessors' | 'querySuccessors';
  graphType: QueryType;
  symbol: string;
  file: string;
  queryDepth?: number;
}

export interface PerfReportMessage {
  action: 'perfReport';
  report: PerfReport;
}

export type ToExtensionMessage =
  | GraphReadyMessage
  | GotoSymbolMessage
  | QueryMessage
  | PerfReportMessage;

// ── Worker messages ──

export interface LayoutWorkerInput {
  nodes: GraphNode[];
  edges: GraphEdge[];
  rootId: string;
  config?: Partial<LayoutConfig>;
}

export interface LayoutWorkerResult {
  type: 'result';
  layoutNodes: LayoutNode[];
}

// ── Theme colors ──

export interface ThemeColors {
  rootFill: string;
  rootStroke: string;
  nodeFill: string;
  nodeStroke: string;
  edgeStroke: string;
  highlightStroke: string;
  selectedStroke: string;
  dimmedOpacity: number;
  pathAnimColor: string;
  menuBg: string;
  menuBorder: string;
  menuText: string;
  menuTextDanger: string;
  menuHover: string;
  toolbarBg: string;
  toolbarBorder: string;
  bg: string;
  fg: string;
  searchBg: string;
  searchBorder: string;
}
