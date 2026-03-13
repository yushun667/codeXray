/**
 * 图核心画布：React Flow 容器。
 * - 节点类型：自定义 graphNode（minWidth 200、maxWidth 360）
 * - 支持：节点选中/框选/拖拽移动/删除（Delete/Backspace），缩放平移
 * - panOnDrag 仅中/右键（[1,2]），左键 selectionOnDrag 用于框选
 * - 节点双击时从 node.data 取 definition，postMessage(gotoSymbol) 跳转到定义
 * - 节点删除（Delete/Backspace/框选删除）时自动清理关联边
 * - 边：smoothstep 折线，borderRadius 圆角过渡
 */

import React, { useCallback } from 'react';
import {
  ReactFlow,
  Controls,
  Background,
  BackgroundVariant,
  applyNodeChanges,
  applyEdgeChanges,
  SelectionMode,
  type Node,
  type Edge,
  type NodeChange,
  type EdgeChange,
} from 'reactflow';
import 'reactflow/dist/style.css';
import './graph.css';
import type { GraphToHostMessage } from '../shared/protocol';
import type { FlowNodeData } from './adapters/callGraph';
import { getVscodeApi } from '../shared/vscodeApi';
import { GraphNode } from './GraphNode';

const nodeTypes = { graphNode: GraphNode };

const defaultEdgeOptions = {
  type: 'smoothstep',
  pathOptions: { borderRadius: 8 },
};

function postToHost(msg: GraphToHostMessage): void {
  getVscodeApi()?.postMessage(msg);
}

export interface GraphCoreProps {
  nodes: Node<FlowNodeData>[];
  edges: Edge[];
  setNodes: React.Dispatch<React.SetStateAction<Node<FlowNodeData>[]>>;
  setEdges: React.Dispatch<React.SetStateAction<Edge[]>>;
  onNodeContextMenu?: (node: Node<FlowNodeData>, event: React.MouseEvent) => void;
  /** 框选后右键：传入选中节点 ID 列表和鼠标位置 */
  onSelectionContextMenu?: (selectedNodeIds: string[], event: React.MouseEvent | MouseEvent) => void;
}

export function GraphCore({ nodes, edges, setNodes, setEdges, onNodeContextMenu, onSelectionContextMenu }: GraphCoreProps) {
  // 节点变更：删除节点时同步清理关联边（框选删除 / Delete键 均走此路径）
  const onNodesChange = useCallback(
    (changes: NodeChange[]) => {
      const removedIds = new Set<string>();
      for (const c of changes) {
        if (c.type === 'remove') removedIds.add(c.id);
      }
      if (removedIds.size > 0) {
        setEdges((eds) =>
          eds.filter((e) => !removedIds.has(e.source) && !removedIds.has(e.target))
        );
      }
      setNodes((nds) => applyNodeChanges(changes, nds));
    },
    [setNodes, setEdges]
  );
  const onEdgesChange = useCallback(
    (changes: EdgeChange[]) => setEdges((eds) => applyEdgeChanges(changes, eds)),
    [setEdges]
  );

  // 节点双击：跳转到代码定义位置
  const onNodeDoubleClick = useCallback((_: React.MouseEvent, node: Node<FlowNodeData>) => {
    const def = node.data?.definition;
    if (def?.file != null && def?.line != null) {
      postToHost({
        action: 'gotoSymbol',
        file: def.file,
        line: def.line,
        column: def.column ?? 1,
      });
    }
  }, []);

  // 右键菜单：阻止默认，转发给 GraphPage
  const onNodeContextMenuHandler = useCallback(
    (ev: React.MouseEvent, node: Node<FlowNodeData>) => {
      ev.preventDefault();
      onNodeContextMenu?.(node, ev);
    },
    [onNodeContextMenu]
  );

  // 空白区域右键：若有框选节点则弹出批量菜单，否则仅阻止默认菜单
  const onPaneContextMenu = useCallback((e: React.MouseEvent | MouseEvent) => {
    e.preventDefault();
    const selectedIds = nodes.filter((n) => n.selected).map((n) => n.id);
    if (selectedIds.length > 1 && onSelectionContextMenu) {
      onSelectionContextMenu(selectedIds, e);
    }
  }, [nodes, onSelectionContextMenu]);

  return (
    <div style={{ width: '100%', height: '100%' }}>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        nodeTypes={nodeTypes}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onNodeDoubleClick={onNodeDoubleClick}
        onNodeContextMenu={onNodeContextMenuHandler}
        onPaneContextMenu={onPaneContextMenu}
        defaultEdgeOptions={defaultEdgeOptions}
        // 左键拖拽用于框选节点，中/右键拖拽用于平移画布
        panOnDrag={[1, 2]}
        selectionOnDrag
        selectionMode={SelectionMode.Partial}
        nodesDraggable
        nodesConnectable={false}
        fitView
        fitViewOptions={{ padding: 0.2 }}
        minZoom={0.05}
        maxZoom={2}
        deleteKeyCode={['Delete', 'Backspace']}
        proOptions={{ hideAttribution: true }}
      >
        <Controls />
        <Background variant={BackgroundVariant.Dots} gap={20} size={1} />
      </ReactFlow>
    </div>
  );
}
