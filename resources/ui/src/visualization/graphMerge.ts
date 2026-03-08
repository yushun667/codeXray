import type { Node, Edge } from 'reactflow';

function edgeKey(e: Edge): string {
  return `${e.source}-${e.target}-${(e as Edge & { data?: { edge_type?: string } }).data?.edge_type ?? ''}`;
}

export function mergeGraph(
  currentNodes: Node[],
  currentEdges: Edge[],
  appendNodes: Node[],
  appendEdges: Edge[]
): { nodes: Node[]; edges: Edge[] } {
  const nodeIds = new Set(currentNodes.map((n) => n.id));
  const edgeKeys = new Set(currentEdges.map(edgeKey));

  const nodes = [...currentNodes];
  appendNodes.forEach((n) => {
    if (!nodeIds.has(n.id)) {
      nodeIds.add(n.id);
      nodes.push(n);
    }
  });

  const edges = [...currentEdges];
  appendEdges.forEach((e) => {
    const k = edgeKey(e);
    if (!edgeKeys.has(k)) {
      edgeKeys.add(k);
      edges.push(e);
    }
  });

  return { nodes, edges };
}
