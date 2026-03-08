import dagre from '@dagrejs/dagre';
import graphlib from '@dagrejs/graphlib';

const Graph = graphlib.Graph;
const layout = dagre.layout;
import type { Node, Edge } from 'reactflow';

export function getLayoutedElements(nodes: Node[], edges: Edge[]): Node[] {
  const g = new Graph({ compound: true });
  g.setDefaultEdgeLabel(() => ({}));
  g.setGraph({ rankdir: 'TB', ranksep: 60, nodesep: 40 });

  nodes.forEach((node) => {
    g.setNode(node.id, { width: 160, height: 40 });
  });
  edges.forEach((edge) => {
    g.setEdge(edge.source, edge.target);
  });

  layout(g);

  return nodes.map((node) => {
    const n = g.node(node.id) as { x: number; y: number; width: number; height: number };
    if (!n) return node;
    return {
      ...node,
      position: { x: n.x - n.width / 2, y: n.y - n.height / 2 },
    };
  });
}
