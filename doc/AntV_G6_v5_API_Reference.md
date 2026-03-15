# AntV G6 v5 完整 API 参考手册

**日期**: 2026 年 3 月
**版本**: 5.0.51 (最新稳定版), 5.1.0-beta.1 (测试版)
**来源**: 官方 G6 仓库 (v5 分支), NPM 注册表, GitHub 源码

---

## 1. 图实例创建

### 1.1 Graph 构造函数

```typescript
import { Graph } from '@antv/g6';

const graph = new Graph({
  // 必需参数
  container: 'canvas-container',           // HTML 容器 ID 或 DOM 元素
  width: 1000,                             // 画布宽度 (px)
  height: 600,                             // 画布高度 (px)

  // 数据配置
  data: {
    nodes: [
      { id: 'node-1', label: 'Node 1' },
      { id: 'node-2', label: 'Node 2' }
    ],
    edges: [
      { source: 'node-1', target: 'node-2' }
    ]
  },

  // 布局配置
  layout: {
    type: 'dagre',                        // 布局类型
    direction: 'LR',                      // 方向: LR, RL, TB, BT
    ranksep: 50,                          // 秩间距
    nodesep: 30,                          // 节点间距
    controlPoints: true
  },

  // 节点默认配置
  node: {
    type: 'rect',                         // 节点类型
    labelShape: {
      content: '{label}',
      position: 'center',
      fontSize: 12
    },
    style: {
      fill: '#e8f5e9',
      stroke: '#4caf50',
      lineWidth: 2,
      radius: [8, 8, 8, 8]
    },
    state: {
      selected: {
        stroke: '#ff9800',
        lineWidth: 3
      },
      highlight: {
        fill: '#fff9c4'
      },
      dimmed: {
        opacity: 0.3
      }
    }
  },

  // 边默认配置
  edge: {
    type: 'cubic-horizontal',             // 边类型
    labelShape: {
      content: '{label}',
      position: 'center'
    },
    style: {
      stroke: '#999',
      lineWidth: 1,
      endArrow: true                      // 显示箭头
    },
    state: {
      selected: {
        stroke: '#ff9800',
        lineWidth: 2
      }
    }
  },

  // 交互行为
  behaviors: [
    'drag-canvas',                        // 拖拽平移画布
    'scroll-canvas',                      // 滚轮缩放
    'zoom-canvas',                        // Ctrl+滚轮 或 Ctrl+拖拽缩放
    'click-select',                       // 点击选中
    'brush-select',                       // 矩形框选
    'lasso-select',                       // 套索选择
    'drag-element',                       // 拖拽节点
    'hover-activate',                     // 悬停高亮
    'focus-element'                       // 点击居中
  ],

  // 插件
  plugins: [
    // new History(),
    // new ContextMenu(),
    // new Tooltip()
  ],

  // 主题
  theme: 'light',                         // 'light' 或 'dark'

  // 其他选项
  autoResize: true,                       // 自动响应容器大小变化
  enableStack: true,                      // 启用撤销栈
  mode: 'default'                         // 模式类型
});

// 渲染图表
graph.render();
```

### 1.2 构造函数完整选项签名

```typescript
interface GraphOptions {
  container: string | HTMLElement;
  width: number;
  height: number;
  data?: GraphData;
  layout?: LayoutOptions;
  node?: NodeOptions;
  edge?: EdgeOptions;
  behaviors?: BehaviorOptions[];
  plugins?: IPlugin[];
  theme?: string;
  autoResize?: boolean;
  enableStack?: boolean;
  mode?: string;
  renderer?: string;
  // ... 其他选项
}
```

---

## 2. 节点配置详解

### 2.1 支持的节点类型

- **rect** - 矩形节点 (支持端口、图标、徽章)
- **circle** - 圆形节点
- **ellipse** - 椭圆节点
- **diamond** - 菱形节点
- **triangle** - 三角形节点
- **star** - 星形节点
- **html** - HTML 渲染节点
- **image** - 图像节点
- **modelRect** - 模型/类矩形

### 2.2 基础节点配置

