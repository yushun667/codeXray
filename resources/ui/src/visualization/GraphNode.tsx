/**
 * 图节点：最大宽度 200px，高度根据文字内容自适应（ref 测 scrollHeight），
 * 多行居中显示；支持显式换行 \n；拖拽 cursor grab。
 */

import { useEffect, useRef, useState } from 'react';
import { Handle, Position, type NodeProps } from 'reactflow';
import type { FlowNodeData } from './adapters/callGraph';

const MAX_WIDTH = 200;
const LINE_HEIGHT = 1.35;
const PADDING = 8;

export function GraphNode({ data }: NodeProps) {
  const label = (data as FlowNodeData)?.label ?? '';
  const ref = useRef<HTMLDivElement>(null);
  const [height, setHeight] = useState<number | 'auto'>('auto');

  useEffect(() => {
    if (ref.current) {
      const h = ref.current.scrollHeight;
      setHeight(h);
    }
  }, [label]);

  const lines = label.split('\n').filter(Boolean);
  const displayLines = lines.length > 0 ? lines : [label || ''];

  return (
    <>
      <Handle type="target" position={Position.Left} />
      <div
        ref={ref}
        title={label}
        style={{
          maxWidth: MAX_WIDTH,
          width: 'max-content',
          minHeight: typeof height === 'number' ? height : undefined,
          padding: PADDING,
          lineHeight: LINE_HEIGHT,
          textAlign: 'center',
          whiteSpace: 'normal',
          wordBreak: 'break-word',
          overflowWrap: 'break-word',
          boxSizing: 'border-box',
          color: '#fff',
          fontSize: 'var(--vscode-font-size, 13px)',
          fontFamily: 'var(--vscode-font-family, monospace)',
          cursor: 'grab',
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
