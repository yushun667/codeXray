/**
 * 类关系图：将解析引擎 class_graph 查询结果转为 React Flow nodes/edges
 */

import type { Node, Edge } from 'reactflow';
import type { GraphData, GraphNode as ApiNode, GraphEdge as ApiEdge } from '../../shared/types';
import type { FlowNodeData } from './callGraph';
import { nodeLabel } from './callGraph';

/**
 * class_graph 数据 -> React Flow nodes/edges（position 由 layout 后续计算）
 */
export function adaptClassGraph(data: GraphData): { nodes: Node<FlowNodeData>[]; edges: Edge[] } {
  const nodes: Node<FlowNodeData>[] = [];
  const edges: Edge[] = [];
  const apiNodes = data.nodes ?? [];
  const apiEdges = data.edges ?? [];

  apiNodes.forEach((n: ApiNode, i: number) => {
    nodes.push({
      id: n.id ?? `n${i}`,
      type: 'default',
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
    const src = e.parent ?? (e as { source?: string }).source ?? (e as { from?: string }).from;
    const tgt = e.child ?? (e as { target?: string }).target ?? (e as { to?: string }).to;
    if (src && tgt) {
      const id = `e-${src}-${tgt}-${e.relation_type ?? i}`;
      edges.push({
        id,
        source: src,
        target: tgt,
        data: { relation_type: e.relation_type },
      });
    }
  });

  return { nodes, edges };
}