```typescript
interface Node {
  id: string;                             // 节点唯一标识
  label?: string;                         // 显示标签
  type?: string;                          // 节点类型
  data?: {
    ports?: NodePort[];                   // 端口配置数组
    [key: string]: any;                   // 自定义数据
  };
  style?: {
    x?: number;                           // X 坐标
    y?: number;                           // Y 坐标
    size?: number | [number, number];     // 尺寸 (宽高或直径)
    width?: number;                       // 宽度 (rect 特定)
    height?: number;                      // 高度 (rect 特定)
    fill?: string;                        // 填充颜色
    stroke?: string;                      // 边框颜色
    lineWidth?: number;                   // 边框宽度
    radius?: number | number[];           // 圆角半径
    opacity?: number;                     // 透明度 (0-1)
  };
}
```

### 2.3 带 4 向端口的节点配置

```typescript
interface NodePort {
  key: string;                            // 端口唯一键
  placement:                              // 端口位置
    | 'top'                               // 顶部中心
    | 'bottom'                            // 底部中心
    | 'left'                              // 左侧中心
    | 'right'                             // 右侧中心
    | [number, number];                   // 相对位置 [0-1, 0-1]
  r?: number;                             // 端口半径
  fill?: string;                          // 填充颜色
  stroke?: string;                        // 边框颜色
  lineWidth?: number;                     // 边框宽度
}
```

### 2.4 完整 4 向端口示例

```typescript
const nodeWithPorts = {
  id: 'node-with-ports',
  label: '功能节点',
  type: 'rect',
  style: {
    width: 200,
    height: 100,
    fill: '#fff',
    stroke: '#000',
    lineWidth: 2
  },
  data: {
    ports: [
      // 顶部端口
      {
        key: 'port-top',
        placement: 'top',
        r: 5,
        fill: '#fff',
        stroke: '#1890ff',
        lineWidth: 2
      },
      // 右侧端口
      {
        key: 'port-right',
        placement: 'right',
        r: 5,
        fill: '#fff',
        stroke: '#1890ff',
        lineWidth: 2
      },
      // 底部端口
      {
        key: 'port-bottom',
        placement: 'bottom',
        r: 5,
        fill: '#fff',
        stroke: '#1890ff',
        lineWidth: 2
      },
      // 左侧端口
      {
        key: 'port-left',
        placement: 'left',
        r: 5,
        fill: '#fff',
        stroke: '#1890ff',
        lineWidth: 2
      }
    ]
  }
};

graph.addNodeData([nodeWithPorts]);
```

### 2.5 节点样式状态

```typescript
const nodeConfig = {
  type: 'rect',
  style: {
    fill: '#e8f5e9',
    stroke: '#4caf50',
    lineWidth: 2
  },
  state: {
    // 选中状态
    selected: {
      stroke: '#ff9800',
      lineWidth: 3,
      shadowColor: '#ff9800',
      shadowBlur: 8
    },
    // 高亮状态 (用于路径突出显示)
    highlight: {
      fill: '#fff9c4',
      stroke: '#ff6f00',
      lineWidth: 3
    },
    // 变暗状态 (用于过滤)
    dimmed: {
      opacity: 0.3,
      fill: '#ccc'
    },
    // 活跃状态 (悬停时)
    active: {
      fill: '#fff9c4',
      shadowColor: '#1890ff',
      shadowBlur: 10
    }
  }
};
```

---

## 3. 边配置详解

### 3.1 支持的边类型

- **line** - 直线边
- **polyline** - 折线边 (支持 A* 路由和正交路由)
- **quadratic** - 二次贝塞尔曲线
- **cubic** - 三次贝塞尔曲线
- **cubic-horizontal** - 水平三次贝塞尔曲线 **[推荐用于流程图]**
- **cubic-vertical** - 垂直三次贝塞尔曲线
- **cubic-radial** - 径向三次贝塞尔曲线

### 3.2 基础边配置

```typescript
interface Edge {
  id?: string;                            // 边唯一标识 (自动生成)
  source: string;                         // 源节点 ID
  target: string;                         // 目标节点 ID
  label?: string;                         // 边标签
  type?: string;                          // 边类型
  data?: {
    sourcePort?: string;                  // 源端口键
    targetPort?: string;                  // 目标端口键
    [key: string]: any;
  };
  style?: {
    stroke?: string;                      // 线条颜色
    lineWidth?: number;                   // 线条宽度
    opacity?: number;                     // 透明度
    endArrow?: boolean | object;          // 目标箭头
    startArrow?: boolean | object;        // 源箭头
    lineDash?: number[];                  // 虚线 [3, 3]
    shadowColor?: string;                 // 阴影颜色
    shadowBlur?: number;                  // 阴影模糊
  };
}
```

