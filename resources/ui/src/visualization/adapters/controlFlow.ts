/**
 * 控制流图：将解析引擎 control_flow 查询结果转为通用 nodes/edges
 */

import type { AdapterNode, AdapterEdge } from './types';
import type { GraphData, GraphNode as ApiNode, GraphEdge as ApiEdge } from '../../shared/types';
import type { FlowNodeData } from './callGraph';
import { nodeLabel } from './callGraph';

/**
 * control_flow 数据 -> 通用 nodes/edges（position 由布局引擎后续计算）
 */
export function adaptControlFlow(data: GraphData): { nodes: AdapterNode<FlowNodeData>[]; edges: AdapterEdge[] } {
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
    const src = String((e as { from?: string }).from ?? (e as { source?: string }).source ?? '');
    const tgt = String((e as { to?: string }).to ?? (e as { target?: string }).target ?? '');
    if (src && tgt) {
      const id = `e-${src}-${tgt}-${i}`;
      edges.push({
        id,
        source: src,
        target: tgt,
        data: {},
      });
    }
  });

  return { nodes, edges };
}
