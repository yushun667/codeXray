import React from 'react';
import type { GraphType } from '../shared/types';

declare const acquireVsCodeApi: () => { postMessage: (msg: unknown) => void };
const vscode = typeof acquireVsCodeApi !== 'undefined' ? acquireVsCodeApi() : null;

export interface GraphContextMenuProps {
  visible: boolean;
  x: number;
  y: number;
  nodeId: string | null;
  graphType: GraphType | null;
  onClose: () => void;
}

export function GraphContextMenu({ visible, x, y, nodeId, graphType, onClose }: GraphContextMenuProps) {
  if (!visible || !nodeId || !graphType || !vscode) return null;

  const send = (action: 'queryPredecessors' | 'querySuccessors') => {
    vscode.postMessage({
      action,
      graphType,
      nodeId,
    });
    onClose();
  };

  return (
    <div
      className="graph-context-menu"
      style={{ left: x, top: y }}
      role="menu"
    >
      <button type="button" className="graph-context-item" onClick={() => send('queryPredecessors')}>
        继续查询前置节点
      </button>
      <button type="button" className="graph-context-item" onClick={() => send('querySuccessors')}>
        继续查询后置节点
      </button>
    </div>
  );
}
