/**
 * 数据流图：将解析引擎 data_flow 查询结果转为 React Flow nodes/edges
 */

import type { Node, Edge } from 'reactflow';
import type { GraphData, GraphNode as ApiNode, GraphEdge as ApiEdge } from '../../shared/types';
import type { FlowNodeData } from './callGraph';
import { nodeLabel } from './callGraph';

/**
 * data_flow 数据 -> React Flow nodes/edges（position 由 layout 后续计算）
 */
export function adaptDataFlow(data: GraphData): { nodes: Node<FlowNodeData>[]; edges: Edge[] } {
  const nodes: Node<FlowNodeData>[] = [];
  const edges: Edge[] = [];
  const apiNodes = data.nodes ?? [];
  const apiEdges = data.edges ?? [];

  apiNodes.forEach((n: ApiNode, i: number) => {
    nodes.push({
      id: n.id ?? `n${i}`,
      type: 'customNode',
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
    const src = e.var ?? (e as { source?: string }).source ?? (e as { writer?: string }).writer;
    const tgt = e.reader ?? (e as { target?: string }).target ?? (e as { writer?: string }).writer;
    if (src && tgt) {
      const id = `e-${src}-${tgt}-${i}`;
      edges.push({
        id,
        source: String(src),
        target: String(tgt),
        data: {},
      });
    }
  });

  return { nodes, edges };
}
