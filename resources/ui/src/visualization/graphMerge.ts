/**
 * 图数据去重合并：按节点 id、边 (source, target, type?) 去重，只追加新节点与新边
 */

import type { Node, Edge } from 'reactflow';

function edgeKey(e: Edge): string {
  const t = (e.type ?? 'default') as string;
  return `${e.source}|${e.target}|${t}`;
}

/**
 * 将 appendNodes/appendEdges 与当前 nodes/edges 合并，去重后返回新数组
 */
export function mergeGraph<T = Record<string, unknown>>(
  currentNodes: Node<T>[],
  currentEdges: Edge[],
  appendNodes: Node<T>[],
  appendEdges: Edge[]
): { nodes: Node<T>[]; edges: Edge[] } {
  const nodeIds = new Set(currentNodes.map((n) => n.id));
  const edgeKeys = new Set(currentEdges.map(edgeKey));

  const nodes = [...currentNodes];
  for (const n of appendNodes) {
    if (!nodeIds.has(n.id)) {
      nodeIds.add(n.id);
      nodes.push(n);
    }
  }

  const edges = [...currentEdges];
  for (const e of appendEdges) {
    const k = edgeKey(e);
    if (!edgeKeys.has(k)) {
      edgeKeys.add(k);
      edges.push(e);
    }
  }

  return { nodes, edges };
}
