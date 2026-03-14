/**
 * G6 数据适配器：
 * - toG6Data(): adapter 输出（AdapterNode/AdapterEdge）→ G6 NodeData/EdgeData
 * - mergeG6Data(): 增量合并，按 id 去重（替代旧 graphMerge.ts）
 */

import type { NodeData, EdgeData } from '@antv/g6';
import type { AdapterNode, AdapterEdge } from './adapters/types';
import type { FlowNodeData } from './adapters/callGraph';

/** 同 (source,target) 仅保留一条边，总数上限避免卡顿 */
const MAX_EDGES = 2500;

/**
 * 将 adapter 输出转为 G6 数据格式。
 * position 字段被丢弃——G6 由 layout 引擎计算实际位置。
 *
 * @param nodes adapter 产出的节点
 * @param edges adapter 产出的边
 * @returns G6 格式的 { nodes, edges }
 */
export function toG6Data(
  nodes: AdapterNode<FlowNodeData>[],
  edges: AdapterEdge[]
): { nodes: NodeData[]; edges: EdgeData[] } {
  return {
    nodes: nodes.map((n) => ({
      id: n.id,
      data: { ...n.data },
    })),
    edges: edges.map((e) => ({
      id: e.id,
      source: e.source,
      target: e.target,
      data: e.data ?? {},
    })),
  };
}

/**
 * 增量合并 G6 数据：按 id 去重节点，按 source|target 去重边。
 * 返回合并后的新数组（不修改原数组）。
 *
 * @param current 当前图数据
 * @param append 待追加的增量数据
 * @returns 合并后的 { nodes, edges }
 */
export function mergeG6Data(
  current: { nodes: NodeData[]; edges: EdgeData[] },
  append: { nodes: NodeData[]; edges: EdgeData[] }
): { nodes: NodeData[]; edges: EdgeData[] } {
  const nodeIds = new Set(current.nodes.map((n) => n.id));
  const edgeKeys = new Set(
    current.edges.map((e) => `${e.source}|${e.target}`)
  );

  const newNodes = append.nodes.filter((n) => !nodeIds.has(n.id));
  const newEdges = append.edges.filter((e) => {
    const key = `${e.source}|${e.target}`;
    if (edgeKeys.has(key)) return false;
    edgeKeys.add(key);
    return true;
  });

  return {
    nodes: [...current.nodes, ...newNodes],
    edges: [...current.edges, ...newEdges],
  };
}

/**
 * 对边做同 (source,target) 去重 + 总数上限截断。
 *
 * @param edges 原始边列表
 * @returns { edges: 去重截断后的边, originalCount: 原始数量 }
 */
export function deduplicateAndLimitEdges(
  edges: EdgeData[]
): { edges: EdgeData[]; originalCount: number } {
  const originalCount = edges.length;
  const seen = new Set<string>();
  const out: EdgeData[] = [];
  for (const e of edges) {
    const key = `${e.source}\t${e.target}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(e);
    if (out.length >= MAX_EDGES) break;
  }
  return { edges: out, originalCount };
}
