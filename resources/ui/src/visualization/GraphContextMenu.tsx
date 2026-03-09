/**
 * 节点右键菜单：「继续查询前置节点」「继续查询后置节点」，postMessage(queryPredecessors/querySuccessors)
 */

import React from 'react';
import type { GraphType } from '../shared/types';
import type { FlowNodeData } from './adapters/callGraph';

declare const acquireVsCodeApi: () => { postMessage: (msg: unknown) => void };

const vscode = typeof acquireVsCodeApi !== 'undefined' ? acquireVsCodeApi() : null;

function postToHost(msg: unknown): void {
  vscode?.postMessage(msg);
}

export interface GraphContextMenuProps {
  graphType: GraphType;
  nodeId: string;
  nodeData: FlowNodeData;
  x: number;
  y: number;
  onClose: () => void;
}

export function GraphContextMenu({ graphType, nodeId, nodeData, x, y, onClose }: GraphContextMenuProps) {
  const file = nodeData.definition?.file ?? '';
  const symbol = nodeData.name ?? nodeData.label ?? nodeId;

  const sendQuery = (action: 'queryPredecessors' | 'querySuccessors') => {
    postToHost({
      action,
      graphType,
      nodeId,
      symbol,
      filePath: file,
      file,
    });
    onClose();
  };

  return (
    <div
      className="graph-context-menu"
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
        fontFamily: 'var(--vscode-font-family)',
        fontSize: 'var(--vscode-font-size)',
      }}
    >
      <button
        type="button"
        className="context-menu-item"
        style={{
          display: 'block',
          width: '100%',
          padding: '6px 12px',
          textAlign: 'left',
          border: 'none',
          background: 'transparent',
          color: 'var(--vscode-foreground)',
          cursor: 'pointer',
        }}
        onClick={() => sendQuery('queryPredecessors')}
      >
        继续查询前置节点
      </button>
      <button
        type="button"
        className="context-menu-item"
        style={{
          display: 'block',
          width: '100%',
          padding: '6px 12px',
          textAlign: 'left',
          border: 'none',
          background: 'transparent',
          color: 'var(--vscode-foreground)',
          cursor: 'pointer',
        }}
        onClick={() => sendQuery('querySuccessors')}
      >
        继续查询后置节点
      </button>
    </div>
  );
}