### 3.3 cubic-horizontal 边配置

```typescript
const cubicHorizontalEdge = {
  source: 'node-1',
  target: 'node-2',
  label: '调用',
  type: 'cubic-horizontal',               // 水平曲线
  style: {
    stroke: '#1890ff',
    lineWidth: 2,
    endArrow: {                           // 箭头配置
      path: 'M 0,0 L 8,-4 L 8,4 Z',      // 箭头路径
      fill: '#1890ff'
    },
    opacity: 1,
    shadowColor: '#1890ff',
    shadowBlur: 4
  },
  data: {
    sourcePort: 'port-right',             // 从右侧端口出发
    targetPort: 'port-left',              // 到达左侧端口
    curvePosition: 0.5,                   // 曲线控制点位置 (0-1)
    curveOffset: 0                        // 曲线偏移距离
  }
};

graph.addEdgeData([cubicHorizontalEdge]);
```

### 3.4 箭头配置详解

```typescript
// 简单箭头 (默认)
endArrow: true

// 自定义箭头对象
endArrow: {
  path: 'M 0,0 L 8,-4 L 8,4 Z',          // SVG 路径
  fill: '#1890ff',                       // 箭头填充色
  stroke: '#0050b3',                     // 箭头边框色
  lineWidth: 1
}

// 起点箭头
startArrow: true

// 双向箭头
endArrow: true,
startArrow: true
```

### 3.5 端口基连接

当边指定 `sourcePort` 和 `targetPort` 时，边自动连接到对应端口位置：

```typescript
// 方式 1: 通过端口名称
edge: {
  source: 'node-1',
  target: 'node-2',
  data: {
    sourcePort: 'port-right',    // 连接到 node-1 的右侧端口
    targetPort: 'port-left'      // 连接到 node-2 的左侧端口
  }
}

// 方式 2: 通过相对坐标 (placement)
// G6 会自动计算最近的端口连接
```

---

## 4. 布局配置详解

### 4.1 支持的布局类型

**分层布局 (Hierarchical):**
- **dagre** - 分层有向图 (最常用于流程图和调用图) **[强烈推荐]**
- antv-dagre - AntV 自定义 Dagre
- fishbone - 鱼骨图
- snake - 蛇形布局

**力导向布局 (Force-based):**
- d3-force, force, fruchterman, forceAtlas2

**圆形布局 (Circular):**
- circular, concentric, radial

**其他:**
- grid, mds, random

### 4.2 Dagre 布局完整配置

```typescript
const graph = new Graph({
  layout: {
    type: 'dagre',

    // 方向: 支持 4 个值
    direction: 'LR',                      // LR=左到右, RL=右到左, TB=上到下, BT=下到上

    // 间距配置
    ranksep: 50,                          // 秩之间的距离 (垂直距离)
    nodesep: 30,                          // 同秩节点之间的距离 (水平距离)

    // 控制点
    controlPoints: true,                  // 是否生成贝塞尔曲线控制点

    // 排列方式
    rankdir: 'LR',                        // 备选写法: rankdir 而不是 direction

    // 回调
    begin: [],                            // 布局前回调
    end: [],                              // 布局后回调

    // 边长 (某些布局适用)
    edgeLength: 150,

    // 防碰撞
    preventOverlap: true,                 // 防止节点重叠
    nodeSize: [200, 100]                  // 节点大小用于碰撞计算
  }
});
```

### 4.3 方向说明

- **'LR'** - 左到右 (水平), 适合调用图、流程图
- **'RL'** - 右到左 (水平反向)
- **'TB'** - 上到下 (垂直), 适合树形图
- **'BT'** - 下到上 (垂直反向)

### 4.4 自定义布局扩展

