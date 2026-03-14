/**
 * G6 图配置工厂：导出节点/边/行为/插件配置常量和创建函数。
 *
 * 节点使用 rect 类型，4 个端口（上右下左），根节点橙色高亮。
 * 边使用 cubic-horizontal 曲线 + 箭头。
 * 行为：拖拽画布、滚轮缩放、点击选中、Shift 框选、节点拖拽。
 * 插件：History（撤销/恢复）。
 */

import type { GraphOptions, NodeData } from '@antv/g6';

/** 布局占位宽，取 minWidth(200)~maxWidth(360) 中间 */
export const NODE_WIDTH = 280;
/** 布局占位高 */
export const NODE_HEIGHT = 60;
/** 层间距（水平方向，LR 布局） */
export const RANK_SEP = 120;
/** 同层节点间距（垂直方向） */
export const NODE_SEP = 40;

/**
 * 创建 G6 Graph 配置对象
 *
 * @param container - DOM 容器元素
 * @returns G6 GraphOptions（不含 data，由调用方通过 graph.setData / graph.draw 设置）
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
        size: (d: NodeData) => {
          // 根据 label 行数动态计算高度
          const label = (d.data?.label as string) ?? '';
          const lines = label.split('\n').length;
          return [NODE_WIDTH, Math.max(NODE_HEIGHT, 24 + lines * 20)];
        },
        radius: 8,
        fill: (d: NodeData) =>
          d.data?.isRoot ? 'rgb(193, 125, 55)' : 'rgb(81, 154, 186)',
        stroke: (d: NodeData) =>
          d.data?.isRoot
            ? 'rgba(255, 255, 255, 0.5)'
            : 'rgba(255, 255, 255, 0.35)',
        lineWidth: 1,
        cursor: 'grab',
        // 标签
        labelText: (d: NodeData) => (d.data?.label as string) ?? '',
        labelFill: '#ffffff',
        labelFontSize: 13,
        labelFontFamily: 'var(--vscode-font-family, monospace)',
        labelPlacement: 'center',
        labelWordWrap: true,
        labelWordWrapWidth: NODE_WIDTH - 24,
        // 端口：四方向（上右下左），边自动选择最近端口
        ports: [
          { key: 'port-top', placement: [0.5, 0] as [number, number] },
          { key: 'port-right', placement: [1, 0.5] as [number, number] },
          { key: 'port-bottom', placement: [0.5, 1] as [number, number] },
          { key: 'port-left', placement: [0, 0.5] as [number, number] },
        ],
        portR: 3,
        portFill: '#ffffff',
        portLineWidth: 1,
        portStroke: 'rgba(255, 255, 255, 0.5)',
      } as Record<string, unknown>,
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

    // ─── 默认布局（通用 dagre LR，自定义布局在 g6Layout.ts 中注册后覆盖） ───
    layout: {
      type: 'dagre',
      rankdir: 'LR',
      nodesep: NODE_SEP,
      ranksep: RANK_SEP,
    },

    // ─── 交互行为 ───
    behaviors: [
      // 拖拽画布（左键拖拽）
      'drag-canvas',
      // 滚轮缩放
      'zoom-canvas',
      // 点击选中
      'click-select',
      // Shift+左键框选
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
    // 初始渲染不启用动画
    animation: false,
  };
}
