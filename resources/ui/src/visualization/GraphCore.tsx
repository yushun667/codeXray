/**
 * 图核心画布：React Flow 容器，节点选中/框选/拖拽/删除，缩放平移，节点点击 postMessage(gotoSymbol)
 */

import React, { useCallback } from 'react';
import {
  ReactFlow,
  Controls,
  Background,
  applyNodeChanges,
  applyEdgeChanges,
  type Node,
  type Edge,
  type NodeMouseHandler,
  type NodeChange,
  type EdgeChange,
} from 'reactflow';
import 'reactflow/dist/style.css';
import type { GraphToHostMessage } from '../shared/protocol';
import type { FlowNodeData } from './adapters/callGraph';

declare const acquireVsCodeApi: () => { postMessage: (msg: unknown) => void };

const vscode = typeof acquireVsCodeApi !== 'undefined' ? acquireVsCodeApi() : null;

function postToHost(msg: GraphToHostMessage): void {
  vscode?.postMessage(msg);
}

export interface GraphCoreProps {
  nodes: Node<FlowNodeData>[];
  edges: Edge[];
  setNodes: React.Dispatch<React.SetStateAction<Node<FlowNodeData>[]>>;
  setEdges: React.Dispatch<React.SetStateAction<Edge[]>>;
  onNodeContextMenu?: (node: Node<FlowNodeData>, event: React.MouseEvent) => void;
}

export function GraphCore({ nodes, edges, setNodes, setEdges, onNodeContextMenu }: GraphCoreProps) {
  const onNodesChange = useCallback(
    (changes: NodeChange[]) => setNodes((nds) => applyNodeChanges(changes, nds)),
    [setNodes]
  );
  const onEdgesChange = useCallback(
    (changes: EdgeChange[]) => setEdges((eds) => applyEdgeChanges(changes, eds)),
    [setEdges]
  );

  const onNodeClick: NodeMouseHandler = useCallback((_, node) => {
    const d = node.data as FlowNodeData;
    const def = d.definition;
    if (def?.file != null && def?.line != null) {
      postToHost({
        action: 'gotoSymbol',
        file: def.file,
        line: def.line,
        column: def.column ?? 1,
      });
    }
  }, []);

  const onNodeContextMenuHandler: NodeMouseHandler = useCallback(
    (_, node, event) => {
      event.preventDefault();
      onNodeContextMenu?.(node, event as unknown as React.MouseEvent);
    },
    [onNodeContextMenu]
  );

  return (
    <div style={{ width: '100%', height: '100%' }}>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onNodeClick={onNodeClick}
        onNodeContextMenu={onNodeContextMenuHandler}
        nodesSelectable
        nodesDraggable
        elementsSelectable
        panOnDrag
        zoomOnScroll
        zoomOnPinch
        fitView
        fitViewOptions={{ padding: 0.2 }}
        minZoom={0.1}
        maxZoom={2}
      >
        <Controls />
        <Background />
      </ReactFlow>
    </div>
  );
}
