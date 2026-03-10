/**
 * 图核心画布：React Flow 容器，节点选中/框选/拖拽/删除，缩放平移，节点点击 postMessage(gotoSymbol)
 */

import React, { useCallback } from 'react';
import {
  ReactFlow,
  Controls,
  Background,
  Panel,
  applyNodeChanges,
  applyEdgeChanges,
  type Node,
  type Edge,
  type NodeMouseHandler,
  type NodeChange,
  type EdgeChange,
} from 'reactflow';
import 'reactflow/dist/style.css';
import './graph.css';
import type { GraphToHostMessage } from '../shared/protocol';
import type { FlowNodeData } from './adapters/callGraph';
import { getVscodeApi } from '../shared/vscodeApi';
import { CustomNode } from './CustomNode';

function postToHost(msg: GraphToHostMessage): void {
  getVscodeApi()?.postMessage(msg);
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
    (event: React.MouseEvent, node: Node<FlowNodeData>) => {
      event.preventDefault();
      event.stopPropagation();
      onNodeContextMenu?.(node, event);
    },
    [onNodeContextMenu]
  );

  const onPaneContextMenu = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
  }, []);

  const onDeleteSelected = useCallback(() => {
    setNodes((nds) => nds.filter((n) => !n.selected));
    setEdges((eds) => eds.filter((e) => !e.selected));
  }, [setNodes, setEdges]);

  return (
    <div style={{ width: '100%', height: '100%', background: 'var(--vscode-editor-background, #1e1e1e)' }}>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        nodeTypes={{ customNode: CustomNode }}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onNodeClick={onNodeClick}
        onNodeContextMenu={onNodeContextMenuHandler}
        onPaneContextMenu={onPaneContextMenu}
        defaultEdgeOptions={
          {
            type: 'smoothstep',
            pathOptions: { borderRadius: 14 },
            animated: true,
          } as import('reactflow').DefaultEdgeOptions
        }
        nodesDraggable
        elementsSelectable
        panOnDrag={[1, 2]}
        selectionOnDrag
        deleteKeyCode={['Backspace', 'Delete']}
        zoomOnScroll
        zoomOnPinch
        fitView
        fitViewOptions={{ padding: 0.2 }}
        minZoom={0.1}
        maxZoom={2}
      >
        <Controls />
        <Background />
        <Panel position="top-right">
          <button
            type="button"
            onClick={onDeleteSelected}
            style={{
              padding: '4px 10px',
              fontSize: 12,
              cursor: 'pointer',
              background: 'var(--vscode-button-background)',
              color: 'var(--vscode-button-foreground)',
              border: '1px solid var(--vscode-button-border)',
              borderRadius: 4,
            }}
          >
            删除选中
          </button>
        </Panel>
      </ReactFlow>
    </div>
  );
}
