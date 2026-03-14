/**
 * G6 图配置工厂
 *
 * 导出节点/边/行为/插件配置常量和 GraphOptions 创建函数。
 *
 * 节点：rect 类型，4 个连接桩（上右下左），根节点橙色高亮。
 * 边：cubic-horizontal 曲线 + 箭头，自动连接最近端口。
 * 行为：拖拽画布、滚轮缩放、点击选中、Shift 框选、节点拖拽。
 * 插件：History（撤销/恢复）。
 */

import type { GraphOptions, NodeData } from '@antv/g6';

// ─── 布局尺寸常量 ────────────────────────────────────────

/** 节点默认宽度 */
export const NODE_WIDTH = 280;
/** 节点默认高度 */
export const NODE_HEIGHT = 60;
/** 层间距（水平方向，LR 布局） */
export const RANK_SEP = 120;
/** 同层节点间距（垂直方向） */
export const NODE_SEP = 40;

// ─── 颜色常量 ────────────────────────────────────────────

/** 根节点填充色 */
const ROOT_FILL = 'rgb(193, 125, 55)';
/** 普通节点填充色 */
const NORMAL_FILL = 'rgb(81, 154, 186)';

/**
 * 创建 G6 Graph 配置对象
 *
 * @param container - DOM 容器元素
 * @returns GraphOptions（不含 data，由调用方通过 graph.setData / graph.render 设置）
 */
export function createGraphOptions(container: HTMLElement): GraphOptions {
  return {
    container,
    autoFit: 'view',
    padding: 20,

    // ─── 节点配置 ───
    node: {
      type: 'rect',
      style: {
        /* 尺寸：根据 label 行数动态计算高度 */
        size: (d: NodeData) => {
          const label = (d.data?.label as string) ?? '';
          const lines = label.split('\n').length;
          return [NODE_WIDTH, Math.max(NODE_HEIGHT, 24 + lines * 20)];
        },
        radius: 8,
        /* 填充色：根节点橙色、普通蓝色 */
        fill: (d: NodeData) => (d.data?.isRoot ? ROOT_FILL : NORMAL_FILL),
        stroke: (d: NodeData) =>
          d.data?.isRoot ? 'rgba(255,255,255,0.5)' : 'rgba(255,255,255,0.35)',
        lineWidth: 1,
        cursor: 'grab',

        /* 标签 */
        labelText: (d: NodeData) => (d.data?.label as string) ?? '',
        labelFill: '#ffffff',
        labelFontSize: 13,
        labelFontFamily: 'var(--vscode-font-family, monospace)',
        labelPlacement: 'center',
        labelWordWrap: true,
        labelWordWrapWidth: NODE_WIDTH - 24,

        /* 连接桩：四方向（上右下左），边自动选择最近端口连接 */
        port: true,
        ports: [
          { key: 'port-top', placement: [0.5, 0] as [number, number] },
          { key: 'port-right', placement: [1, 0.5] as [number, number] },
          { key: 'port-bottom', placement: [0.5, 1] as [number, number] },
          { key: 'port-left', placement: [0, 0.5] as [number, number] },
        ],
      } as Record<string, unknown>,

      /* 节点状态样式 */
      state: {
        selected: {
          stroke: '#007acc',
          lineWidth: 2,
          shadowBlur: 8,
          shadowColor: 'rgba(0, 122, 204, 0.5)',
        },
        highlight: {
          stroke: '#ffa940',
          lineWidth: 2.5,
          shadowBlur: 10,
          shadowColor: 'rgba(255, 169, 64, 0.6)',
        },
        dimmed: {
          opacity: 0.3,
        },
      },
    },

    // ─── 边配置 ───
    edge: {
      type: 'cubic-horizontal',
      style: {
        stroke: 'rgba(255, 255, 255, 0.55)',
        lineWidth: 1.5,
        endArrow: true,
        endArrowFill: 'rgba(255, 255, 255, 0.55)',
        endArrowSize: 8,
      },
      state: {
        highlight: {
          stroke: '#ffa940',
          lineWidth: 2.5,
          endArrowFill: '#ffa940',
        },
        dimmed: {
          opacity: 0.15,
        },
      },
    },

    // ─── 默认布局 (会在 G6Graph 中被自定义布局覆盖) ───
    layout: {
      type: 'dagre',
      rankdir: 'LR',
      nodesep: NODE_SEP,
      ranksep: RANK_SEP,
    },

    // ─── 交互行为 ───
    behaviors: [
      // 拖拽画布
      'drag-canvas',
      // 滚轮缩放
      'zoom-canvas',
      // 点击选中
      'click-select',
      // Shift + 左键框选
      {
        type: 'brush-select',
        key: 'brush-select',
        trigger: ['shift'],
        immediately: true,
      },
      // 节点拖拽
      'drag-element',
    ],

    // ─── 插件 ───
    plugins: [
      // 撤销/恢复历史记录
      { type: 'history', key: 'history' },
    ],

    // 缩放范围
    zoomRange: [0.05, 2],
    // 初始渲染不启用动画（避免首次加载闪烁）
    animation: false,
  };
}
