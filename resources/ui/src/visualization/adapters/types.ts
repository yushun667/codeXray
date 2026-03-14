/**
 * 通用图节点/边类型（图库无关）
 *
 * 替代 `import type { Node, Edge } from 'reactflow'`，
 * 让 adapter 输出格式独立于具体图渲染库（React Flow / AntV G6 等）。
 * 各 adapter 返回此类型，由上层转换为目标图库的数据格式。
 */

/** 通用图节点 */
export interface AdapterNode<T = Record<string, unknown>> {
  /** 节点唯一标识（字符串） */
  id: string;
  /** 节点类型标识（如 'graphNode'），上层可忽略 */
  type?: string;
  /** 节点位置（adapter 中默认 {0,0}，由图库布局引擎计算实际位置） */
  position: { x: number; y: number };
  /** 节点携带的业务数据 */
  data: T;
}

/** 通用图边 */
export interface AdapterEdge {
  /** 边唯一标识 */
  id: string;
  /** 起点节点 ID */
  source: string;
  /** 终点节点 ID */
  target: string;
  /** 边携带的业务数据 */
  data?: Record<string, unknown>;
}
