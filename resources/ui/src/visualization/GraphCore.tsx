import React, { useCallback, useState } from 'react';
import {
  ReactFlow,
  Controls,
  Background,
  useNodesState,
  useEdgesState,
  type Node,
  type Edge,
  type NodeMouseHandler,
} from 'reactflow';
import 'reactflow/dist/style.css';
import type { GraphType } from '../shared/types';
import { GraphContextMenu } from './GraphContextMenu';

declare const acquireVsCodeApi: () => { postMessage: (msg: unknown) => void };
const vscode = typeof acquireVsCodeApi !== 'undefined' ? acquireVsCodeApi() : null;

export interface GraphCoreProps {
  initialNodes: Node[];
  initialEdges: Edge[];
  graphType: GraphType | null;
}

export function GraphCore({ initialNodes, initialEdges, graphType }: GraphCoreProps) {
  const [nodes, setNodes, onNodesStateChange] = useNodesState(initialNodes);
  const [edges, setEdges, onEdgesStateChange] = useEdgesState(initialEdges);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; nodeId: string } | null>(null);

  React.useEffect(() => {
    setNodes(initialNodes);
    setEdges(initialEdges);
  }, [initialNodes, initialEdges, setNodes, setEdges]);

  const onNodeClick: NodeMouseHandler = useCallback(
    (_, node) => {
      const def = (node.data as { definition?: { file?: string; line?: number; column?: number } })?.definition;
      if (vscode && def?.file != null) {
        vscode.postMessage({
          action: 'gotoSymbol',
          uri: def.file,
          line: def.line ?? 0,
          column: def.column ?? 0,
        });
      }
    },
    []
  );

  const onNodeContextMenu: NodeMouseHandler = useCallback((event, node) => {
    event.preventDefault();
    setContextMenu({ x: event.clientX, y: event.clientY, nodeId: node.id });
  }, []);

  const onPaneClick = useCallback(() => setContextMenu(null), []);

  return (
    <>
      <div className="graph-core-wrap">
        <ReactFlow
          nodes={nodes}
          edges={edges}
          onNodesChange={onNodesStateChange}
          onEdgesChange={onEdgesStateChange}
          onNodeClick={onNodeClick}
          onNodeContextMenu={onNodeContextMenu}
          onPaneClick={onPaneClick}
          fitView
          nodesDraggable
          nodesConnectable={false}
          elementsSelectable
          panOnDrag
          zoomOnScroll
          zoomOnPinch
          deleteKeyCode={['Backspace', 'Delete']}
        >
          <Controls />
          <Background />
        </ReactFlow>
      </div>
      <GraphContextMenu
        visible={!!contextMenu}
        x={contextMenu?.x ?? 0}
        y={contextMenu?.y ?? 0}
        nodeId={contextMenu?.nodeId ?? null}
        graphType={graphType}
        onClose={() => setContextMenu(null)}
      />
    </>
  );
}
