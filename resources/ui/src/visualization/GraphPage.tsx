/**
 * 图页入口：接收 type + initialData，选 adapter 转 nodes/edges，交给 GraphCore；
 * 接收 graphAppend 时 merge + layout 后更新。与主仓库通过 postMessage 通信。
 *
 * 占位状态：
 * - !ready → 「加载图中…」
 * - ready && nodes.length === 0 → 「查询结果为空。…」
 * - edges 超过 MAX_EDGES → 顶部提示横幅
 */

import React, { useState, useEffect, useCallback, useRef } from 'react';
import type { Node, Edge } from 'reactflow';
import type { GraphType, GraphData } from '../shared/types';
import type { HostToGraphMessage } from '../shared/protocol';
import { getLayoutedElements } from './graphLayout';
import { mergeGraph } from './graphMerge';
import type { Node as RFNode } from 'reactflow';
import { GraphCore } from './GraphCore';
import { GraphContextMenu, SelectionContextMenu } from './GraphContextMenu';
import { adaptCallGraph } from './adapters/callGraph';
import { adaptClassGraph } from './adapters/classGraph';
import { adaptDataFlow } from './adapters/dataFlow';
import { adaptControlFlow } from './adapters/controlFlow';
import type { FlowNodeData } from './adapters/callGraph';
import { getVscodeApi } from '../shared/vscodeApi';

/** 同 (source,target) 仅保留一条边，总数上限避免卡顿 */
const MAX_EDGES = 2500;

