/**
 * 自定义节点：函数名多行居中、根据文字测量宽高、自定义样式（颜色/边框/圆角）。
 * 通过 onNodeDimensions 回传测量尺寸供 dagre 布局防重叠。
 */

import { useCallback, useEffect, useRef, useState } from 'react';
import { Handle, Position, type NodeProps } from 'reactflow';
import type { FlowNodeData } from './adapters/callGraph';

const PADDING_X = 14;
const PADDING_Y = 10;
const LINE_HEIGHT = 1.35;
const MIN_WIDTH = 100;
const MIN_HEIGHT = 32;
const MAX_WIDTH = 360;

export interface CustomNodeStyle {
  backgroundColor?: string;
  color?: string;
  borderColor?: string;
  borderRadius?: number;
}

const defaultStyle: CustomNodeStyle = {
  backgroundColor: 'rgb(81, 154, 186)',
  color: '#fff',
  borderColor: 'rgba(255,255,255,0.3)',
  borderRadius: 8,
};

export function CustomNode({ id, data }: NodeProps<FlowNodeData>) {
  const label = data?.label ?? '';
  const measureRef = useRef<HTMLDivElement>(null);
  const [reported, setReported] = useState(false);
  const style: CustomNodeStyle = { ...defaultStyle, ...(data?.style as CustomNodeStyle | undefined) };
  const onDimensions = (data as { onDimensions?: (id: string, w: number, h: number) => void })?.onDimensions;

  const reportDimensions = useCallback(() => {
    const el = measureRef.current;
    if (!el || !onDimensions) return;
    const w = Math.min(MAX_WIDTH, Math.max(MIN_WIDTH, el.offsetWidth + PADDING_X * 2));
    const h = Math.max(MIN_HEIGHT, el.offsetHeight + PADDING_Y * 2);
    onDimensions(id, w, h);
    setReported(true);
  }, [id, onDimensions]);

  useEffect(() => {
    if (!onDimensions || reported) return;
    const t = requestAnimationFrame(() => {
      reportDimensions();
    });
    return () => cancelAnimationFrame(t);
  }, [label, onDimensions, reported, reportDimensions]);

  return (
    <>
      <Handle type="target" position={Position.Left} />
      <div
        style={{
          width: '100%',
          height: '100%',
          minWidth: MIN_WIDTH,
          minHeight: MIN_HEIGHT,
          padding: `${PADDING_Y}px ${PADDING_X}px`,
          backgroundColor: style.backgroundColor,
          color: style.color,
          border: `1px solid ${style.borderColor}`,
          borderRadius: style.borderRadius ?? 8,
          boxSizing: 'border-box',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          fontSize: 'var(--vscode-font-size, 13px)',
          fontFamily: 'var(--vscode-font-family, monospace)',
        }}
      >
        {/* 隐藏测量用，用于计算内容尺寸 */}
        <div
          ref={measureRef}
          aria-hidden
          style={{
            position: 'absolute',
            visibility: 'hidden',
            display: 'inline-block',
            whiteSpace: 'normal',
            wordBreak: 'break-word',
            overflowWrap: 'break-word',
            maxWidth: MAX_WIDTH - PADDING_X * 2,
            lineHeight: LINE_HEIGHT,
            textAlign: 'center',
            pointerEvents: 'none',
            left: 0,
            top: 0,
            fontFamily: 'var(--vscode-font-family, monospace)',
            fontSize: 'var(--vscode-font-size, 13px)',
          }}
        >
          {label}
        </div>
        {/* 可见多行居中文字 */}
        <div
          title={label}
          style={{
            width: '100%',
            lineHeight: LINE_HEIGHT,
            textAlign: 'center',
            whiteSpace: 'normal',
            wordBreak: 'break-word',
            overflowWrap: 'break-word',
          }}
        >
          {label}
        </div>
      </div>
      <Handle type="source" position={Position.Right} />
    </>
  );
}
