/**
 * 图页入口：接收 type + initialData，选 adapter 转 nodes/edges，交给 GraphCore；
 * 接收 graphAppend 时 merge + layout 后更新。与主仓库通过 postMessage 通信。
 *
 * 占位状态：
 * - !ready → 「加载图中…」
 * - ready && nodes.length === 0 → 「查询结果为空。…」
 * - edges 超过 MAX_EDGES → 顶部提示横幅
 */

import React, { useState, useEffect, useCallback } from 'react';
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

  const applyData = useCallback((type: GraphType, data: GraphData) => {
    const { nodes: n, edges: e, edgesTruncated: trunc } = adaptAndLayout(type, data);
    setNodes(n);
    setEdges(e);
    setEdgesTruncated(trunc ?? null);
  }, []);

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
          onDeleteNode={(nid) => {
            setNodes((nds) => nds.filter((n) => n.id !== nid));
            setEdges((eds) => eds.filter((e) => e.source !== nid && e.target !== nid));
          }}
        />
      )}
      {selectionMenu && (
        <SelectionContextMenu
          selectedNodeIds={selectionMenu.selectedNodeIds}
          x={selectionMenu.x}
          y={selectionMenu.y}
          onClose={() => setSelectionMenu(null)}
          onDeleteSelected={(ids) => {
            const idSet = new Set(ids);
            setNodes((nds) => nds.filter((n) => !idSet.has(n.id)));
            setEdges((eds) => eds.filter((e) => !idSet.has(e.source) && !idSet.has(e.target)));
          }}
        />
      )}
    </div>
  );
}
