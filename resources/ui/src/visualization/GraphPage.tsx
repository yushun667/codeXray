/**
 * 图页入口：接收 type + initialData，选 adapter 转 nodes/edges，交给 GraphCore；
 * 接收 graphAppend 时 merge + layout 后更新。与主仓库通过 postMessage 通信。
 */

import React, { useState, useEffect, useCallback, useRef } from 'react';
import type { Node, Edge } from 'reactflow';
import type { GraphType, GraphData } from '../shared/types';
import type { HostToGraphMessage } from '../shared/protocol';
import { getLayoutedElementsDagre } from './dagreLayout';
import { mergeGraph } from './graphMerge';
import type { Node as RFNode } from 'reactflow';
import { GraphCore } from './GraphCore';
import { GraphContextMenu } from './GraphContextMenu';
import { adaptCallGraph } from './adapters/callGraph';
import { adaptClassGraph } from './adapters/classGraph';
import { adaptDataFlow } from './adapters/dataFlow';
import { adaptControlFlow } from './adapters/controlFlow';
import type { FlowNodeData } from './adapters/callGraph';
import { getVscodeApi } from '../shared/vscodeApi';

/** 边数量上限，避免数万条边导致渲染卡死；同 (source,target) 仅保留一条 */
const MAX_EDGES = 2500;