```typescript
import { BaseLayout } from '@antv/g6';

class CustomLayout extends BaseLayout {
  // 计算节点位置
  execute(graph) {
    const nodes = graph.getNodes();
    const edges = graph.getEdges();

    // 自定义布局逻辑
    nodes.forEach((node, index) => {
      node.data.x = index * 200;
      node.data.y = 0;
    });

    return nodes.map(node => ({
      id: node.id,
      x: node.data.x,
      y: node.data.y
    }));
  }
}

// 注册自定义布局
graph.registry.register('layout', 'custom', CustomLayout);

// 使用
graph.layout({
  type: 'custom'
});
```

---

## 5. 交互行为配置

### 5.1 可用行为列表

**画布操作:**
- **drag-canvas** - 拖拽平移画布
- **scroll-canvas** - 滚轮缩放 (鼠标滚轮缩放)
- **zoom-canvas** - Ctrl+滚轮 或 Ctrl+拖拽 缩放

**选择操作:**
- **click-select** - 单击选中 / 多击多选
- **brush-select** - 矩形框选
- **lasso-select** - 套索自由选择

**编辑操作:**
- **drag-element** - 拖拽移动节点和边
- **create-edge** - 绘制新边

**视觉反馈:**
- **hover-activate** - 悬停时高亮相关元素 **[高级功能]**
- **focus-element** - 点击节点时自动平移至视口中心
- **auto-adapt-label** - 动态调整标签大小
- **fix-element-size** - 保持元素固定尺寸

**树操作:**
- **collapse-expand** - 树节点展开/折叠

**优化:**
- **optimize-viewport-transform** - 视口变换优化

### 5.2 行为启用方式

```typescript
const graph = new Graph({
  behaviors: [
    'drag-canvas',
    'scroll-canvas',
    'zoom-canvas',
    'click-select',
    'brush-select',
    'drag-element',
    'hover-activate',
    'focus-element'
  ]
});
```

### 5.3 hover-activate 行为详解

```typescript
// 在 behaviors 中配置
behaviors: [
  {
    type: 'hover-activate',

    // 动画效果
    animation: true,                      // 平滑过渡状态

    // 高亮范围
    degree: 1,                            // 0=只高亮当前元素
                                          // 1=当前+直接连接
                                          // 2=当前+2度关系

    // 方向过滤
    direction: 'both',                    // 'both', 'in', 'out'

    // 使用的状态名
    state: 'active',                      // 触发的状态名

    // 回调
    onHover: (itemId) => {
      console.log('Hovering:', itemId);
    },
    onUnhover: (itemId) => {
      console.log('Left:', itemId);
    }
  }
]
```

---

## 6. 插件系统

### 6.1 History 插件 (撤销/重做)

```typescript
import { History } from '@antv/g6';

const history = new History();
graph.addPlugin(history);

// 撤销
history.undo();

// 重做
history.redo();

// 检查状态
if (history.canUndo()) {
  history.undo();
}

if (history.canRedo()) {
  history.redo();
}

// 清除历史
history.clear();

// 自动追踪以下操作:
// - addNodeData, removeNodeData, updateNodeData
// - addEdgeData, removeEdgeData, updateEdgeData
// - graph.setElementState()
// - graph.clearElementState()
```

### 6.2 Context Menu 插件 (右键菜单)

```typescript
import { ContextMenu } from '@antv/g6';

const contextMenu = new ContextMenu({
  container: document.getElementById('context-menu'),
  items: [
    {
      label: '编辑',
      onClick: (event, element) => {
        console.log('Edit:', element.id);
      }
    },
    {
      label: '删除',
      onClick: (event, element) => {
        graph.removeNodeData([element.id]);
      }
    },
    {
      type: 'separator'                   // 分隔线
    },
    {
      label: '复制',
      onClick: (event, element) => { }
    }
  ],

  // 自动响应右键
  trigger: 'contextmenu'
});

graph.addPlugin(contextMenu);
```

### 6.3 其他插件

```typescript
import {
  Tooltip,
  Minimap,
  Grid,
  Legend,
  Timebar
} from '@antv/g6';

graph.addPlugin(new Tooltip());
graph.addPlugin(new Minimap());
graph.addPlugin(new Grid());
graph.addPlugin(new Legend());
```

---

## 7. 数据操作 API

### 7.1 设置初始数据

```typescript
// 方式 1: 构造函数中
const graph = new Graph({
  data: {
    nodes: [
      { id: 'node-1', label: 'Node 1' },
      { id: 'node-2', label: 'Node 2' }
    ],
    edges: [
      { source: 'node-1', target: 'node-2' }
    ]
  }
});

// 方式 2: 渲染后设置
graph.setData({
  nodes: [...],
  edges: [...]
});

// 渲染
graph.render();
```

