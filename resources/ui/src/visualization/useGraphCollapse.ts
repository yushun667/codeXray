/**
 * 图节点折叠/展开状态：维护已展开节点 id 集合，提供 toggle；
 * 可根据 expandedSet 过滤 nodes/edges 或标记 node.data.expanded。
 * 首版仅提供状态与 toggle，暂不接入 GraphCore 显隐过滤（见 doc/占位功能记录.md）。
 */

import { useState, useCallback } from 'react';
import type { Node, Edge } from 'reactflow';

export function useGraphCollapse<T>(initialNodes: Node<T>[], _initialEdges: Edge[]) {
  const [expandedSet, setExpandedSet] = useState<Set<string>>(() => new Set(
    initialNodes.map((n) => n.id)
  ));

  const toggle = useCallback((nodeId: string) => {
    setExpandedSet((prev) => {
      const next = new Set(prev);
      if (next.has(nodeId)) next.delete(nodeId);
      else next.add(nodeId);
      return next;
    });
  }, []);

  const expandAll = useCallback((nodeIds: string[]) => {
    setExpandedSet((prev) => new Set([...prev, ...nodeIds]));
  }, []);

  const collapseAll = useCallback(() => {
    setExpandedSet(new Set());
  }, []);

  return {
    expandedSet,
    toggle,
    expandAll,
    collapseAll,
    filteredNodes: initialNodes,
    filteredEdges: _initialEdges,
  };
}
