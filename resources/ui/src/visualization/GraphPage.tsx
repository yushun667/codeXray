/**
 * 图页入口组件
 *
 * 接收 host 的 initGraph / graphAppend 消息，
 * 通过 G6Graph ref 控制图实例；管理右键菜单状态。
 *
 * 状态展示：
 *   !ready                → 「加载图中…」
 *   ready && nodeCount===0 → 「查询结果为空。…」
 *   edges 超过上限         → 顶部提示横幅
 */

import React, { useState, useEffect, useCallback, useRef } from 'react';
import type { GraphType, GraphData } from '../shared/types';
import type { HostToGraphMessage } from '../shared/protocol';
import type { FlowNodeData } from './adapters/callGraph';
import type { AdapterNode, AdapterEdge } from './adapters/types';
import { adaptCallGraph } from './adapters/callGraph';
import { adaptClassGraph } from './adapters/classGraph';
import { adaptDataFlow } from './adapters/dataFlow';
import { adaptControlFlow } from './adapters/controlFlow';
import { toG6Data, deduplicateAndLimitEdges } from './g6Adapter';
import { G6Graph, type G6GraphHandle } from './G6Graph';
import { GraphContextMenu, SelectionContextMenu } from './GraphContextMenu';
import { getVscodeApi } from '../shared/vscodeApi';

/** 同 (source,target) 边的总数上限 */
const MAX_EDGES = 2500;

/**
 * 根据图类型选择对应 adapter 转换数据
 *
 * @param graphType - 图类型（call_graph / class_graph / data_flow / control_flow）
 * @param data      - 原始 API 数据
 * @returns adapter 输出的通用节点/边
 */
function adaptGraph(
  graphType: GraphType,
  data: GraphData,
): { nodes: AdapterNode<FlowNodeData>[]; edges: AdapterEdge[] } {
  switch (graphType) {
    case 'call_graph':
      return adaptCallGraph(data);
    case 'class_graph':
      return adaptClassGraph(data);
    case 'data_flow':
      return adaptDataFlow(data);
    case 'control_flow':
      return adaptControlFlow(data);
    default:
      return { nodes: [], edges: [] };
  }
}

/**
 * 识别查询入口节点（根节点）
 *
 * 匹配策略：
 *   1) querySymbol name 精确匹配
 *   2) label 首行匹配
 *   3) 拓扑分析（度数最高的节点）
 *
 * @param nodes       - adapter 输出的节点
 * @param edges       - adapter 输出的边
 * @param querySymbol - 查询入口符号名
 * @returns 查询根节点 ID 集合
 */
function identifyRootNodes(
  nodes: AdapterNode<FlowNodeData>[],
  edges: AdapterEdge[],
  querySymbol?: string,
): Set<string> {
  const rootIds = new Set<string>();
  let found = false;

  // 策略 1：name 精确匹配
  if (querySymbol) {
    for (const nd of nodes) {
      if (nd.data.name === querySymbol) {
        rootIds.add(nd.id);
        nd.data.isRoot = true;
        found = true;
        break;
      }
    }

    // 策略 2：label 首行匹配
    if (!found) {
      for (const nd of nodes) {
        const firstLine = nd.data.label.split('\n')[0] ?? '';
        if (
          firstLine === querySymbol ||
          firstLine.endsWith('::' + querySymbol)
        ) {
          rootIds.add(nd.id);
          nd.data.isRoot = true;
          found = true;
          break;
        }
      }
    }
  }

  // 策略 3：度数最高的节点
  if (!found && nodes.length > 0 && edges.length > 0) {
    const degree = new Map<string, number>();
    for (const nd of nodes) degree.set(nd.id, 0);
    for (const edge of edges) {
      degree.set(edge.source, (degree.get(edge.source) ?? 0) + 1);
      degree.set(edge.target, (degree.get(edge.target) ?? 0) + 1);
    }
    let maxDeg = 0;
    let maxId = nodes[0].id;
    for (const [id, deg] of degree) {
      if (deg > maxDeg) {
        maxDeg = deg;
        maxId = id;
      }
    }
    rootIds.add(maxId);
    const rootNd = nodes.find((nd) => nd.id === maxId);
    if (rootNd) rootNd.data.isRoot = true;
  }

  return rootIds;
}

