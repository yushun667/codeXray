/**
 * G6 核心 React 组件：管理 AntV G6 Graph 实例的生命周期。
 *
 * - 通过 useImperativeHandle 暴露 setData/appendData/fitView/relayout/undo/redo 给父组件
 * - 注册 G6 事件：node:dblclick（跳转定义）、node:contextmenu（右键菜单）、
 *   node:click（路径高亮）、canvas:click（清除高亮）、canvas:contextmenu（框选菜单）
 * - 键盘快捷键：Ctrl+Z 撤销、Ctrl+Shift+Z / Ctrl+Y 恢复
 * - 右下角工具栏：适应画布、重新布局
 */

import React, {
  useRef,
  useEffect,
  useImperativeHandle,
  forwardRef,
  useCallback,
} from 'react';
import { Graph, type NodeData, type EdgeData } from '@antv/g6';
import type { FlowNodeData } from './adapters/callGraph';
import type { GraphToHostMessage } from '../shared/protocol';
import { getVscodeApi } from '../shared/vscodeApi';
import { createGraphOptions, NODE_WIDTH, NODE_HEIGHT, NODE_SEP, RANK_SEP } from './g6Config';
import { mergeG6Data, deduplicateAndLimitEdges } from './g6Adapter';
import { registerBidirectionalDagre } from './g6Layout';
import './g6.css';

/** 向 VSCode 主仓库发送 postMessage */
function postToHost(msg: GraphToHostMessage): void {
  getVscodeApi()?.postMessage(msg);
}

// ─── 路径高亮工具函数 ───

/**
 * BFS 在无向图上找从 startId 到 endId 的最短路径
 * @returns 路径上的节点 ID 有序数组，或 null（不可达）
 */
function bfsShortestPath(
  nodes: NodeData[],
  edges: EdgeData[],
  startId: string,
  endId: string
): string[] | null {
  if (startId === endId) return [startId];

  // 构建无向邻接表
  const adj = new Map<string, Set<string>>();
  for (const n of nodes) adj.set(String(n.id), new Set());
  for (const e of edges) {
    const s = String(e.source);
    const t = String(e.target);
    adj.get(s)?.add(t);
    adj.get(t)?.add(s);
  }

  // BFS
  const visited = new Set<string>([startId]);
  const parent = new Map<string, string>();
  const queue = [startId];

  while (queue.length > 0) {
    const cur = queue.shift()!;
    for (const nb of adj.get(cur) ?? []) {
      if (visited.has(nb)) continue;
      visited.add(nb);
      parent.set(nb, cur);
      if (nb === endId) {
        // 回溯路径
        const path: string[] = [endId];
        let p = endId;
        while (parent.has(p)) {
          p = parent.get(p)!;
          path.unshift(p);
        }
        return path;
      }
      queue.push(nb);
    }
  }
  return null;
}

/**
 * 高亮从 rootId 到 targetId 的路径，其余节点/边设为 dimmed
 */
function highlightPath(graph: Graph, rootId: string, targetId: string): void {
  clearHighlight(graph);

  const allNodes = graph.getNodeData();
  const allEdges = graph.getEdgeData();
  const path = bfsShortestPath(allNodes, allEdges, rootId, targetId);
  if (!path || path.length === 0) return;

  const pathNodeSet = new Set(path);

  // 找出路径上的边
  const pathEdgeIds = new Set<string>();
  for (let i = 0; i < path.length - 1; i++) {
    const from = path[i];
    const to = path[i + 1];
    for (const e of allEdges) {
      const eId = String(e.id ?? `${e.source}-${e.target}`);
      const eSrc = String(e.source);
      const eTgt = String(e.target);
      if ((eSrc === from && eTgt === to) || (eSrc === to && eTgt === from)) {
        pathEdgeIds.add(eId);
      }
    }
  }

  // 批量设置状态
  const stateMap: Record<string, string | string[]> = {};
  for (const n of allNodes) {
    const nid = String(n.id);
    stateMap[nid] = pathNodeSet.has(nid) ? 'highlight' : 'dimmed';
  }
  for (const e of allEdges) {
    const eid = String(e.id ?? `${e.source}-${e.target}`);
    stateMap[eid] = pathEdgeIds.has(eid) ? 'highlight' : 'dimmed';
  }
  graph.setElementState(stateMap);
}

/**
 * 清除所有高亮/暗化状态
 */
function clearHighlight(graph: Graph): void {
  const stateMap: Record<string, string[]> = {};
  for (const n of graph.getNodeData()) {
    stateMap[String(n.id)] = [];
  }
  for (const e of graph.getEdgeData()) {
    const eid = String(e.id ?? `${e.source}-${e.target}`);
    stateMap[eid] = [];
  }
  graph.setElementState(stateMap);
}

// ─── 组件 Props 和 Handle ───