function deduplicateAndLimitEdges(edges: Edge[]): { edges: Edge[]; originalCount: number } {
  const originalCount = edges.length;
  const seen = new Set<string>();
  const out: Edge[] = [];
  for (const e of edges) {
    const key = `${e.source}\t${e.target}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(e);
    if (out.length >= MAX_EDGES) break;
  }
  return { edges: out, originalCount };
}

function adaptGraph(
  graphType: GraphType,
  data: GraphData
): { nodes: Node<FlowNodeData>[]; edges: Edge[] } {
  switch (graphType) {
    case 'call_graph':   return adaptCallGraph(data);
    case 'class_graph':  return adaptClassGraph(data);
    case 'data_flow':    return adaptDataFlow(data);
    case 'control_flow': return adaptControlFlow(data);
    default:             return { nodes: [], edges: [] };
  }
}

function adaptAndLayout(
  graphType: GraphType,
  data: GraphData
): { nodes: Node<FlowNodeData>[]; edges: Edge[]; edgesTruncated?: number } {
  let { nodes, edges } = adaptGraph(graphType, data);
  const { edges: limitedEdges, originalCount } = deduplicateAndLimitEdges(edges);
  edges = limitedEdges;
  const edgesTruncated = originalCount > edges.length ? originalCount : undefined;
  if (nodes.length > 0) {
    nodes = getLayoutedElements(nodes, edges, graphType);
  }
  return { nodes, edges, edgesTruncated };
}

/**
 * 删除指定节点后清理孤立节点：
 * 1. 移除 removeIds 中的节点及其关联边
 * 2. 在剩余节点/边上做连通分量分析（无向图 BFS）
 * 3. 保留包含 rootNodeIds 中任一节点的连通分量，移除其他孤立分量
 *
 * @param nodes 当前所有节点
 * @param edges 当前所有边
 * @param removeIds 要删除的节点 ID 集合
 * @param rootNodeIds 查询根节点 ID 集合（不可被孤立清理删除）
 */
function removeNodesAndOrphans(
  nodes: Node<FlowNodeData>[],
  edges: Edge[],
  removeIds: Set<string>,
  rootNodeIds: Set<string>
): { nodes: Node<FlowNodeData>[]; edges: Edge[] } {
  // Step 1: 移除指定节点和关联边
  const remainingNodes = nodes.filter((n) => !removeIds.has(n.id));
  const remainingEdges = edges.filter(
    (e) => !removeIds.has(e.source) && !removeIds.has(e.target)
  );

  if (remainingNodes.length === 0) return { nodes: [], edges: [] };

  // Step 2: 构建无向邻接表
  const adj = new Map<string, Set<string>>();
  for (const n of remainingNodes) adj.set(n.id, new Set());
  for (const e of remainingEdges) {
    adj.get(e.source)?.add(e.target);
    adj.get(e.target)?.add(e.source);
  }

  // Step 3: BFS 找所有连通分量
  const visited = new Set<string>();
  const components: Set<string>[] = [];
  for (const n of remainingNodes) {
    if (visited.has(n.id)) continue;
    const comp = new Set<string>();
    const queue = [n.id];
    visited.add(n.id);
    while (queue.length > 0) {
      const cur = queue.shift()!;
      comp.add(cur);
      for (const nb of adj.get(cur) ?? []) {
        if (!visited.has(nb)) {
          visited.add(nb);
          queue.push(nb);
        }
      }
    }
    components.push(comp);
  }

  // Step 4: 保留包含任一 root 节点的分量；若无 root 则保留最大分量
  const keepIds = new Set<string>();
  let hasRootComp = false;
  for (const comp of components) {
    for (const rid of rootNodeIds) {
      if (comp.has(rid)) {
        for (const id of comp) keepIds.add(id);
        hasRootComp = true;
        break;
      }
    }
  }
  if (!hasRootComp) {
    // 回退：保留最大连通分量
    let largest = components[0];
    for (const comp of components) {
      if (comp.size > largest.size) largest = comp;
    }
    for (const id of largest) keepIds.add(id);
  }

  return {
    nodes: remainingNodes.filter((n) => keepIds.has(n.id)),
    edges: remainingEdges.filter(
      (e) => keepIds.has(e.source) && keepIds.has(e.target)
    ),
  };
}

export function GraphPage() {
  const [graphType, setGraphType] = useState<GraphType>('call_graph');
  const [ready, setReady] = useState(false);
  const [nodes, setNodes] = useState<Node<FlowNodeData>[]>([]);
  const [edges, setEdges] = useState<Edge[]>([]);
  const [edgesTruncated, setEdgesTruncated] = useState<number | null>(null);
  const [contextMenu, setContextMenu] = useState<{
    node: RFNode<FlowNodeData>;
    x: number;
    y: number;
  } | null>(null);
  const [selectionMenu, setSelectionMenu] = useState<{
    selectedNodeIds: string[];
    x: number;
    y: number;
  } | null>(null);

  /** 查询根节点 ID 集合：首次 initGraph 时记录，用于孤立节点清理 */
  const rootNodeIdsRef = useRef<Set<string>>(new Set());

  const applyData = useCallback((type: GraphType, data: GraphData) => {
    const { nodes: n, edges: e, edgesTruncated: trunc } = adaptAndLayout(type, data);
    setNodes(n);
    setEdges(e);
    setEdgesTruncated(trunc ?? null);
  }, []);

  /**
   * 统一的节点删除函数：删除指定节点 + 清理关联边 + 移除孤立节点
   * 供右键单删、右键批量删、键盘删除三条路径共用
   */
  const deleteNodes = useCallback((idsToRemove: Set<string>) => {
    setNodes((curNodes) => {
      setEdges((curEdges) => {
        const result = removeNodesAndOrphans(
          curNodes, curEdges, idsToRemove, rootNodeIdsRef.current
        );
        // 用 queueMicrotask 避免 setState 嵌套问题
        queueMicrotask(() => setNodes(result.nodes));
        return result.edges;
      });
      return curNodes; // 由 queueMicrotask 中的 setNodes 更新
    });
  }, [setNodes, setEdges]);

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
        if (m.nodes?.length || m.edges?.length) {
          applyData(type, { nodes: m.nodes ?? [], edges: m.edges ?? [] });
          // 记录初始节点 ID 作为查询根节点（首次设置；后续 graphAppend 不覆盖）
          if (rootNodeIdsRef.current.size === 0) {
            const adapted = adaptGraph(type, { nodes: m.nodes ?? [], edges: m.edges ?? [] });
            for (const n of adapted.nodes) rootNodeIdsRef.current.add(n.id);
          }
        }
        setReady(true);
      }

      if (m.action === 'graphAppend' && (m.nodes?.length || m.edges?.length)) {
        const appendData: GraphData = { nodes: m.nodes ?? [], edges: m.edges ?? [] };
        setGraphType((curType) => {
          const { nodes: appendN, edges: appendE } = adaptGraph(curType, appendData);
          setNodes((curNodes) => {
            setEdges((curEdges) => {
              const { nodes: mergedN, edges: mergedE } = mergeGraph(
                curNodes,
                curEdges,
                appendN,
                appendE
              );
              const { edges: limitedE, originalCount } = deduplicateAndLimitEdges(mergedE);
              if (originalCount > limitedE.length) setEdgesTruncated(originalCount);
              const laid = getLayoutedElements(mergedN, limitedE, curType);
              queueMicrotask(() => setNodes(laid));
              return limitedE;
            });
            return curNodes;
          });
          return curType;
        });
      }
    };

    window.addEventListener('message', handler);

    // 通知主仓库 Webview 已就绪，可发送 initGraph
    const sendReady = () => getVscodeApi()?.postMessage({ action: 'graphReady' });
    sendReady();
    const t1 = setTimeout(sendReady, 200);
    const t2 = setTimeout(sendReady, 600);

    return () => {
      window.removeEventListener('message', handler);
      clearTimeout(t1);
      clearTimeout(t2);
    };
  }, [applyData]);

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

  if (nodes.length === 0) {
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
      {edgesTruncated != null && (
        <div
          style={{
            flexShrink: 0,
            padding: '4px 10px',
            fontSize: 12,
            color: 'var(--vscode-descriptionForeground, #858585)',
            background: 'var(--vscode-editor-inactiveSelectionBackground, #2a2a2a)',
          }}
        >
          边数量较多，已按节点对去重并仅展示前 {MAX_EDGES} 条（原始约 {edgesTruncated} 条），以保持流畅。
        </div>
      )}
      <div style={{ flex: 1, minHeight: 0 }}>
        <GraphCore
          nodes={nodes}
          edges={edges}
          setNodes={setNodes}
          setEdges={setEdges}
          onNodeContextMenu={(node, ev) =>
            setContextMenu({ node, x: ev.clientX, y: ev.clientY })
          }
          onSelectionContextMenu={(selectedNodes, ev) => {
            setContextMenu(null);
            setSelectionMenu({
              selectedNodeIds: selectedNodes.map((n) => n.id),
              x: ev.clientX,
              y: ev.clientY,
            });
          }}
          onNodesDeleted={(removedIds) => {
            // 键盘删除后，GraphCore 已移除节点和关联边，
            // 这里只需做孤立节点清理（使用 requestAnimationFrame 等待状态更新后执行）
            requestAnimationFrame(() => {
              setNodes((curNodes) => {
                setEdges((curEdges) => {
                  const result = removeNodesAndOrphans(
                    curNodes, curEdges, new Set(), rootNodeIdsRef.current
                  );
                  if (result.nodes.length < curNodes.length) {
                    queueMicrotask(() => setNodes(result.nodes));
                    return result.edges;
                  }
                  return curEdges;
                });
                return curNodes;
              });
            });
          }}
        />
      </div>
      {contextMenu && (
        <GraphContextMenu
          graphType={graphType}
          nodeId={contextMenu.node.id}
          nodeData={contextMenu.node.data as FlowNodeData}
          x={contextMenu.x}
          y={contextMenu.y}
          onClose={() => setContextMenu(null)}
          onDeleteNode={(nid) => deleteNodes(new Set([nid]))}
        />
      )}
      {selectionMenu && (
        <SelectionContextMenu
          selectedNodeIds={selectionMenu.selectedNodeIds}
          x={selectionMenu.x}
          y={selectionMenu.y}
          onClose={() => setSelectionMenu(null)}
          onDeleteSelected={(ids) => deleteNodes(new Set(ids))}
        />
      )}
    </div>
  );
}
