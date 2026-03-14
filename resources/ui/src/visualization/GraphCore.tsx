/**
 * 图核心画布：React Flow 容器。
 * - 节点类型：自定义 graphNode（minWidth 200、maxWidth 360）
 * - 支持：节点选中/框选/拖拽移动/删除（Delete/Backspace），缩放平移
 * - panOnDrag 仅中/右键（[1,2]），左键 selectionOnDrag 用于框选
 * - 节点双击时从 node.data 取 definition，postMessage(gotoSymbol) 跳转到定义
 * - 节点删除（Delete/Backspace/框选删除）时自动清理关联边
 * - 边：smoothstep 折线，borderRadius 圆角过渡
 * - 键盘快捷键：Ctrl/Cmd+Z 撤销，Ctrl/Cmd+Shift+Z 或 Ctrl/Cmd+Y 恢复
 * - 节点拖拽开始时触发 onBeforeDrag 回调保存快照
 */

import React, { useCallback, useEffect } from 'react';
import {
  ReactFlow,
  Controls,
  Background,
  BackgroundVariant,
  MarkerType,
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
import { pushOverlapping } from './graphLayout';

const nodeTypes = { graphNode: GraphNode };

const defaultEdgeOptions = {
  type: 'smoothstep',
  pathOptions: { borderRadius: 8 },
  markerEnd: {
    type: MarkerType.ArrowClosed,
    width: 16,
    height: 16,
    color: 'rgba(255, 255, 255, 0.55)',
  },
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
  /** 框选后右键：传入选中节点列表和鼠标事件 */
  onSelectionContextMenu?: (selectedNodes: Node<FlowNodeData>[], event: React.MouseEvent) => void;
  /** 键盘删除节点后回调（传入被删除的节点 ID 集合），由 GraphPage 做孤立节点清理 */
  onNodesDeleted?: (removedIds: Set<string>) => void;
  /** Ctrl/Cmd+Z 撤销回调 */
  onUndo?: () => void;
  /** Ctrl/Cmd+Shift+Z 或 Ctrl/Cmd+Y 恢复回调 */
  onRedo?: () => void;
  /** 节点拖拽开始前回调（用于保存快照） */
  onBeforeDrag?: () => void;
}

export function GraphCore({ nodes, edges, setNodes, setEdges, onNodeContextMenu, onSelectionContextMenu, onNodesDeleted, onUndo, onRedo, onBeforeDrag }: GraphCoreProps) {
  // 键盘快捷键：Ctrl/Cmd+Z 撤销，Ctrl/Cmd+Shift+Z 或 Ctrl/Cmd+Y 恢复
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const mod = e.metaKey || e.ctrlKey;
      if (!mod) return;
      // Ctrl+Shift+Z 或 Ctrl+Y → 恢复（Shift+Z 优先判断，避免被 Z 吞掉）
      if ((e.key === 'z' || e.key === 'Z') && e.shiftKey) {
        e.preventDefault();
        onRedo?.();
        return;
      }
      if (e.key === 'y') {
        e.preventDefault();
        onRedo?.();
        return;
      }
      // Ctrl+Z → 撤销
      if (e.key === 'z' && !e.shiftKey) {
        e.preventDefault();
        onUndo?.();
        return;
      }
    };
    document.addEventListener('keydown', handler);
    return () => document.removeEventListener('keydown', handler);
  }, [onUndo, onRedo]);

  // 节点拖拽开始：保存拖拽前快照
  const onNodeDragStart = useCallback(() => {
    onBeforeDrag?.();
  }, [onBeforeDrag]);

  // 节点拖拽中：实时碰撞检测，推开被覆盖的节点
  const onNodeDrag = useCallback(
    (_: React.MouseEvent, draggedNode: Node<FlowNodeData>) => {
      setNodes((curNodes) => {
        // 先把被拖拽节点的最新位置更新到列表中
        const updated = curNodes.map((n) =>
          n.id === draggedNode.id ? { ...n, position: draggedNode.position } : n
        );
        const pushed = pushOverlapping(draggedNode.id, updated);
        return pushed ?? updated;
      });
    },
    [setNodes]
  );

  // 节点变更：删除节点时先由 React Flow 处理，再通知 GraphPage 做孤立清理
  const onNodesChange = useCallback(
    (changes: NodeChange[]) => {
      const removedIds = new Set<string>();
      for (const c of changes) {
        if (c.type === 'remove') removedIds.add(c.id);
      }
      // 先应用变更（让 React Flow 移除节点）
      setNodes((nds) => applyNodeChanges(changes, nds));
      // 清理关联边
      if (removedIds.size > 0) {
        setEdges((eds) =>
          eds.filter((e) => !removedIds.has(e.source) && !removedIds.has(e.target))
        );
        // 通知 GraphPage 执行孤立节点清理
        onNodesDeleted?.(removedIds);
      }
    },
    [setNodes, setEdges, onNodesDeleted]
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

  // 空白区域右键：阻止浏览器默认菜单
  const onPaneContextMenu = useCallback((e: React.MouseEvent | MouseEvent) => {
    e.preventDefault();
  }, []);

  // 框选区域右键：React Flow 原生 onSelectionContextMenu，转发给 GraphPage
  const onSelectionContextMenuHandler = useCallback(
    (ev: React.MouseEvent, selectedNodes: Node<FlowNodeData>[]) => {
      ev.preventDefault();
      if (selectedNodes.length > 0 && onSelectionContextMenu) {
        onSelectionContextMenu(selectedNodes, ev);
      }
    },
    [onSelectionContextMenu]
  );

  return (
    <div style={{ width: '100%', height: '100%' }}>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        nodeTypes={nodeTypes}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onNodeDoubleClick={onNodeDoubleClick}
        onNodeDragStart={onNodeDragStart}
        onNodeDrag={onNodeDrag}
        onNodeContextMenu={onNodeContextMenuHandler}
        onSelectionContextMenu={onSelectionContextMenuHandler}
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
