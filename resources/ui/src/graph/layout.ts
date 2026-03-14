import type { GraphNode, GraphEdge, LayoutNode, LayoutConfig } from '../types';

// ── Default constants ──
export const DEFAULT_NODE_WIDTH = 200;
export const DEFAULT_NODE_HEIGHT = 60;
export const DEFAULT_RANK_SEP = 160;
export const DEFAULT_NODE_SEP = 35;
export const DEFAULT_PADDING = 60;

const DEFAULT_CONFIG: LayoutConfig = {
  rankSep: DEFAULT_RANK_SEP,
  nodeSep: DEFAULT_NODE_SEP,
  nodeWidth: DEFAULT_NODE_WIDTH,
  nodeHeight: DEFAULT_NODE_HEIGHT,
  padding: DEFAULT_PADDING,
};

/** Extract filename from a file path (last component, no directory) */
function extractFilename(filePath: string): string {
  const sep = filePath.lastIndexOf('/');
  const bsep = filePath.lastIndexOf('\\');
  const lastSep = Math.max(sep, bsep);
  return lastSep >= 0 ? filePath.substring(lastSep + 1) : filePath;
}

/**
 * Build display label for a node:
 *   qualified_name (filename)
 * Falls back to name if qualified_name is absent.
 * Appends filename from definition.file or node.file.
 */
export function buildDisplayLabel(node: GraphNode): string {
  const funcName = (node as any).qualified_name as string
    || node.name
    || String(node.id);
  const filePath = node.definition?.file ?? node.file;
  const line = node.definition?.line ?? node.line;
  if (filePath) {
    const filename = extractFilename(filePath);
    const loc = line ? `${filename}:${line}` : filename;
    return `${funcName}\n${loc}`;
  }
  return funcName;
}

/** Calculate node width based on display label length */
export function calcNodeWidth(label: string): number {
  // Use the longest line for width calculation
  const lines = label.split('\n');
  const maxLen = Math.max(...lines.map((l) => l.length));
  return Math.max(200, Math.min(maxLen * 7.5 + 32, 400));
}

/** Calculate node dimensions using display label */
export function calcNodeDimensions(node: GraphNode): { width: number; height: number } {
  const label = buildDisplayLabel(node);
  return { width: calcNodeWidth(label), height: DEFAULT_NODE_HEIGHT };
}

/**
 * Compute bidirectional tree layout.
 * - Callers go left (negative ranks)
 * - Root is rank 0
 * - Callees go right (positive ranks)
 */
