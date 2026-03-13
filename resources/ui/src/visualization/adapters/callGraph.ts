/**
 * 调用链图：将解析引擎 call_graph 查询结果转为 React Flow nodes/edges
 */

import type { Node, Edge } from 'reactflow';
import type { GraphData, GraphNode as ApiNode, GraphEdge as ApiEdge } from '../../shared/types';

export interface FlowNodeData {
  label: string;
  definition?: { file: string; line: number; column: number };
  definition_range?: { start_line: number; start_column: number; end_line: number; end_column: number };
  usr?: string;
  name?: string;
  /** 是否为查询根节点（初始查询结果中的节点），用于高亮显示 */
  isRoot?: boolean;
}

/** 节点显示名：作用域前缀(qualified_name) + 文件名 + 行号，不使用 USR（供各 adapter 复用） */
export function nodeLabel(n: ApiNode): string {
  const name = (n as { qualified_name?: string }).qualified_name ?? n.name ?? '?';
  const def = n.definition;
  if (def?.file != null && def?.line != null) {
    const base = def.file.replace(/^.*[/\\]/, '');
    return `${name}\n(${base}:${def.line})`;
  }
  return name;
}

/**
 * call_graph 数据 -> React Flow nodes/edges（position 由 layout 后续计算）
 */
export function adaptCallGraph(data: GraphData): { nodes: Node<FlowNodeData>[]; edges: Edge[] } {
  const nodes: Node<FlowNodeData>[] = [];
  const edges: Edge[] = [];
  const apiNodes = data.nodes ?? [];
  const apiEdges = data.edges ?? [];

  apiNodes.forEach((n: ApiNode, i: number) => {
    // 解析引擎返回的 id 为整数，React Flow 要求 string 类型
    const nodeId = String(n.id ?? `n${i}`);
    nodes.push({
      id: nodeId,
      type: 'graphNode',
      position: { x: 0, y: 0 },
      data: {
        label: nodeLabel(n),
        definition: n.definition,
        definition_range: n.definition_range,
        usr: n.usr,
        name: n.name,
      },
    });
  });

  apiEdges.forEach((e: ApiEdge, i: number) => {
    // 解析引擎返回的 caller/callee 为整数，必须转为 string 与节点 id 匹配
    const src = String(e.caller ?? (e as { source?: string }).source ?? '');
    const tgt = String(e.callee ?? (e as { target?: string }).target ?? '');
    if (src && tgt) {
      const id = `e-${src}-${tgt}-${e.edge_type ?? i}`;
      edges.push({
        id,
        source: src,
        target: tgt,
        data: { edge_type: e.edge_type },
      });
    }
  });

  return { nodes, edges };
}
