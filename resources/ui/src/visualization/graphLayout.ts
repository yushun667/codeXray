/**
 * 双向树状布局：以根节点为中心，前驱在左、后继在右，同层节点对齐；
 * 节点间保留间距（不紧挨），层间距与同层间距均含余量。
 */

import type { Node, Edge } from 'reactflow';
import type { GraphType } from '../shared/types';

const NODE_WIDTH = 300;
/** 单节点占位高度（约 2 行文字 + 内边距；多行时节点实际高度可能更大） */
const NODE_HEIGHT = 72;
/** 同层节点之间的最小间隙（像素），保证不紧挨 */
const NODE_GAP_V = 24;
/** 层与层之间的最小间隙（像素） */
const NODE_GAP_H = 32;
/** 层间距 = 节点宽度 + 水平间隙 */
const RANK_SEP = NODE_WIDTH + NODE_GAP_H;
/** 同层节点垂直间距 = 占位高度 + 垂直间隙 */
const NODE_SEP = NODE_HEIGHT + NODE_GAP_V;

/** 从边列表构建邻接：前驱 = 有边指向该节点的源；后继 = 该节点指向的目标 */
function buildAdjacency(edges: Edge[]) {
  const predecessors: Record<string, string[]> = {};
  const successors: Record<string, string[]> = {};
  for (const e of edges) {
    if (!predecessors[e.target]) predecessors[e.target] = [];
    predecessors[e.target].push(e.source);
    if (!successors[e.source]) successors[e.source] = [];
    successors[e.source].push(e.target);
  }
  return { predecessors, successors };
}

/** 选根：无入边的节点，或入边最少的，否则第一个 */
function pickRoot(nodes: Node[], predecessors: Record<string, string[]>): string {
  const ids = new Set(nodes.map((n) => n.id));
  let best = nodes[0]?.id ?? '';
  let minIn = ids.size;
  for (const n of nodes) {
    const inList = predecessors[n.id]?.filter((id) => ids.has(id)) ?? [];
    if (inList.length < minIn) {
      minIn = inList.length;
      best = n.id;
    }
  }
  return best;
}

/** 双向 BFS 赋层：根=0，前驱层负，后继层正 */
function assignLayers(
  rootId: string,
  nodeIds: Set<string>,
  predecessors: Record<string, string[]>,
  successors: Record<string, string[]>
): Map<string, number> {
  const layer = new Map<string, number>();
  layer.set(rootId, 0);
  const queue: string[] = [rootId];
  let head = 0;
  while (head < queue.length) {
    const n = queue[head++];
    const L = layer.get(n)!;
    const preds = predecessors[n]?.filter((id) => nodeIds.has(id)) ?? [];
    for (const p of preds) {
      if (!layer.has(p)) {
        layer.set(p, L - 1);
        queue.push(p);
      }
    }
    const succs = successors[n]?.filter((id) => nodeIds.has(id)) ?? [];
    for (const s of succs) {
      if (!layer.has(s)) {
        layer.set(s, L + 1);
        queue.push(s);
      }
    }
  }
  return layer;
}

/**
 * 双向树状布局 + 同层对齐：根居中，前驱在左、后继在右，同层节点同一 x、y 等距并整体居中
 */
export function getLayoutedElements<T = Record<string, unknown>>(
  nodes: Node<T>[],
  edges: Edge[],
  _graphType: GraphType = 'call_graph'
): Node<T>[] {
  if (nodes.length === 0) return nodes;

  const nodeIds = new Set(nodes.map((n) => n.id));
  const { predecessors, successors } = buildAdjacency(edges);
  const rootId = pickRoot(nodes, predecessors);
  const layerMap = assignLayers(rootId, nodeIds, predecessors, successors);

  const byLayer = new Map<number, string[]>();
  for (const [id, L] of layerMap) {
    if (!byLayer.has(L)) byLayer.set(L, []);
    byLayer.get(L)!.push(id);
  }
  for (const arr of byLayer.values()) arr.sort();

  const layerXs = new Map<number, number>();
  const minL = Math.min(...byLayer.keys());
  const maxL = Math.max(...byLayer.keys());
  for (let L = minL; L <= maxL; L++) layerXs.set(L, L * RANK_SEP);

  const positions = new Map<string, { x: number; y: number }>();
  for (const [L, ids] of byLayer) {
    const x = layerXs.get(L) ?? 0;
    const totalH = (ids.length - 1) * NODE_SEP;
    const startY = -totalH / 2;
    ids.forEach((id, i) => {
      positions.set(id, { x, y: startY + i * NODE_SEP });
    });
  }

  return nodes.map((node) => {
    const pos = positions.get(node.id);
    return {
      ...node,
      position:
        pos != null
          ? { x: pos.x - NODE_WIDTH / 2, y: pos.y - NODE_HEIGHT / 2 }
          : node.position,
    };
  });
}