### 7.2 添加节点/边

```typescript
// 添加单个或多个节点
graph.addNodeData([
  { id: 'new-node-1', label: 'New Node 1' },
  { id: 'new-node-2', label: 'New Node 2' }
]);

// 添加边
graph.addEdgeData([
  { source: 'node-1', target: 'new-node-1' },
  { source: 'node-2', target: 'new-node-2' }
]);
```

### 7.3 更新节点/边

```typescript
// 更新节点 (部分字段更新)
graph.updateNodeData([
  { id: 'node-1', label: '更新后的标签' }
]);

// 更新边
graph.updateEdgeData([
  { id: 'edge-1', label: '更新后的边标签' }
]);
```

### 7.4 移除节点/边

```typescript
// 移除节点 (会自动移除关联的边)
graph.removeNodeData(['node-1', 'node-2']);

// 移除边
graph.removeEdgeData(['edge-1', 'edge-2']);
```

### 7.5 查询数据

```typescript
// 获取所有节点
const nodes = graph.getNodes();

// 获取单个节点
const node = graph.getNodeById('node-1');

// 获取所有边
const edges = graph.getEdges();

// 获取单个边
const edge = graph.getEdgeById('edge-1');

// 获取节点的连接边
const node = graph.getNodeById('node-1');
const connectedEdges = graph.getEdgesData().filter(e =>
  e.source === node.id || e.target === node.id
);
```

### 7.6 批量操作

```typescript
// 将多个操作合并为一次重新渲染
graph.batch(() => {
  graph.addNodeData([...]);
  graph.addEdgeData([...]);
  graph.updateNodeData([...]);
  // 只会重新渲染一次
});
```

---

## 8. 元素状态管理

### 8.1 setElementState - 设置元素状态

```typescript
// 设置单个元素状态
graph.setElementState('node-1', 'selected');

// 同时设置多个状态
graph.setElementState('node-1', ['selected', 'highlight']);

// 对多个元素设置状态
['node-1', 'node-2', 'edge-1'].forEach(itemId => {
  graph.setElementState(itemId, 'highlight');
});
```

### 8.2 clearElementState - 清除元素状态

```typescript
// 清除单个状态
graph.clearElementState('node-1', 'selected');

// 清除所有状态
graph.clearElementState('node-1');

// 批量清除
graph.clearElementState('node-1', 'highlight');
```

### 8.3 findIdByState - 查找具有特定状态的元素

```typescript
// 获取所有选中的元素
const selectedIds = graph.findIdByState('selected');

// 获取所有高亮元素
const highlightedIds = graph.findIdByState('highlight');

// 清除所有高亮
highlightedIds.forEach(id => {
  graph.clearElementState(id, 'highlight');
});
```

### 8.4 路径高亮示例

```typescript
function highlightPath(graph, path) {
  // 设置路径中所有元素为高亮状态
  path.forEach(itemId => {
    graph.setElementState(itemId, 'highlight');
  });
}

function dimOtherElements(graph, highlightedIds) {
  // 获取所有节点和边
  const allNodes = graph.getNodes();
  const allEdges = graph.getEdges();

  allNodes.concat(allEdges).forEach(item => {
    if (!highlightedIds.includes(item.id)) {
      graph.setElementState(item.id, 'dimmed');
    }
  });
}

function clearHighlight(graph) {
  // 清除所有高亮
  const highlightedIds = graph.findIdByState('highlight');
  highlightedIds.forEach(id => {
    graph.clearElementState(id, 'highlight');
  });

  // 清除所有变暗
  const dimmedIds = graph.findIdByState('dimmed');
  dimmedIds.forEach(id => {
    graph.clearElementState(id, 'dimmed');
  });
}
```

---

## 9. 事件系统

### 9.1 节点和边事件

