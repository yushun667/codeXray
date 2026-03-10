/**
 * 图节点：名称多行在边框中显示，高度随内容适应；悬停 title 显示全文
 */

import React from 'react';
import { Handle, Position, type NodeProps } from 'reactflow';
import type { FlowNodeData } from './adapters/callGraph';

const MIN_WIDTH = 200;
const MAX_WIDTH = 360;
const PADDING_V = 10;
const PADDING_H = 12;
const LINE_HEIGHT = 1.35;

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
          padding: `${PADDING_V}px ${PADDING_H}px`,
          background: 'var(--vscode-editor-background, #1e1e1e)',
          border: '1px solid var(--vscode-panel-border, #3c3c3c)',
          borderRadius: 6,
          color: 'var(--vscode-editor-foreground, #d4d4d4)',
          fontSize: 'var(--vscode-font-size, 13px)',
          fontFamily: 'var(--vscode-font-family, monospace)',
          lineHeight: LINE_HEIGHT,
          whiteSpace: 'normal',
          wordBreak: 'break-word',
          overflowWrap: 'break-word',
          boxSizing: 'border-box',
        }}
      >
        {label}
      </div>
      <Handle type="source" position={Position.Right} />
    </>
  );
}