export function computeBidirectionalLayout(
  nodes: GraphNode[],
  edges: GraphEdge[],
  rootId: string,
  config?: Partial<LayoutConfig>,
): LayoutNode[] {
  const cfg = { ...DEFAULT_CONFIG, ...config };

  if (nodes.length === 0) return [];

  // Build adjacency maps
  const nodeMap = new Map<string, GraphNode>();
  for (const n of nodes) nodeMap.set(n.id, n);

  // callers[id] = nodes that call id (predecessors)
  // callees[id] = nodes that id calls (successors)
  const callers = new Map<string, string[]>();
  const callees = new Map<string, string[]>();

  for (const e of edges) {
    const src = e.caller || e.source || '';
    const tgt = e.callee || e.target || '';
    if (!src || !tgt) continue;
    if (!callees.has(src)) callees.set(src, []);
    callees.get(src)!.push(tgt);
    if (!callers.has(tgt)) callers.set(tgt, []);
    callers.get(tgt)!.push(src);
  }

  // BFS to assign ranks
  const rankMap = new Map<string, number>();
  const visited = new Set<string>();

  // Root at rank 0
  if (!nodeMap.has(rootId) && nodes.length > 0) {
    // Fallback: use first node as root
    rankMap.set(nodes[0].id, 0);
    visited.add(nodes[0].id);
  } else {
    rankMap.set(rootId, 0);
    visited.add(rootId);
  }

  const effectiveRootId = nodeMap.has(rootId) ? rootId : nodes[0].id;

  // BFS left: callers (negative ranks)
  const leftQueue: string[] = [effectiveRootId];
  while (leftQueue.length > 0) {
    const current = leftQueue.shift()!;
    const currentRank = rankMap.get(current)!;
    const preds = callers.get(current) ?? [];
    for (const pred of preds) {
      if (!visited.has(pred) && nodeMap.has(pred)) {
        const newRank = currentRank - 1;
        rankMap.set(pred, newRank);
        visited.add(pred);
        leftQueue.push(pred);
      }
    }
  }

  // BFS right: callees (positive ranks)
  const rightQueue: string[] = [effectiveRootId];
  while (rightQueue.length > 0) {
    const current = rightQueue.shift()!;
    const currentRank = rankMap.get(current)!;
    const succs = callees.get(current) ?? [];
    for (const succ of succs) {
      if (!visited.has(succ) && nodeMap.has(succ)) {
        const newRank = currentRank + 1;
        rankMap.set(succ, newRank);
        visited.add(succ);
        rightQueue.push(succ);
      }
    }
  }

  // Assign unvisited nodes to max rank + 1
  let maxRank = 0;
  for (const r of rankMap.values()) {
    if (Math.abs(r) > maxRank) maxRank = Math.abs(r);
  }
  for (const n of nodes) {
    if (!visited.has(n.id)) {
      rankMap.set(n.id, maxRank + 1);
      visited.add(n.id);
    }
  }

  // Group by rank
  const rankGroups = new Map<number, GraphNode[]>();
  for (const n of nodes) {
    const rank = rankMap.get(n.id) ?? 0;
    if (!rankGroups.has(rank)) rankGroups.set(rank, []);
    rankGroups.get(rank)!.push(n);
  }

  // Compute positions — use per-rank X that accounts for actual widths
  const layoutNodes: LayoutNode[] = [];
  const ranks = Array.from(rankGroups.keys()).sort((a, b) => a - b);

  // First pass: compute max width per rank
  const rankMaxWidth = new Map<number, number>();
  for (const rank of ranks) {
    const group = rankGroups.get(rank)!;
    let maxW = 0;
    for (const node of group) {
      const dims = calcNodeDimensions(node);
      if (dims.width > maxW) maxW = dims.width;
    }
    rankMaxWidth.set(rank, maxW);
  }

  // Compute X positions: accumulate from center outward
  const rankX = new Map<number, number>();
  // Root rank (0) at x=0
  rankX.set(0, 0);
  // Positive ranks (right)
  for (let i = 0; i < ranks.length; i++) {
    const r = ranks[i];
    if (r <= 0) continue;
    const prevRank = r - 1;
    const prevX = rankX.get(prevRank) ?? 0;
    const prevW = rankMaxWidth.get(prevRank) ?? cfg.nodeWidth;
    const currW = rankMaxWidth.get(r) ?? cfg.nodeWidth;
    rankX.set(r, prevX + prevW / 2 + cfg.rankSep + currW / 2);
  }
  // Negative ranks (left)
  for (let i = ranks.length - 1; i >= 0; i--) {
    const r = ranks[i];
    if (r >= 0) continue;
    const nextRank = r + 1;
    const nextX = rankX.get(nextRank) ?? 0;
    const nextW = rankMaxWidth.get(nextRank) ?? cfg.nodeWidth;
    const currW = rankMaxWidth.get(r) ?? cfg.nodeWidth;
    rankX.set(r, nextX - nextW / 2 - cfg.rankSep - currW / 2);
  }

  for (const rank of ranks) {
    const group = rankGroups.get(rank)!;
    const totalHeight = group.reduce((sum, n) => {
      return sum + calcNodeDimensions(n).height + cfg.nodeSep;
    }, -cfg.nodeSep);

    let y = -totalHeight / 2;
    const cx = rankX.get(rank) ?? 0;

    for (const node of group) {
      const dims = calcNodeDimensions(node);
      const x = cx - dims.width / 2;
      layoutNodes.push({
        id: node.id,
        rank,
        x,
        y,
        width: dims.width,
        height: dims.height,
        raw: node,
      });
      y += dims.height + cfg.nodeSep;
    }
  }

  return layoutNodes;
}

/**
 * Resolve collisions within same-rank groups.
 * Iteratively push overlapping nodes apart vertically.
 */
export function resolveCollisions(nodes: LayoutNode[], maxIter: number = 30): void {
  if (nodes.length < 2) return;

  // Group by rank
  const groups = new Map<number, LayoutNode[]>();
  for (const n of nodes) {
    if (!groups.has(n.rank)) groups.set(n.rank, []);
    groups.get(n.rank)!.push(n);
  }

  for (let iter = 0; iter < maxIter; iter++) {
    let anyOverlap = false;

    for (const group of groups.values()) {
      if (group.length < 2) continue;

      // Sort by Y
      group.sort((a, b) => a.y - b.y);

      for (let i = 0; i < group.length - 1; i++) {
        const a = group[i];
        const b = group[i + 1];
        const minDist = (a.height + b.height) / 2 + DEFAULT_NODE_SEP;
        const centerA = a.y + a.height / 2;
        const centerB = b.y + b.height / 2;
        const actualDist = centerB - centerA;

        if (actualDist < minDist) {
          const push = (minDist - actualDist) / 2;
          a.y -= push;
          b.y += push;
          anyOverlap = true;
        }
      }
    }

    if (!anyOverlap) break;
  }
}