```typescript
// 点击事件
graph.on('node:click', (event) => {
  const { itemId, item } = event;
  console.log('Clicked node:', itemId);
});

graph.on('edge:click', (event) => {
  const { itemId, item } = event;
  console.log('Clicked edge:', itemId);
});

// 双击事件
graph.on('node:dblclick', (event) => {
  const { itemId } = event;
  console.log('Double clicked:', itemId);
});

// 右键菜单事件
graph.on('node:contextmenu', (event) => {
  event.preventDefault();  // 阻止默认右键菜单
  const { x, y, itemId } = event;
  // 显示自定义菜单
});

// 鼠标进入/离开
graph.on('node:pointerenter', (event) => {
  console.log('Mouse enter:', event.itemId);
});

graph.on('node:pointerleave', (event) => {
  console.log('Mouse leave:', event.itemId);
});
```

### 9.2 拖拽事件

```typescript
graph.on('node:dragstart', (event) => {
  const { itemId, canvasX, canvasY } = event;
  console.log('Started dragging:', itemId);
});

graph.on('node:dragmove', (event) => {
  const { itemId, canvasX, canvasY } = event;
  // 实时拖拽中
  // 可实现碰撞检测
});

graph.on('node:dragend', (event) => {
  const { itemId, canvasX, canvasY } = event;
  console.log('Finished dragging:', itemId);
});
```

### 9.3 选择事件

```typescript
graph.on('element:selected', (event) => {
  console.log('Selected:', event.itemId);
});

graph.on('element:unselected', (event) => {
  console.log('Unselected:', event.itemId);
});
```

### 9.4 画布事件

```typescript
graph.on('canvas:click', (event) => {
  const { x, y } = event;
  console.log('Canvas clicked at:', x, y);
});

graph.on('canvas:contextmenu', (event) => {
  event.preventDefault();
  // 画布右键菜单
});

graph.on('canvas:dblclick', (event) => {
  console.log('Canvas double clicked');
});
```

### 9.5 事件对象结构

```typescript
interface G6Event {
  itemId: string;                         // 元素 ID
  item: any;                              // 元素对象 (节点或边)
  x: number;                              // 画布坐标 X
  y: number;                              // 画布坐标 Y
  canvasX?: number;                       // 画布坐标 (alt)
  canvasY?: number;
  clientX?: number;                       // 屏幕坐标 X
  clientY?: number;
  srcEvent: MouseEvent;                   // 原始 DOM 事件
  preventDefault(): void;
  stopPropagation(): void;
}
```

---

## 10. 视口控制 API

### 10.1 fitView - 缩放至内容

```typescript
// 基础使用
graph.fitView({
  direction: 'both',                      // 'x', 'y', 或 'both'
  padding: [50, 50, 50, 50],             // [上, 右, 下, 左] 或 [水平, 垂直]
  when: 'always'                          // 'always' 或 'overflow'
});

// 带动画
graph.fitView(
  { direction: 'both', padding: 50 },
  {
    duration: 500,
    easing: 'ease-in-out'
  }
);
```

### 10.2 fitCenter - 居中显示

```typescript
// 将整个图表居中显示
graph.fitCenter({
  duration: 500,
  easing: 'ease-in-out'
});
```

### 10.3 focusElement - 聚焦元素

```typescript
// 动画聚焦单个元素到视口中心
graph.focusElement('node-1', {
  duration: 500,
  easing: 'ease-in-out',
  padding: [20, 20, 20, 20]
});
```

### 10.4 zoomTo - 缩放到指定倍率

```typescript
// 缩放到 1.5 倍
graph.zoomTo(1.5);

// 缩放并动画
graph.zoomTo(1.5, [500, 300], {
  duration: 500,
  easing: 'ease-in-out'
});
```

### 10.5 其他视口方法

```typescript
// 获取当前缩放倍率
const zoomLevel = graph.getZoom();

// 获取视口变换矩阵
const matrix = graph.getTransform();

// 设置视口变换
graph.setTransform([scaleX, 0, 0, scaleY, translateX, translateY]);

// 重置视口
graph.resetTransform({
  duration: 500
});
```

---

## 11. 渲染和刷新

### 11.1 基础渲染

```typescript
// 初始渲染
graph.render();

// 重新渲染 (重新计算布局)
graph.layout();

// 只更新视觉 (不重新计算布局)
graph.draw();
```

### 11.2 性能优化

