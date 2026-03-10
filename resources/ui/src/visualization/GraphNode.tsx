/**
 * 图节点：展示完整 label，悬停显示全文（title），避免名称被截断
 */

import React from 'react';
import { Handle, Position, type NodeProps } from 'reactflow';
import type { FlowNodeData } from './adapters/callGraph';

const MIN_WIDTH = 200;
const MAX_WIDTH = 360;
const PADDING = 10;

export function GraphNode({ data }: NodeProps) {
  const label = (data as FlowNodeData)?.label ?? '';
  return (
    <>
      <Handle type="target" position={Position.Left} />
      <div
        title={label}
        style={{
        minWidth: MIN_WIDTH,
        maxWidth: MAX_WIDTH,
        padding: `${PADDING}px 12px`,
        background: 'var(--vscode-editor-background, #1e1e1e)',
        border: '1px solid var(--vscode-panel-border, #3c3c3c)',
        borderRadius: 6,
        color: 'var(--vscode-editor-foreground, #d4d4d4)',
        fontSize: 'var(--vscode-font-size, 13px)',
        fontFamily: 'var(--vscode-font-family, monospace)',
        overflow: 'hidden',
        textOverflow: 'ellipsis',
        whiteSpace: 'nowrap',
      }}
    >
      {label}
    </div>
      <Handle type="source" position={Position.Right} />
    </>
  );
}
