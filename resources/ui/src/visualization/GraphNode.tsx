/**
 * 图节点：使用 React Flow 默认节点框（.react-flow__node-default），
 * 名称多行居中显示，节点框高度随内容适应。
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
        className="react-flow__node-default"
        title={label}
        style={{
          minWidth: MIN_WIDTH,
          maxWidth: MAX_WIDTH,
          width: 'max-content',
          padding: `${PADDING_V}px ${PADDING_H}px`,
          lineHeight: LINE_HEIGHT,
          textAlign: 'center',
          whiteSpace: 'normal',
          wordBreak: 'break-word',
          overflowWrap: 'break-word',
          boxSizing: 'border-box',
          color: 'var(--vscode-editor-foreground, #d4d4d4)',
          fontSize: 'var(--vscode-font-size, 13px)',
          fontFamily: 'var(--vscode-font-family, monospace)',
        }}
      >
        {label}
      </div>
      <Handle type="source" position={Position.Right} />
    </>
  );
}