```typescript
// 批量更新 (单次重绘)
graph.batch(() => {
  graph.addNodeData([...]);
  graph.addEdgeData([...]);
  graph.updateNodeData([...]);
});

// 禁用自动渲染
graph.setAutoPin(false);  // 某些操作后需手动 render()

// 启用自动渲染
graph.setAutoPin(true);
```

---

## 12. 自定义节点类型

### 12.1 扩展 BaseNode

```typescript
import { BaseNode } from '@antv/g6';

class CustomRectNode extends BaseNode {
  // 绘制主形状
  drawKeyShape(attributes, container) {
    // 获取节点尺寸
    const { width = 200, height = 100 } = attributes;

    return container.appendChild(
      new GRect({
        style: {
          x: -width / 2,
          y: -height / 2,
          width: width,
          height: height,
          fill: '#fff',
          stroke: '#1890ff',
          lineWidth: 2,
          radius: [4, 4, 4, 4]
        }
      })
    );
  }

  // 绘制标签
  drawLabelShape(attributes, container) {
    const { label = '' } = attributes;
    if (label) {
      return container.appendChild(
        new GText({
          style: {
            text: label,
            textAlign: 'center',
            fontSize: 12,
            fill: '#000'
          }
        })
      );
    }
  }

  // 绘制端口
  drawPortShapes(attributes, container) {
    const ports = attributes.ports || [];
    ports.forEach(port => {
      // 计算端口位置
      const [px, py] = this.getPortXY(port);

      container.appendChild(
        new GCircle({
          style: {
            cx: px,
            cy: py,
            r: port.r || 4,
            fill: port.fill || '#fff',
            stroke: port.stroke || '#1890ff'
          }
        })
      );
    });
  }
}

// 注册自定义节点
graph.registry.register('node', 'custom-rect', CustomRectNode);

// 使用
graph.addNodeData([
  {
    id: 'custom-1',
    label: 'Custom Node',
    type: 'custom-rect'
  }
]);
```

---

## 13. 完整工作流示例

### 13.1 构建带有 4 向端口的流程图

```typescript
import { Graph } from '@antv/g6';

// 创建图实例
const graph = new Graph({
  container: 'canvas',
  width: 1200,
  height: 800,

  layout: {
    type: 'dagre',
    direction: 'LR',
    ranksep: 60,
    nodesep: 40
  },

  node: {
    type: 'rect',
    style: {
      width: 200,
      height: 100,
      fill: '#fff',
      stroke: '#1890ff',
      lineWidth: 2,
      radius: [4, 4, 4, 4]
    },
    state: {
      selected: {
        stroke: '#ff9800',
        lineWidth: 3,
        shadowColor: '#ff9800',
        shadowBlur: 8
      },
      highlight: {
        fill: '#fff9c4',
        stroke: '#ff6f00'
      }
    }
  },

  edge: {
    type: 'cubic-horizontal',
    style: {
      stroke: '#999',
      lineWidth: 2,
      endArrow: {
        path: 'M 0,0 L 8,-4 L 8,4 Z',
        fill: '#999'
      }
    }
  },

  behaviors: [
    'drag-canvas',
    'scroll-canvas',
    'zoom-canvas',
    'click-select',
    'drag-element',
    'hover-activate',
    'focus-element'
  ]
});

// 创建含端口的节点
const nodes = [
  {
    id: 'start',
    label: '开始',
    type: 'rect',
    data: {
      ports: [
        { key: 'out', placement: 'right', r: 5, fill: '#fff', stroke: '#1890ff' }
      ]
    }
  },
  {
    id: 'process1',
    label: '处理步骤 1',
    type: 'rect',
    data: {
      ports: [
        { key: 'in', placement: 'left', r: 5, fill: '#fff', stroke: '#1890ff' },
        { key: 'out', placement: 'right', r: 5, fill: '#fff', stroke: '#1890ff' }
      ]
    }
  },
  {
    id: 'process2',
    label: '处理步骤 2',
    type: 'rect',
    data: {
      ports: [
        { key: 'in', placement: 'left', r: 5, fill: '#fff', stroke: '#1890ff' },
        { key: 'out', placement: 'right', r: 5, fill: '#fff', stroke: '#1890ff' }
      ]
    }
  },
  {
    id: 'end',
    label: '结束',
    type: 'rect',
    data: {
      ports: [
        { key: 'in', placement: 'left', r: 5, fill: '#fff', stroke: '#1890ff' }
      ]
    }
  }
];

// 创建边
const edges = [
  {
    source: 'start',
    target: 'process1',
    data: {
      sourcePort: 'out',
      targetPort: 'in'
    }
  },
  {
    source: 'process1',
    target: 'process2',
    data: {
      sourcePort: 'out',
      targetPort: 'in'
    }
  },
  {
    source: 'process2',
    target: 'end',
    data: {
      sourcePort: 'out',
      targetPort: 'in'
    }
  }
];

// 设置数据并渲染
graph.setData({ nodes, edges });
graph.render();

// 事件处理
graph.on('node:click', (event) => {
  const { itemId } = event;
  console.log('Clicked:', itemId);

  // 突出显示路径
  const allNodes = graph.getNodesData();
  allNodes.forEach(node => {
    graph.clearElementState(node.id, 'highlight');
  });
  graph.setElementState(itemId, 'highlight');
});

// 清理
window.addEventListener('beforeunload', () => {
  graph.destroy();
});
```

