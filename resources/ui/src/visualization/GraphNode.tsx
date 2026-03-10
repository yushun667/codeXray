/**
 * 图节点：仅提供内容与 Handle，节点框由 React Flow 外层容器提供（type=default 时带 .react-flow__node-default）。
 * 名称多行居中，无内层边框，避免出现“名称框叠在节点框上”的双框。
 */

import { Handle, Position, type NodeProps } from 'reactflow';
import type { FlowNodeData } from './adapters/callGraph';

const MIN_WIDTH = 200;
const MAX_WIDTH = 360;
const LINE_HEIGHT = 1.35;

export function GraphNode({ data }: NodeProps) {
  const label = (data as FlowNodeData)?.label ?? '';
  return (
    <>
      <Handle type="target" position={Position.Left} />
      {/* 仅做文本布局，不加边框/背景；节点框由父级 .react-flow__node-default 提供 */}
      <div
        title={label}
        style={{
          minWidth: MIN_WIDTH,
          maxWidth: MAX_WIDTH,
          width: 'max-content',
          lineHeight: LINE_HEIGHT,
          textAlign: 'center',
          whiteSpace: 'normal',
          wordBreak: 'break-word',
          overflowWrap: 'break-word',
          boxSizing: 'border-box',
          color: '#fff',
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
