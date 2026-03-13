/**
 * 图节点：自定义 GraphNode。
 * - minWidth 200、maxWidth 360，高度根据内容自适应。
 * - title={label} 悬停显示完整名称，避免名称显示不全。
 * - 多行居中显示（支持 \n 换行）；cursor: grab 拖拽手势。
 * - 颜色通过 CSS class 引用，不写死颜色。
 */

import { useEffect, useRef, useState } from 'react';
import { Handle, Position, type NodeProps } from 'reactflow';
import type { FlowNodeData } from './adapters/callGraph';

const LINE_HEIGHT = 1.4;
const PADDING_H = 12;
const PADDING_V = 8;

export function GraphNode({ data, selected }: NodeProps) {
  const flowData = data as FlowNodeData;
  const label = String(flowData?.label ?? '');
  const isRoot = flowData?.isRoot === true;
  const ref = useRef<HTMLDivElement>(null);
  const [measuredHeight, setMeasuredHeight] = useState<number | undefined>(undefined);

  useEffect(() => {
    if (ref.current) {
      setMeasuredHeight(ref.current.scrollHeight);
    }
  }, [label]);

  const parts = label.split('\n').filter(Boolean);
  const displayLines = parts.length > 0 ? parts : [label || ''];

  return (
    <>
      <Handle type="target" position={Position.Left} />
      <div
        ref={ref}
        title={label}
        className={`graph-node-inner${selected ? ' graph-node-selected' : ''}${isRoot ? ' graph-node-root' : ''}`}
        style={{
          minWidth: 200,
          maxWidth: 360,
          height: measuredHeight != null ? measuredHeight : undefined,
          paddingTop: PADDING_V,
          paddingBottom: PADDING_V,
          paddingLeft: PADDING_H,
          paddingRight: PADDING_H,
          lineHeight: LINE_HEIGHT,
          textAlign: 'center',
          whiteSpace: 'normal',
          wordBreak: 'break-word',
          overflowWrap: 'break-word',
          boxSizing: 'border-box',
          cursor: 'grab',
          fontSize: 'var(--vscode-font-size, 13px)',
          fontFamily: 'var(--vscode-font-family, monospace)',
        }}
      >
        {displayLines.map((line, i) => (
          <div key={i}>{line}</div>
        ))}
      </div>
      <Handle type="source" position={Position.Right} />
    </>
  );
}