function limitEdges(edges: Edge[]): Edge[] {
  const seen = new Set<string>();
  const out: Edge[] = [];
  for (const e of edges) {
    const key = `${e.source}\t${e.target}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({ ...e, id: out.length ? `e-${e.source}-${e.target}-${out.length}` : e.id });
    if (out.length >= MAX_EDGES) break;
  }
  return out;
}

function adaptAndLayout(
  graphType: GraphType,
  data: GraphData
): { nodes: Node<FlowNodeData>[]; edges: Edge[]; edgesTruncated?: number } {
  let nodes: Node<FlowNodeData>[];
  let edges: Edge[];
  switch (graphType) {
    case 'call_graph':
      ({ nodes, edges } = adaptCallGraph(data));
      break;
    case 'class_graph':
      ({ nodes, edges } = adaptClassGraph(data));
      break;
    case 'data_flow':
      ({ nodes, edges } = adaptDataFlow(data));
      break;
    case 'control_flow':
      ({ nodes, edges } = adaptControlFlow(data));
      break;
    default:
      nodes = [];
      edges = [];
  }
  const originalCount = edges.length;
  edges = limitEdges(edges);
  const edgesTruncated = originalCount > edges.length ? originalCount : undefined;
  if (nodes.length > 0) {
    nodes = getLayoutedElementsDagre(nodes, edges, 'LR');
  }
  return { nodes, edges, edgesTruncated };
}

export function GraphPage() {
  const [graphType, setGraphType] = useState<GraphType>('call_graph');
  const [ready, setReady] = useState(false);
  const [nodes, setNodes] = useState<Node<FlowNodeData>[]>([]);
  const [edges, setEdges] = useState<Edge[]>([]);
  const [contextMenu, setContextMenu] = useState<{
    node: RFNode<FlowNodeData>;
    x: number;
    y: number;
  } | null>(null);

  const [edgesTruncated, setEdgesTruncated] = useState<number | null>(null);
  const nodesRef = useRef<Node<FlowNodeData>[]>([]);
  const edgesRef = useRef<Edge[]>([]);
  nodesRef.current = nodes;
  edgesRef.current = edges;

  const handleNodeDimensions = useCallback((id: string, w: number, h: number) => {
    setNodes((prev) => {
      const withSize = prev.map((n) => (n.id === id ? { ...n, width: w, height: h } : n));
      return getLayoutedElementsDagre(withSize, edgesRef.current);
    });
  }, []);

  const injectOnDimensions = useCallback(
    (nodeList: Node<FlowNodeData>[]) =>
      nodeList.map((n) => ({
        ...n,
        data: { ...n.data, onDimensions: handleNodeDimensions },
      })),
    [handleNodeDimensions]
  );

  const applyData = useCallback(
    (type: GraphType, data: GraphData) => {
      const { nodes: n, edges: e, edgesTruncated: truncated } = adaptAndLayout(type, data);
      setEdges(e);
      setNodes(injectOnDimensions(n));
      setEdgesTruncated(truncated ?? null);
    },
    [injectOnDimensions]
  );

  useEffect(() => {
    if (!getVscodeApi()) {
      setReady(true);
      return;
    }
    const handler = (event: MessageEvent<HostToGraphMessage>) => {
      const m = event.data;
      if (!m || typeof m !== 'object' || !m.action) return;
      if (m.action === 'exportGraph') {
        const currentNodes = nodesRef.current;
        const currentEdges = edgesRef.current;
        const payload = {
          nodes: currentNodes.map((n) => ({
            id: n.id,
            type: n.type,
            data: { label: (n.data as FlowNodeData)?.label },
            position: n.position,
          })),
          edges: currentEdges.map((e) => ({ id: e.id, source: e.source, target: e.target })),
        };
        getVscodeApi()?.postMessage({ action: 'graphExported', payload });
        return;
      }
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
        let appendFlowNodes: Node<FlowNodeData>[];
        let appendFlowEdges: Edge[];
        switch (graphType) {
          case 'call_graph':
            ({ nodes: appendFlowNodes, edges: appendFlowEdges } = adaptCallGraph(appendData));
            break;
          case 'class_graph':
            ({ nodes: appendFlowNodes, edges: appendFlowEdges } = adaptClassGraph(appendData));
            break;
          case 'data_flow':
            ({ nodes: appendFlowNodes, edges: appendFlowEdges } = adaptDataFlow(appendData));
            break;
          case 'control_flow':
            ({ nodes: appendFlowNodes, edges: appendFlowEdges } = adaptControlFlow(appendData));
            break;
          default:
            appendFlowNodes = [];
            appendFlowEdges = [];
        }
        setNodes((cur) => {
          setEdges((curE) => {
            const { nodes: mergedN, edges: mergedE } = mergeGraph(
              cur,
              curE,
              appendFlowNodes,
              appendFlowEdges
            );
            const limitedE = limitEdges(mergedE);
            if (mergedE.length > limitedE.length) setEdgesTruncated(mergedE.length);
            const laid = getLayoutedElementsDagre(mergedN, limitedE, 'LR');
            queueMicrotask(() => {
              setNodes(injectOnDimensions(laid));
            });
            return limitedE;
          });
          return cur;
        });
      }
    };
    window.addEventListener('message', handler);
    const sendReady = () => getVscodeApi()?.postMessage({ action: 'graphReady' });
    sendReady();
    const t1 = setTimeout(sendReady, 200);
    const t2 = setTimeout(sendReady, 600);
    return () => {
      window.removeEventListener('message', handler);
      clearTimeout(t1);
      clearTimeout(t2);
    };
  }, [graphType, applyData]);

  useEffect(() => {
    if (!contextMenu) return;
    const close = () => setContextMenu(null);
    const t = setTimeout(close, 0);
    document.addEventListener('mousedown', close);
    return () => {
      clearTimeout(t);
      document.removeEventListener('mousedown', close);
    };
  }, [contextMenu]);

  if (!ready) {
    return (
      <div
        className="graph-loading"
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
        className="graph-empty"
        style={{
          width: '100%',
          height: '100%',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
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
    <div className="graph-page" style={{ width: '100%', height: '100%', display: 'flex', flexDirection: 'column', background: 'var(--vscode-editor-background, #1e1e1e)' }}>
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
          onNodeContextMenu={(node, ev) => setContextMenu({ node, x: ev.clientX, y: ev.clientY })}
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
        />
      )}
    </div>
  );
}
