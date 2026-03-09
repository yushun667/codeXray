/**
 * 图页入口：接收 type + initialData，选 adapter 转 nodes/edges，交给 GraphCore；
 * 接收 graphAppend 时 merge + layout 后更新。与主仓库通过 postMessage 通信。
 */

import React, { useState, useEffect, useCallback } from 'react';
import type { Node, Edge } from 'reactflow';
import type { GraphType, GraphData } from '../shared/types';
import type { HostToGraphMessage } from '../shared/protocol';
import { getLayoutedElements } from './graphLayout';
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

function adaptAndLayout(
  graphType: GraphType,
  data: GraphData
): { nodes: Node<FlowNodeData>[]; edges: Edge[] } {
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
  if (nodes.length > 0) {
    nodes = getLayoutedElements(nodes, edges, graphType);
  }
  return { nodes, edges };
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

  const applyData = useCallback((type: GraphType, data: GraphData) => {
    const { nodes: n, edges: e } = adaptAndLayout(type, data);
    setNodes(n);
    setEdges(e);
  }, []);

  useEffect(() => {
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
            const laid = getLayoutedElements(mergedN, mergedE, graphType);
            queueMicrotask(() => {
              setNodes(laid);
            });
            return mergedE;
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
    <div className="graph-page" style={{ width: '100%', height: '100%', background: 'var(--vscode-editor-background, #1e1e1e)' }}>
      <GraphCore
        nodes={nodes}
        edges={edges}
        setNodes={setNodes}
        setEdges={setEdges}
        onNodeContextMenu={(node, ev) => setContextMenu({ node, x: ev.clientX, y: ev.clientY })}
      />
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
