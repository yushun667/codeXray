/**
 * 图节点：仅一个节点框，名称在框内多行显示并居中；悬停 title 显示全文
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
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        textAlign: 'center',
      }}
    >
      <Handle type="target" position={Position.Left} />
      <span style={{ flex: 1, textAlign: 'center', minWidth: 0 }}>{label}</span>
      <Handle type="source" position={Position.Right} />
    </div>
  );
}
