/**
 * 节点右键菜单：
 * - 「继续查询前置节点」「继续查询后置节点」— 展开调用链
 * - 「跳转到定义」— 跳转到节点代码定义位置
 * - 「删除节点」— 从图中移除该节点及其关联边
 *
 * postMessage(queryPredecessors / querySuccessors / gotoSymbol) 发给主仓库
 */

import React from 'react';
import type { GraphType } from '../shared/types';
import type { FlowNodeData } from './adapters/callGraph';
import { getVscodeApi } from '../shared/vscodeApi';

/** 向 VSCode 主仓库发送 postMessage */
function postToHost(msg: unknown): void {
  getVscodeApi()?.postMessage(msg);
}

export interface GraphContextMenuProps {
  /** 当前图类型 */
  graphType: GraphType;
  /** 右键点击的节点 ID */
  nodeId: string;
  /** 右键点击的节点数据 */
  nodeData: FlowNodeData;
  /** 菜单在视口中的 X 坐标 */
  x: number;
  /** 菜单在视口中的 Y 坐标 */
  y: number;
  /** 关闭菜单的回调 */
  onClose: () => void;
  /** 删除节点的回调（从图中移除该节点及关联边） */
  onDeleteNode: (nodeId: string) => void;
}

/** 菜单项基础样式 */
const itemBaseStyle: React.CSSProperties = {
  display: 'block',
  width: '100%',
  padding: '6px 12px',
  textAlign: 'left',
  border: 'none',
  background: 'transparent',
  color: 'var(--vscode-foreground)',
  cursor: 'pointer',
  fontSize: 'var(--vscode-font-size, 13px)',
  fontFamily: 'var(--vscode-font-family, monospace)',
  whiteSpace: 'nowrap',
};

/** 菜单分隔线样式 */
const separatorStyle: React.CSSProperties = {
  height: 1,
  margin: '4px 8px',
  background: 'var(--vscode-editorWidget-border, #454545)',
};

/**
 * 菜单项组件：统一 hover 效果
 * @param label 菜单项文字
 * @param onClick 点击回调
 * @param disabled 是否禁用
 */
function MenuItem({
  label,
  onClick,
  disabled,
}: {
  label: string;
  onClick: () => void;
  disabled?: boolean;
}) {
  return (
    <button
      type="button"
      disabled={disabled}
      style={{
        ...itemBaseStyle,
        opacity: disabled ? 0.5 : 1,
        cursor: disabled ? 'default' : 'pointer',
      }}
      onMouseEnter={(e) => {
        if (!disabled) {
          (e.currentTarget as HTMLElement).style.background =
            'var(--vscode-list-hoverBackground)';
        }
      }}
      onMouseLeave={(e) => {
        (e.currentTarget as HTMLElement).style.background = 'transparent';
      }}
      onClick={onClick}
    >
      {label}
    </button>
  );
}

// ─── 框选右键菜单 ─────────────────────────────────────────

export interface SelectionContextMenuProps {
  /** 选中的节点 ID 列表 */
  selectedNodeIds: string[];
  /** 菜单在视口中的 X 坐标 */
  x: number;
  /** 菜单在视口中的 Y 坐标 */
  y: number;
  /** 关闭菜单的回调 */
  onClose: () => void;
  /** 批量删除选中节点的回调 */
  onDeleteSelected: (nodeIds: string[]) => void;
}

/**
 * 框选后右键菜单：提供「批量删除选中节点」操作
 */
export function SelectionContextMenu({
  selectedNodeIds,
  x,
  y,
  onClose,
  onDeleteSelected,
}: SelectionContextMenuProps) {
  const count = selectedNodeIds.length;

  return (
    <div
      className="graph-context-menu"
      onMouseDown={(e) => e.stopPropagation()}
      style={{
        position: 'fixed',
        left: x,
        top: y,
        zIndex: 1000,
        minWidth: 180,
        padding: 4,
        background: 'var(--vscode-editorWidget-background)',
        border: '1px solid var(--vscode-editorWidget-border)',
        borderRadius: 4,
        boxShadow: 'var(--vscode-widget-shadow)',
      }}
    >
      <MenuItem
        label={`删除选中的 ${count} 个节点`}
        onClick={() => {
          onDeleteSelected(selectedNodeIds);
          onClose();
        }}
      />
    </div>
  );
}

// ─── 单节点右键菜单 ───────────────────────────────────────

/**
 * 图节点右键菜单组件
 * 提供：展开前置/后置节点、跳转到定义、删除节点 四个操作
 */
export function GraphContextMenu({
  graphType,
  nodeId,
  nodeData,
  x,
  y,
  onClose,
  onDeleteNode,
}: GraphContextMenuProps) {
  const file = nodeData.definition?.file ?? '';
  const line = nodeData.definition?.line;
  const column = nodeData.definition?.column ?? 1;
  const symbol = nodeData.name ?? nodeData.label ?? nodeId;

  /** 发送继续查询请求（展开前置/后置节点） */
  const sendQuery = (action: 'queryPredecessors' | 'querySuccessors') => {
    postToHost({ action, graphType, nodeId, symbol, filePath: file, file });
    onClose();
  };

  /** 跳转到代码定义位置 */
  const gotoDefinition = () => {
    if (file && line != null) {
      postToHost({ action: 'gotoSymbol', file, line, column });
    }
    onClose();
  };

  /** 删除该节点 */
  const deleteNode = () => {
    onDeleteNode(nodeId);
    onClose();
  };

  const hasDefinition = Boolean(file && line != null);

  return (
    <div
      className="graph-context-menu"
      // 阻止 mousedown 冒泡，避免触发 GraphPage 中关闭菜单的 document 监听
      onMouseDown={(e) => e.stopPropagation()}
      style={{
        position: 'fixed',
        left: x,
        top: y,
        zIndex: 1000,
        minWidth: 180,
        padding: 4,
        background: 'var(--vscode-editorWidget-background)',
        border: '1px solid var(--vscode-editorWidget-border)',
        borderRadius: 4,
        boxShadow: 'var(--vscode-widget-shadow)',
      }}
    >
      <MenuItem label="展开前置节点" onClick={() => sendQuery('queryPredecessors')} />
      <MenuItem label="展开后置节点" onClick={() => sendQuery('querySuccessors')} />
      <div style={separatorStyle} />
      <MenuItem
        label="跳转到定义"
        onClick={gotoDefinition}
        disabled={!hasDefinition}
      />
      <div style={separatorStyle} />
      <MenuItem label="删除节点" onClick={deleteNode} />
    </div>
  );
}
