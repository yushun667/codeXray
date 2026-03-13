/**
 * 图历史管理 Hook：基于快照栈的撤销/恢复（Undo/Redo）。
 *
 * - 每次图状态变更前调用 pushSnapshot(当前状态) 保存快照到 undo 栈
 * - undo(当前状态) 将当前推入 redo 栈，弹出 undo 栈顶返回
 * - redo(当前状态) 将当前推入 undo 栈，弹出 redo 栈顶返回
 * - 栈使用 useRef 存储，避免不必要的 re-render
 * - 快照使用 structuredClone 深拷贝，确保历史记录不被后续修改影响
 * - 最大历史记录 MAX_HISTORY=50，超出时丢弃最早的记录
 */

import { useRef, useCallback } from 'react';
import type { Node, Edge } from 'reactflow';
import type { FlowNodeData } from './adapters/callGraph';

/**
 * 图快照：保存某一时刻的完整图状态
 * @property nodes - 所有节点（含 position、data 等）
 * @property edges - 所有边
 * @property rootNodeIds - 查询根节点 ID 集合（用于孤立节点清理）
 */
export interface GraphSnapshot {
  nodes: Node<FlowNodeData>[];
  edges: Edge[];
  rootNodeIds: Set<string>;
}

/** 历史记录上限 */
const MAX_HISTORY = 50;

/**
 * 深拷贝快照：使用 structuredClone 拷贝 nodes/edges，Set 需手动重建
 * @param s - 要拷贝的快照
 * @returns 完全独立的快照副本
 */
function cloneSnapshot(s: GraphSnapshot): GraphSnapshot {
  return {
    nodes: structuredClone(s.nodes),
    edges: structuredClone(s.edges),
    rootNodeIds: new Set(s.rootNodeIds),
  };
}

/**
 * 图历史管理 Hook
 * @returns pushSnapshot - 变更前保存当前状态
 * @returns undo - 撤销：返回上一个状态快照，或 null（无可撤销）
 * @returns redo - 恢复：返回下一个状态快照，或 null（无可恢复）
 * @returns clearHistory - 重置历史栈（initGraph 时调用）
 */
export function useGraphHistory() {
  /** 撤销栈：从底到顶为从旧到新 */
  const undoStack = useRef<GraphSnapshot[]>([]);
  /** 恢复栈：从底到顶为从旧到新 */
  const redoStack = useRef<GraphSnapshot[]>([]);

  /**
   * 在变更前调用：将当前状态推入 undo 栈，清空 redo 栈
   * @param current - 当前图状态（变更前）
   */
  const pushSnapshot = useCallback((current: GraphSnapshot) => {
    undoStack.current.push(cloneSnapshot(current));
    if (undoStack.current.length > MAX_HISTORY) {
      undoStack.current.shift();
    }
    // 新操作清空 redo 栈（分支历史被丢弃）
    redoStack.current = [];
  }, []);

  /**
   * 撤销：将当前状态推入 redo 栈，弹出 undo 栈顶返回
   * @param current - 当前图状态
   * @returns 恢复到的快照，或 null（undo 栈为空）
   */
  const undo = useCallback((current: GraphSnapshot): GraphSnapshot | null => {
    if (undoStack.current.length === 0) return null;
    redoStack.current.push(cloneSnapshot(current));
    return undoStack.current.pop()!;
  }, []);

  /**
   * 恢复：将当前状态推入 undo 栈，弹出 redo 栈顶返回
   * @param current - 当前图状态
   * @returns 恢复到的快照，或 null（redo 栈为空）
   */
  const redo = useCallback((current: GraphSnapshot): GraphSnapshot | null => {
    if (redoStack.current.length === 0) return null;
    undoStack.current.push(cloneSnapshot(current));
    return redoStack.current.pop()!;
  }, []);

  /**
   * 重置历史栈：initGraph 初始化时调用，清空所有历史
   */
  const clearHistory = useCallback(() => {
    undoStack.current = [];
    redoStack.current = [];
  }, []);

  return { pushSnapshot, undo, redo, clearHistory };
}
