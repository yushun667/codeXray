/**
 * 类关系图：将解析引擎 class_graph 查询结果转为通用 nodes/edges
 */

import type { AdapterNode, AdapterEdge } from './types';
import type { GraphData, GraphNode as ApiNode, GraphEdge as ApiEdge } from '../../shared/types';
import type { FlowNodeData } from './callGraph';
import { nodeLabel } from './callGraph';

/**
 * class_graph 数据 -> 通用 nodes/edges（position 由布局引擎后续计算）
 */
export function adaptClassGraph(data: GraphData): { nodes: AdapterNode<FlowNodeData>[]; edges: AdapterEdge[] } {
  const nodes: AdapterNode<FlowNodeData>[] = [];
  const edges: AdapterEdge[] = [];
  const apiNodes = data.nodes ?? [];
  const apiEdges = data.edges ?? [];

  apiNodes.forEach((n: ApiNode, i: number) => {
    nodes.push({
      id: String(n.id ?? `n${i}`),
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
    const src = String(e.parent ?? (e as { source?: string }).source ?? (e as { from?: string }).from ?? '');
    const tgt = String(e.child ?? (e as { target?: string }).target ?? (e as { to?: string }).to ?? '');
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