/**
 * 图页面主组件
 *
 * 负责：消息监听、数据适配、G6Graph 控制、右键菜单渲染
 */
export function GraphPage() {
  const [graphType, setGraphType] = useState<GraphType>('call_graph');
  const [ready, setReady] = useState(false);
  const [nodeCount, setNodeCount] = useState(0);
  const [edgesTruncated, setEdgesTruncated] = useState<number | null>(null);

  // 右键菜单状态
  const [contextMenu, setContextMenu] = useState<{
    nodeId: string;
    nodeData: FlowNodeData;
    x: number;
    y: number;
  } | null>(null);
  const [selectionMenu, setSelectionMenu] = useState<{
    selectedNodeIds: string[];
    x: number;
    y: number;
  } | null>(null);

  /** G6Graph 组件 ref */
  const g6Ref = useRef<G6GraphHandle>(null);
  /** 查询根节点 ID 集合（ref 版本避免闭包过时） */
  const queryRootIdsRef = useRef<Set<string>>(new Set());
  /** 当前图类型（ref 版本避免 useEffect 闭包过时） */
  const graphTypeRef = useRef<GraphType>('call_graph');
  /** 缓存待初始化的图数据（G6Graph 尚未挂载时暂存） */
  const pendingDataRef = useRef<{
    data: { nodes: import('@antv/g6').NodeData[]; edges: import('@antv/g6').EdgeData[] };
    rootIds: Set<string>;
  } | null>(null);

  // ─── 消息处理 ───
  useEffect(() => {
    // 非 VSCode 环境（开发调试）：直接标记 ready
    if (!getVscodeApi()) {
      setReady(true);
      return;
    }

    const handler = (event: MessageEvent<HostToGraphMessage>) => {
      const m = event.data;
      if (!m || typeof m !== 'object' || !m.action) return;

      if (m.action === 'initGraph') {
        const type = (m.graphType as GraphType) ?? 'call_graph';
        setGraphType(type);
        graphTypeRef.current = type;

        if (m.nodes?.length || m.edges?.length) {
          // 1. 适配数据
          const { nodes: rawN, edges: rawE } = adaptGraph(type, {
            nodes: m.nodes ?? [],
            edges: m.edges ?? [],
          });

          // 2. 识别根节点
          const qSym = (m as { querySymbol?: string }).querySymbol;
          const rootIds = identifyRootNodes(rawN, rawE, qSym);
          queryRootIdsRef.current = rootIds;

          // 3. 转为 G6 格式
          const g6Data = toG6Data(rawN, rawE);

          // 4. 检查边截断
          const { edges: limitedE, truncated, originalCount } =
            deduplicateAndLimitEdges(g6Data.edges);
          setEdgesTruncated(truncated ? originalCount : null);

          // 5. 同步更新节点数量（避免异步 render 完成前显示"空结果"）
          setNodeCount(g6Data.nodes.length);

          // 6. 初始化图（如果 G6Graph 已挂载则直接设置，否则缓存等挂载后应用）
          const initPayload = {
            data: { nodes: g6Data.nodes, edges: limitedE },
            rootIds,
          };
          if (g6Ref.current) {
            g6Ref.current.setData(initPayload.data, initPayload.rootIds);
            pendingDataRef.current = null;
          } else {
            pendingDataRef.current = initPayload;
          }
        }
        setReady(true);
      }

      if (
        m.action === 'graphAppend' &&
        (m.nodes?.length || m.edges?.length)
      ) {
        const curType = graphTypeRef.current;
        const { nodes: appendN, edges: appendE } = adaptGraph(curType, {
          nodes: m.nodes ?? [],
          edges: m.edges ?? [],
        });

        const g6AppendData = toG6Data(appendN, appendE);
        g6Ref.current?.appendData(
          g6AppendData,
          queryRootIdsRef.current,
        );
      }
    };

    window.addEventListener('message', handler);

    // 通知主仓库 Webview 已就绪，可发送 initGraph
    const sendReady = () =>
      getVscodeApi()?.postMessage({ action: 'graphReady' });
    sendReady();
    const t1 = setTimeout(sendReady, 200);
    const t2 = setTimeout(sendReady, 600);

    return () => {
      window.removeEventListener('message', handler);
      clearTimeout(t1);
      clearTimeout(t2);
    };
  }, []);

  // 点击任意位置关闭右键菜单
  useEffect(() => {
    if (!contextMenu && !selectionMenu) return;
    const close = () => {
      setContextMenu(null);
      setSelectionMenu(null);
    };
    document.addEventListener('mousedown', close);
    return () => document.removeEventListener('mousedown', close);
  }, [contextMenu, selectionMenu]);

  /** 节点数量变化回调 */
  const handleNodeCountChange = useCallback((count: number) => {
    setNodeCount(count);
  }, []);

  /** G6Graph 挂载完成回调：如果有缓存的待初始化数据，立即应用 */
  const handleG6Ready = useCallback(() => {
    const pending = pendingDataRef.current;
    if (pending && g6Ref.current) {
      g6Ref.current.setData(pending.data, pending.rootIds);
      pendingDataRef.current = null;
    }
  }, []);

  // ─── 渲染 ───

  // 加载中状态
  if (!ready) {
    return (
      <div
        style={{
          padding: 16,
          fontFamily: 'var(--vscode-font-family, monospace)',
          background: 'var(--vscode-editor-background, #1e1e1e)',
          color: 'var(--vscode-editor-foreground, #d4d4d4)',
          minHeight: '100%',
        }}
      >
        加载图中…
      </div>
    );
  }

  // 空结果状态
  if (ready && nodeCount === 0) {
    return (
      <div
        style={{
          width: '100%',
          height: '100%',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          textAlign: 'center',
          padding: '0 24px',
          boxSizing: 'border-box',
          background: 'var(--vscode-editor-background, #1e1e1e)',
          color: 'var(--vscode-descriptionForeground, #858585)',
          fontFamily: 'var(--vscode-font-family, monospace)',
          fontSize: 'var(--vscode-font-size, 13px)',
        }}
      >
        查询结果为空。请先解析工程，或在 C/C++ 文件中选中符号后右键「查看调用链」等。
      </div>
    );
  }

  // 正常图展示
  return (
    <div
      style={{
        width: '100%',
        height: '100%',
        display: 'flex',
        flexDirection: 'column',
        background: 'var(--vscode-editor-background, #1e1e1e)',
      }}
    >
      {/* 边数截断提示 */}
      {edgesTruncated != null && (
        <div
          style={{
            flexShrink: 0,
            padding: '4px 10px',
            fontSize: 12,
            color: 'var(--vscode-descriptionForeground, #858585)',
            background:
              'var(--vscode-editor-inactiveSelectionBackground, #2a2a2a)',
          }}
        >
          边数量较多，已按节点对去重并仅展示前 {MAX_EDGES} 条（原始约{' '}
          {edgesTruncated} 条），以保持流畅。
        </div>
      )}

      {/* G6 图画布 */}
      <div style={{ flex: 1, minHeight: 0 }}>
        <G6Graph
          ref={g6Ref}
          onG6Ready={handleG6Ready}
          onNodeContextMenu={(nodeId, nodeData, x, y) =>
            setContextMenu({ nodeId, nodeData, x, y })
          }
          onSelectionContextMenu={(selectedNodeIds, x, y) => {
            setContextMenu(null);
            setSelectionMenu({ selectedNodeIds, x, y });
          }}
          onNodeCountChange={handleNodeCountChange}
        />
      </div>

      {/* 单节点右键菜单 */}
      {contextMenu && (
        <GraphContextMenu
          graphType={graphType}
          nodeId={contextMenu.nodeId}
          nodeData={contextMenu.nodeData}
          x={contextMenu.x}
          y={contextMenu.y}
          onClose={() => setContextMenu(null)}
          onDeleteNode={(nid) => {
            g6Ref.current?.removeNode(nid);
            setContextMenu(null);
          }}
        />
      )}

      {/* 框选右键菜单 */}
      {selectionMenu && (
        <SelectionContextMenu
          selectedNodeIds={selectionMenu.selectedNodeIds}
          x={selectionMenu.x}
          y={selectionMenu.y}
          onClose={() => setSelectionMenu(null)}
          onDeleteSelected={(ids) => {
            g6Ref.current?.removeNodes(ids);
            setSelectionMenu(null);
          }}
        />
      )}
    </div>
  );
}