---

## 14. API 速查表

| 操作 | 方法 | 说明 |
|------|------|------|
| **创建** | `new Graph(options)` | 创建图实例 |
| **渲染** | `graph.render()` | 初次渲染 |
| **重绘** | `graph.draw()` | 更新视觉 |
| **布局** | `graph.layout()` | 重新计算布局 |
| **添加节点** | `graph.addNodeData(array)` | 添加一个或多个节点 |
| **移除节点** | `graph.removeNodeData(ids)` | 移除一个或多个节点 |
| **更新节点** | `graph.updateNodeData(array)` | 更新节点属性 |
| **添加边** | `graph.addEdgeData(array)` | 添加一个或多个边 |
| **移除边** | `graph.removeEdgeData(ids)` | 移除一个或多个边 |
| **更新边** | `graph.updateEdgeData(array)` | 更新边属性 |
| **获取节点** | `graph.getNodes()` | 获取所有节点对象 |
| **获取单节点** | `graph.getNodeById(id)` | 获取指定节点 |
| **获取边** | `graph.getEdges()` | 获取所有边对象 |
| **获取单边** | `graph.getEdgeById(id)` | 获取指定边 |
| **状态设置** | `graph.setElementState(id, state)` | 设置元素状态 |
| **状态清除** | `graph.clearElementState(id, state)` | 清除元素状态 |
| **状态查询** | `graph.findIdByState(state)` | 查找具有状态的元素 |
| **缩放视图** | `graph.fitView(options, animation)` | 缩放至内容 |
| **居中** | `graph.fitCenter(animation)` | 居中显示 |
| **聚焦** | `graph.focusElement(id, options)` | 聚焦单个元素 |
| **缩放** | `graph.zoomTo(ratio)` | 缩放到指定倍率 |
| **事件绑定** | `graph.on(event, callback)` | 绑定事件 |
| **事件触发** | `graph.emit(event, data)` | 触发事件 |
| **批量操作** | `graph.batch(callback)` | 批量操作 (单次重绘) |
| **销毁** | `graph.destroy()` | 清理资源 |

---

## 15. 参考资源

- **官方文档**: https://g6.antv.antgroup.com/
- **GitHub 仓库**: https://github.com/antvis/g6 (v5 分支)
- **NPM 包**: https://www.npmjs.com/package/@antv/g6
- **版本**: 5.0.51 (稳定), 5.1.0-beta.1 (测试)
- **许可证**: MIT

---

## 16. 常见问题

### Q: 如何实现节点之间的碰撞检测?
**A**: 在 `node:dragmove` 事件中检查节点边界是否重叠，如需要可阻止默认行为。

### Q: 如何自定义箭头样式?
**A**: 通过 `endArrow` 对象配置，指定 SVG 路径、填充色、边框色。

### Q: 端口是否自动连接?
**A**: G6 根据 `sourcePort` 和 `targetPort` 自动计算连接点，无需手动计算。

### Q: 如何实现撤销重做?
**A**: 使用 History 插件，自动追踪所有修改操作。

### Q: Dagre 布局能否自定义节点排列?
**A**: 可以通过扩展 BaseLayout 实现完全自定义布局逻辑。

---

**文档维护者**: CodeXray 项目
**最后更新**: 2026 年 3 月 14 日