export interface G6GraphProps {
  /** 节点右键菜单回调 */
  onNodeContextMenu?: (
    nodeId: string,
    nodeData: FlowNodeData,
    x: number,
    y: number
  ) => void;
  /** 框选后右键菜单回调 */
  onSelectionContextMenu?: (
    selectedNodeIds: string[],
    x: number,
    y: number
  ) => void;
  /** 节点数量变化回调（用于外部判断空/非空状态） */
  onNodeCountChange?: (count: number) => void;
}

export interface G6GraphHandle {
  /** 初始化图数据（initGraph 时调用） */
  setData(
    data: { nodes: NodeData[]; edges: EdgeData[] },
    rootIds: Set<string>
  ): void;
  /** 增量追加数据（graphAppend 时调用） */
  appendData(
    data: { nodes: NodeData[]; edges: EdgeData[] },
    rootIds: Set<string>
  ): void;
  /** 适应画布 */
  fitView(): void;
  /** 重新布局 */
  relayout(): void;
  /** 删除单个节点 */
  removeNode(id: string): void;
  /** 批量删除节点 */
  removeNodes(ids: string[]): void;
}

/**
 * G6 核心组件：包裹 AntV G6 Graph 实例，通过 ref handle 暴露操作接口
 */
export const G6Graph = forwardRef<G6GraphHandle, G6GraphProps>(function G6Graph(
  { onNodeContextMenu, onSelectionContextMenu, onNodeCountChange },
  ref
) {
  const containerRef = useRef<HTMLDivElement>(null);
  const graphRef = useRef<Graph | null>(null);
  /** 当前查询根节点 ID（用于路径高亮） */
  const rootIdRef = useRef<string | null>(null);
  /** 当前查询根节点集合（用于布局） */
  const rootIdsRef = useRef<Set<string>>(new Set());

  // ─── 创建 / 销毁 Graph 实例 ───
  useEffect(() => {
    if (!containerRef.current) return;

    // 注册自定义布局
    registerBidirectionalDagre();

    const graph = new Graph(createGraphOptions(containerRef.current));
    graphRef.current = graph;

    // 初始化渲染（空图）
    graph.render();

    // ─── 事件绑定 ───

    // 节点双击 → 跳转到代码定义位置
    graph.on('node:dblclick', (event: any) => {
      const nodeId = String(event.target?.id ?? event.targetId ?? '');
      if (!nodeId) return;
      const nodeData = graph.getNodeData(nodeId);
      const def = nodeData?.data?.definition as
        | { file: string; line: number; column?: number }
        | undefined;
      if (def?.file && def?.line != null) {
        postToHost({
          action: 'gotoSymbol',
          file: def.file,
          line: def.line,
          column: def.column ?? 1,
        });
      }
    });

    // 节点单击 → 路径高亮
    graph.on('node:click', (event: any) => {
      const nodeId = String(event.target?.id ?? event.targetId ?? '');
      if (!nodeId || !rootIdRef.current) return;
      if (nodeId === rootIdRef.current) {
        clearHighlight(graph);
      } else {
        highlightPath(graph, rootIdRef.current, nodeId);
      }
    });

    // 画布单击 → 清除高亮
    graph.on('canvas:click', () => {
      clearHighlight(graph);
    });

    // 节点右键菜单
    graph.on('node:contextmenu', (event: any) => {
      const nodeId = String(event.target?.id ?? event.targetId ?? '');
      if (!nodeId) return;
      const nodeData = graph.getNodeData(nodeId);
      const flowData = (nodeData?.data ?? {}) as FlowNodeData;
      // 获取鼠标坐标
      const clientX = event.client?.x ?? event.originalEvent?.clientX ?? 0;
      const clientY = event.client?.y ?? event.originalEvent?.clientY ?? 0;
      onNodeContextMenu?.(nodeId, flowData, clientX, clientY);
    });

    // 画布右键（框选后的右键菜单）
    graph.on('canvas:contextmenu', (event: any) => {
      const selected = graph.getElementDataByState('node', 'selected');
      if (selected.length > 1) {
        const clientX = event.client?.x ?? event.originalEvent?.clientX ?? 0;
        const clientY = event.client?.y ?? event.originalEvent?.clientY ?? 0;
        onSelectionContextMenu?.(
          selected.map((n: NodeData) => String(n.id)),
          clientX,
          clientY
        );
      }
    });

    return () => {
      graph.destroy();
      graphRef.current = null;
    };
  }, []); // 只在 mount/unmount 时执行

  // ─── 键盘快捷键 ───
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const mod = e.metaKey || e.ctrlKey;
      if (!mod) return;
      const graph = graphRef.current;
      if (!graph) return;

      const history = graph.getPluginInstance<any>('history');
      if (!history) return;

      // Ctrl+Shift+Z 或 Ctrl+Y → 恢复
      if ((e.key === 'z' || e.key === 'Z') && e.shiftKey) {
        e.preventDefault();
        if (history.canRedo?.()) history.redo?.();
        return;
      }
      if (e.key === 'y') {
        e.preventDefault();
        if (history.canRedo?.()) history.redo?.();
        return;
      }
      // Ctrl+Z → 撤销
      if (e.key === 'z' && !e.shiftKey) {
        e.preventDefault();
        if (history.canUndo?.()) history.undo?.();
        return;
      }
    };
    document.addEventListener('keydown', handler);
    return () => document.removeEventListener('keydown', handler);
  }, []);

  // ─── 通知外部节点数量变化 ───
  const notifyNodeCount = useCallback(() => {
    const graph = graphRef.current;
    if (!graph || !onNodeCountChange) return;
    try {
      const count = graph.getNodeData().length;
      onNodeCountChange(count);
    } catch {
      // graph 可能还没渲染完成
    }
  }, [onNodeCountChange]);

  // ─── 暴露 Handle ───
  useImperativeHandle(
    ref,
    () => ({
      setData(data, rootIds) {
        const graph = graphRef.current;
        if (!graph) return;

        rootIdsRef.current = rootIds;
        // 取第一个 root ID 用于路径高亮
        rootIdRef.current = rootIds.size > 0 ? [...rootIds][0] : null;

        // 去重 + 限流边
        const { edges: limitedEdges } = deduplicateAndLimitEdges(data.edges);

        // 设置数据 + 使用自定义布局
        graph.setData({
          nodes: data.nodes,
          edges: limitedEdges,
        });

        // 使用自定义双向布局
        graph.setLayout({
          type: 'bidirectional-dagre' as any,
          rootIds,
          nodesep: NODE_SEP,
          ranksep: RANK_SEP,
        });

        graph.render().then(() => {
          graph.fitView();
          notifyNodeCount();
        });
      },

      appendData(appendData, rootIds) {
        const graph = graphRef.current;
        if (!graph) return;

        rootIdsRef.current = rootIds;

        // 获取当前数据
        const currentNodes = graph.getNodeData();
        const currentEdges = graph.getEdgeData();

        // 合并
        const merged = mergeG6Data(
          { nodes: currentNodes, edges: currentEdges },
          appendData
        );

        // 去重 + 限流
        const { edges: limitedEdges } = deduplicateAndLimitEdges(merged.edges);

        // 重新设置数据并布局
        graph.setData({
          nodes: merged.nodes,
          edges: limitedEdges,
        });

        graph.setLayout({
          type: 'bidirectional-dagre' as any,
          rootIds,
          nodesep: NODE_SEP,
          ranksep: RANK_SEP,
        });

        graph.render().then(() => {
          graph.fitView();
          notifyNodeCount();
        });
      },

      fitView() {
        graphRef.current?.fitView();
      },

      relayout() {
        const graph = graphRef.current;
        if (!graph) return;
        graph.layout().then(() => {
          graph.fitView();
        });
      },

      removeNode(id) {
        const graph = graphRef.current;
        if (!graph) return;
        // 先删除关联边
        const edges = graph.getEdgeData();
        const relatedEdgeIds = edges
          .filter((e) => String(e.source) === id || String(e.target) === id)
          .map((e) => String(e.id ?? `${e.source}-${e.target}`));
        if (relatedEdgeIds.length > 0) {
          graph.removeEdgeData(relatedEdgeIds);
        }
        graph.removeNodeData([id]);
        graph.draw().then(() => notifyNodeCount());
      },

      removeNodes(ids) {
        const graph = graphRef.current;
        if (!graph) return;
        const idSet = new Set(ids);
        // 先删除关联边
        const edges = graph.getEdgeData();
        const relatedEdgeIds = edges
          .filter(
            (e) => idSet.has(String(e.source)) || idSet.has(String(e.target))
          )
          .map((e) => String(e.id ?? `${e.source}-${e.target}`));
        if (relatedEdgeIds.length > 0) {
          graph.removeEdgeData(relatedEdgeIds);
        }
        graph.removeNodeData(ids);
        graph.draw().then(() => notifyNodeCount());
      },
    }),
    [notifyNodeCount]
  );

  return (
    <div style={{ width: '100%', height: '100%', position: 'relative' }}>
      <div ref={containerRef} className="g6-container" />
      <div className="g6-toolbar">
        <button
          title="适应画布"
          onClick={() => graphRef.current?.fitView()}
        >
          ⊞
        </button>
        <button
          title="重新布局"
          onClick={() => {
            const g = graphRef.current;
            if (g) g.layout().then(() => g.fitView());
          }}
        >
          ⟳
        </button>
      </div>
    </div>
  );
});
