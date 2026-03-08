import type { Node, Edge } from 'reactflow';
import type { GraphData, GraphNode, GraphEdge, GraphType } from '../shared/types';

function buildEdgesByType(
  rawEdges: GraphEdge[],
  graphType: GraphType | undefined
): Edge[] {
  const result: Edge[] = [];
  if (graphType === 'class_graph') {
    rawEdges.forEach((e, i) => {
      const parent = e.parent;
      const child = e.child;
      if (parent && child) {
        result.push({
          id: `e${i}-${parent}-${child}`,
          source: parent,
          target: child,
          data: { edge_type: e.relation_type ?? e.edge_type },
        });
      }
    });
    return result;
  }
  if (graphType === 'control_flow') {
    rawEdges.forEach((e, i) => {
      const from = e.from;
      const to = e.to;
      if (from && to) {
        result.push({
          id: `e${i}-${from}-${to}`,
          source: from,
          target: to,
          data: { edge_type: e.edge_type },
        });
      }
    });
    return result;
  }
  if (graphType === 'data_flow') {
    let idx = 0;
    rawEdges.forEach((e) => {
      const varId = e.var;
      if (!varId) return;
      if (e.reader) {
        result.push({
          id: `e${idx++}-${varId}-${e.reader}`,
          source: varId,
          target: e.reader,
          data: { edge_type: 'read' },
        });
      }
      if (e.writer) {
        result.push({
          id: `e${idx++}-${varId}-${e.writer}`,
          source: varId,
          target: e.writer,
          data: { edge_type: 'write' },
        });
      }
    });
    return result;
  }
  // call_graph or unknown: caller/callee or source/target
  rawEdges.forEach((e, i) => {
    const source = e.caller ?? e.source ?? '';
    const target = e.callee ?? e.target ?? '';
    if (source && target) {
      result.push({
        id: `e${i}-${source}-${target}`,
        source,
        target,
        data: { edge_type: e.edge_type },
      });
    }
  });
  return result;
}

export function graphDataToReactFlow(
  data: GraphData,
  graphType?: GraphType
): { nodes: Node[]; edges: Edge[] } {
  const rawNodes = (data.nodes ?? []) as GraphNode[];
  const rawEdges = (data.edges ?? []) as GraphEdge[];

  const nodes: Node[] = rawNodes.map((n) => ({
    id: n.id,
    type: 'default',
    position: { x: 0, y: 0 },
    data: {
      label: n.name ?? (n as { block_id?: string }).block_id ?? n.id,
      definition: n.definition,
      definition_range: n.definition_range,
      ...n,
    },
  }));

  const edges = buildEdgesByType(rawEdges, graphType);

  return { nodes, edges };
}
