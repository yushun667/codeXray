# CodeXray UI API 参考文档

**项目**: CodeXray（C++ 代码可视化分析）
**UI 技术栈**: React 18 + TypeScript + Vite
**关键库**: ReactFlow v11.11.4 + Dagre v0.8.5
**最后更新**: 2026-03-14

---

## 目录

1. [ReactFlow v11 完整 API](#1-reactflow-v11-完整-api)
2. [Dagre v0.8 布局 API](#2-dagre-v08-布局-api)
3. [CodeXray 集成模式](#3-codexray-集成模式)
4. [类型定义](#4-类型定义)
5. [常见问题](#5-常见问题)

---

## 1. ReactFlow v11 完整 API

### 1.1 导入路径与核心组件

```typescript
import {
  // 主容器
  ReactFlow,

  // UI 控件
  Controls,
  Background,
  BackgroundVariant,

  // 节点/边基础
  Handle,
  Position,
  MarkerType,

  // 工具函数
  applyNodeChanges,
  applyEdgeChanges,
  SelectionMode,
  useReactFlow,
  useNodes,
  useEdges,
  useOnSelectionChange,
  useNodeId,

  // 类型定义
  type Node,
  type Edge,
  type NodeChange,
  type EdgeChange,
  type NodeProps,
  type EdgeProps,
  type Connection,
  type DefaultEdgeOptions,
  type GetNodeCore,
} from 'reactflow';

// 样式（必须导入）
import 'reactflow/dist/style.css';
```

### 1.2 节点类型定义

#### Node<T = any>

```typescript
interface Node<T = any> {
  /** 节点唯一标识 */
  id: string;

  /** 节点业务数据 */
  data: T;

  /** 位置（像素，左上角） */
  position: { x: number; y: number };

  /** 节点类型，对应 nodeTypes 键（默认 'default'） */
  type?: string;

  /** 是否被选中 */
  selected?: boolean;

  /** 是否正在被拖拽 */
  dragging?: boolean;

  /** 边的起点位置（枚举：Top|Right|Bottom|Left） */
  sourcePosition?: Position;

  /** 边的终点位置 */
  targetPosition?: Position;

  /** 是否可连接（有 Handle） */
  connectable?: boolean;

  /** 是否可被选中 */
  selectable?: boolean;

  /** 是否可被删除 */
  deletable?: boolean;

  /** 是否可被拖拽 */
  draggable?: boolean;

  /** 内联样式 */
  style?: React.CSSProperties;

  /** CSS 类名 */
  className?: string;

  /** Z-index */
  zIndex?: number;

  // 其他内部属性...
}
```

#### CodeXray FlowNodeData 示例

```typescript
interface FlowNodeData {
  label: string;              // 节点显示名称（支持 \n 换行）
  definition?: {
    file: string;              // 源文件路径
    line: number;              // 行号
    column?: number;            // 列号
  };
  definition_range?: {          // 定义范围
    start: { line: number; column: number };
    end: { line: number; column: number };
  };
  isRoot?: boolean;            // 是否为根节点
  // 其他自定义字段...
}

// 使用示例
const nodes: Node<FlowNodeData>[] = [
  {
    id: 'func1',
    data: { label: 'main', definition: { file: 'main.cpp', line: 10 }, isRoot: true },
    position: { x: 0, y: 0 },
    type: 'graphNode',
  }
];
```

### 1.3 边类型定义

#### Edge

```typescript
interface Edge {
  /** 边唯一标识 */
  id: string;

  /** 源节点 ID */
  source: string;

  /** 目标节点 ID */
  target: string;

  /** 边的显示类型 */
  type?: 'default' | 'straight' | 'step' | 'smoothstep' | 'simplebezier';

  /** 源节点的 Handle 键（用于多个出口） */
  sourceHandle?: string;

  /** 目标节点的 Handle 键（用于多个入口） */
  targetHandle?: string;

  /** 是否动画 */
  animated?: boolean;

  /** 是否被选中 */
  selected?: boolean;

  /** 内联样式 */
  style?: React.CSSProperties;

  /** CSS 类名 */
  className?: string;

  /** 边标签 */
  label?: string | React.ReactNode;

  /** 标签样式 */
  labelStyle?: React.CSSProperties;

  /** 边的起点箭头 */
  markerStart?: {
    type: MarkerType;
    // ... 其他属性
  };

  /** 边的终点箭头 */
  markerEnd?: {
    type: MarkerType;         // 'arrow' | 'arrowclosed'
    width: number;             // 箭头宽度
    height: number;            // 箭头高度
    color: string;             // 箭头颜色
  };

  /** 路径选项（用于 smoothstep） */
  pathOptions?: {
    borderRadius: number;      // 圆角半径
  };

  /** 边的业务数据 */
  data?: Record<string, unknown>;

  /** 是否可被删除 */
  deletable?: boolean;
}

// CodeXray 示例
const edges: Edge[] = [
  {
    id: 'e1-2',
    source: 'func1',
    target: 'func2',
    type: 'smoothstep',
    pathOptions: { borderRadius: 8 },
    data: { edge_type: 'direct', call_site: { line: 25, column: 5 } },
  }
];
```

### 1.4 变更事件类型

#### NodeChange / EdgeChange

```typescript
type NodeChange =
  | { type: 'select'; id: string; selected: boolean }
  | { type: 'position'; id: string; position?: XYPosition; positionAbsolute?: XYPosition }
  | { type: 'remove'; id: string }
  | { type: 'replace'; item: Node }
  | { type: 'dimensions'; id: string; dimensions: Dimensions }
  // ... 其他类型

type EdgeChange =
  | { type: 'select'; id: string; selected: boolean }
  | { type: 'remove'; id: string }
  | { type: 'replace'; item: Edge }
  // ... 其他类型
```

### 1.5 ReactFlow 主容器 Props

```typescript
interface ReactFlowProps {
  // 数据
  nodes: Node[];
  edges: Edge[];

  // 节点/边类型定义
  nodeTypes?: Record<string, React.ComponentType<NodeProps>>;
  edgeTypes?: Record<string, React.ComponentType<EdgeProps>>;

  // 回调
  onNodesChange?: (changes: NodeChange[]) => void;
  onEdgesChange?: (changes: EdgeChange[]) => void;
  onNodeClick?: (event: React.MouseEvent, node: Node) => void;
  onNodeDoubleClick?: (event: React.MouseEvent, node: Node) => void;
  onNodeContextMenu?: (event: React.MouseEvent, node: Node) => void;
  onNodeDragStart?: (event: React.MouseEvent, node: Node, nodes: Node[]) => void;
  onNodeDrag?: (event: React.MouseEvent, node: Node, nodes: Node[]) => void;
  onNodeDragStop?: (event: React.MouseEvent, node: Node, nodes: Node[]) => void;
  onEdgeClick?: (event: React.MouseEvent, edge: Edge) => void;
  onConnect?: (connection: Connection) => void;
  onPaneContextMenu?: (event: React.MouseEvent | MouseEvent) => void;
  onSelectionContextMenu?: (event: React.MouseEvent, nodes: Node[]) => void;

  // 交互配置
  panOnDrag?: boolean | number[];        // [1,2] = 中键+右键
  panOnScroll?: boolean;
  panOnScrollMode?: 'free' | 'vertical' | 'horizontal';
  zoomOnScroll?: boolean;
  zoomOnPinch?: boolean;
  selectionOnDrag?: boolean;             // 框选
  selectionMode?: SelectionMode;         // 'partial' | 'full'

  // 节点/边状态
  nodesDraggable?: boolean;
  nodesConnectable?: boolean;
  nodesSelectable?: boolean;

  // 缩放与平移
  fitView?: boolean;
  fitViewOptions?: {
    padding: number;
    includeHiddenNodes?: boolean;
    minZoom?: number;
    maxZoom?: number;
  };
  minZoom?: number;
  maxZoom?: number;

  // 快捷键
  deleteKeyCode?: string[];              // ['Delete', 'Backspace']
  multiSelectionKeyCode?: string;        // 'Shift' | 'Meta'

  // 默认配置
  defaultEdgeOptions?: Partial<Edge>;

  // 其他
  proOptions?: { hideAttribution: boolean };
  // ... 更多配置
}
```

**CodeXray GraphCore 示例:**

```typescript
<ReactFlow
  nodes={nodes}
  edges={edges}
  nodeTypes={{ graphNode: GraphNode }}
  onNodesChange={onNodesChange}
  onEdgesChange={onEdgesChange}
  onNodeDoubleClick={onNodeDoubleClick}
  onNodeDragStart={onNodeDragStart}
  onNodeDrag={onNodeDrag}
  onNodeContextMenu={onNodeContextMenuHandler}
  onSelectionContextMenu={onSelectionContextMenuHandler}
  onPaneContextMenu={onPaneContextMenu}
  defaultEdgeOptions={{
    type: 'smoothstep',
    pathOptions: { borderRadius: 8 },
    markerEnd: {
      type: MarkerType.ArrowClosed,
      width: 16,
      height: 16,
      color: 'rgba(255, 255, 255, 0.55)',
    },
  }}
  panOnDrag={[1, 2]}        // 中键+右键平移
  selectionOnDrag           // 框选
  selectionMode={SelectionMode.Partial}
  nodesDraggable
  nodesConnectable={false}
  fitView
  fitViewOptions={{ padding: 0.2 }}
  minZoom={0.05}
  maxZoom={2}
  deleteKeyCode={['Delete', 'Backspace']}
  proOptions={{ hideAttribution: true }}
>
  <Controls />
  <Background variant={BackgroundVariant.Dots} gap={20} size={1} />
</ReactFlow>
```

### 1.6 Handle（节点端口）

```typescript
import { Handle, Position } from 'reactflow';

// 在自定义节点组件内使用
export function GraphNode({ data }: NodeProps) {
  return (
    <>
      {/* 进入端口（左侧） */}
      <Handle
        type="target"               // 'target' = 边的终点，'source' = 边的起点
        position={Position.Left}    // Top | Right | Bottom | Left
        // 可选配置：
        isConnectable={true}
        style={{ background: '#555' }}
      />

      {/* 节点内容 */}
      <div>My Node</div>

      {/* 离开端口（右侧） */}
      <Handle type="source" position={Position.Right} />
    </>
  );
}
```

### 1.7 事件处理模式

#### 节点点击（跳转到定义）

```typescript
const onNodeDoubleClick = useCallback((_: React.MouseEvent, node: Node<FlowNodeData>) => {
  const def = node.data?.definition;
  if (def?.file != null && def?.line != null) {
    postToHost({
      action: 'gotoSymbol',
      file: def.file,
      line: def.line,
      column: def.column ?? 1,
    });
  }
}, []);
```

#### 节点右键菜单

```typescript
const onNodeContextMenuHandler = useCallback(
  (ev: React.MouseEvent, node: Node<FlowNodeData>) => {
    ev.preventDefault();
    onNodeContextMenu?.(node, ev);  // 传给父组件处理
  },
  [onNodeContextMenu]
);
```

#### 拖拽事件

```typescript
const onNodeDragStart = useCallback(() => {
  onBeforeDrag?.();  // 保存快照用于撤销
}, [onBeforeDrag]);

const onNodeDrag = useCallback(
  (_: React.MouseEvent, draggedNode: Node<FlowNodeData>) => {
    setNodes((curNodes) => {
      const updated = curNodes.map((n) =>
        n.id === draggedNode.id ? { ...n, position: draggedNode.position } : n
      );
      // 碰撞检测
      const pushed = pushOverlapping(draggedNode.id, updated);
      return pushed ?? updated;
    });
  },
  [setNodes]
);
```

### 1.8 工具函数

#### applyNodeChanges / applyEdgeChanges

```typescript
import { applyNodeChanges, applyEdgeChanges } from 'reactflow';

const onNodesChange = (changes: NodeChange[]) => {
  setNodes((nds) => applyNodeChanges(changes, nds));
};

const onEdgesChange = (changes: EdgeChange[]) => {
  setEdges((eds) => applyEdgeChanges(changes, eds));
};
```

#### useReactFlow Hook

```typescript
import { useReactFlow } from 'reactflow';

function MyComponent() {
  const {
    getNode,              // (id: string) => Node | undefined
    getNodes,             // () => Node[]
    getEdge,              // (id: string) => Edge | undefined
    getEdges,             // () => Edge[]
    setNodes,
    setEdges,
    addNodes,             // (nodes: Node[]) => void
    addEdges,             // (edges: Edge[]) => void
    deleteElements,       // (items: {nodes?: Node[], edges?: Edge[]}) => void
    fitView,              // (options?) => Promise<boolean>
    fitCenter,
    focusElement,         // (nodeId: string, options?) => void
    zoomIn, zoomOut,
    getViewport,
    setViewport,
  } = useReactFlow();

  // 示例：删除选中节点
  const deleteSelected = () => {
    deleteElements({ nodes: getNodes().filter(n => n.selected) });
  };
}
```

### 1.9 Hooks

#### useNodes / useEdges

```typescript
import { useNodes, useEdges } from 'reactflow';

function MyComponent() {
  const nodes = useNodes();  // Node[]
  const edges = useEdges();  // Edge[]

  // 实时响应节点/边变化
  useEffect(() => {
    console.log('Nodes updated:', nodes);
  }, [nodes]);
}
```

#### useOnSelectionChange

```typescript
import { useOnSelectionChange } from 'reactflow';

function MyComponent() {
  useOnSelectionChange({
    onChange: ({ nodes, edges }) => {
      console.log('Selected nodes:', nodes);
      console.log('Selected edges:', edges);
    }
  });
}
```

#### useNodeId

```typescript
import { useNodeId } from 'reactflow';

// 仅在自定义节点组件内使用
export function GraphNode() {
  const nodeId = useNodeId();  // string
  return <div>Node {nodeId}</div>;
}
```

### 1.10 撤销/重做模式（无内置插件）

ReactFlow v11 **不包含内置的撤销/重做机制**。需手动实现：

```typescript
import { useCallback, useRef } from 'react';

interface Snapshot {
  nodes: Node[];
  edges: Edge[];
}

function useGraphHistory() {
  const historyRef = useRef<Snapshot[]>([]);
  const pointerRef = useRef(0);

  const saveSnapshot = useCallback((nodes: Node[], edges: Edge[]) => {
    // 清空重做栈
    historyRef.current = historyRef.current.slice(0, pointerRef.current + 1);
    historyRef.current.push({ nodes, edges });
    pointerRef.current++;
  }, []);

  const undo = useCallback((): Snapshot | null => {
    if (pointerRef.current <= 0) return null;
    pointerRef.current--;
    return historyRef.current[pointerRef.current];
  }, []);

  const redo = useCallback((): Snapshot | null => {
    if (pointerRef.current >= historyRef.current.length - 1) return null;
    pointerRef.current++;
    return historyRef.current[pointerRef.current];
  }, []);

  return {
    saveSnapshot,
    undo,
    redo,
    canUndo: pointerRef.current > 0,
    canRedo: pointerRef.current < historyRef.current.length - 1,
  };
}

// 在 GraphPage 使用
function GraphPage() {
  const [nodes, setNodes] = useState<Node[]>([]);
  const [edges, setEdges] = useState<Edge[]>([]);
  const history = useGraphHistory();

  // 拖拽前保存快照
  const onBeforeDrag = () => {
    history.saveSnapshot(nodes, edges);
  };

  // 键盘快捷键
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const mod = e.ctrlKey || e.metaKey;
      if (!mod) return;

      if (e.key === 'z' && !e.shiftKey) {
        e.preventDefault();
        const snap = history.undo();
        if (snap) {
          setNodes(snap.nodes);
          setEdges(snap.edges);
        }
      } else if ((e.key === 'z' && e.shiftKey) || e.key === 'y') {
        e.preventDefault();
        const snap = history.redo();
        if (snap) {
          setNodes(snap.nodes);
          setEdges(snap.edges);
        }
      }
    };

    document.addEventListener('keydown', handler);
    return () => document.removeEventListener('keydown', handler);
  }, [history, nodes, edges]);

  return (
    <GraphCore
      nodes={nodes}
      edges={edges}
      setNodes={setNodes}
      setEdges={setEdges}
      onBeforeDrag={onBeforeDrag}
      onUndo={() => {
        const snap = history.undo();
        if (snap) { setNodes(snap.nodes); setEdges(snap.edges); }
      }}
      onRedo={() => {
        const snap = history.redo();
        if (snap) { setNodes(snap.nodes); setEdges(snap.edges); }
      }}
    />
  );
}
```

---

## 2. Dagre v0.8 布局 API

### 2.1 导入

```typescript
import dagre from 'dagre';

// 或 CommonJS
const dagre = require('dagre');
```

### 2.2 图对象创建

```typescript
const g = new dagre.graphlib.Graph();

// 配置图的全局选项
g.setGraph({
  rankdir: 'LR' | 'RL' | 'TB' | 'BT',    // LR = 左→右，TB = 上→下
  align: 'DL' | 'DR' | 'UL' | 'UR',      // 可选：节点对齐方式
  nodesep: number,                        // 同层节点间距（rankdir=LR 时为垂直）
  ranksep: number,                        // 层间距（rankdir=LR 时为水平）
  marginx: number,                        // 左右边距
  marginy: number,                        // 上下边距
  acyclicer: 'greedy' | 'dfs',           // 可选：如何处理循环
  ranker: 'network-simplex' | 'tight-tree' | 'longest-path',  // 排名算法
});

// 设置边的默认标签（必须调用，即使为空）
g.setDefaultEdgeLabel(() => ({}));
```

**CodeXray 示例:**

```typescript
const g = new dagre.graphlib.Graph();
g.setGraph({
  rankdir: 'LR',           // 左→右（调用者在左，被调用者在右）
  nodesep: 40,             // 同层节点垂直间距
  ranksep: 120,            // 层水平间距
  marginx: 20,
  marginy: 20,
});
g.setDefaultEdgeLabel(() => ({}));
```

### 2.3 添加节点

```typescript
g.setNode('nodeId', {
  width: number,    // 节点宽度（必需）
  height: number,   // 节点高度（必需）
  rank?: number,    // 可选：强制指定层级（用于根居中布局）
  label?: string,   // 可选：节点标签（仅用于输出）
  // 其他自定义属性...
});

// CodeXray 中的用法
nodes.forEach((node) => {
  const nodeData: { width: number; height: number; rank?: number } = {
    width: NODE_WIDTH,    // 280
    height: NODE_HEIGHT,  // 60
  };
  if (directedRanks) {
    const rank = directedRanks.get(node.id);
    if (rank != null) {
      nodeData.rank = rank + rankOffset;
    }
  }
  g.setNode(node.id, nodeData);
});
```

### 2.4 添加边

```typescript
// 最简形式
g.setEdge('sourceId', 'targetId');

// 带标签
g.setEdge('sourceId', 'targetId', { label: 'edgeLabel' });

// 自定义边属性
g.setEdge('sourceId', 'targetId', {
  label: 'call',
  weight: 1,      // 权重（用于最短路径）
  // 其他属性...
});
```

**CodeXray 中的用法:**

```typescript
edges.forEach((edge) => {
  // 防止 dagre 抛出错误：只添加两端节点均存在的边
  if (g.hasNode(edge.source) && g.hasNode(edge.target)) {
    g.setEdge(edge.source, edge.target);
  }
});
```

### 2.5 执行布局

```typescript
dagre.layout(g);
```

### 2.6 获取布局结果

```typescript
// 获取单个节点的位置
const node = g.node('nodeId');
// {
//   x: number,          // 中心 x 坐标
//   y: number,          // 中心 y 坐标
//   width: number,      // 设置时的宽度
//   height: number,     // 设置时的高度
//   rank?: number,      // 计算出的层级
// }

// 获取所有节点 ID
const nodeIds = g.nodes();  // string[]

// 获取边
const points = g.edge('sourceId', 'targetId').points;  // Array<{x, y}>（路径点）
```

**CodeXray 中的用法:**

```typescript
return nodes.map((node) => {
  const n = g.node(node.id);
  if (n == null) return node;
  return {
    ...node,
    position: {
      x: n.x - NODE_WIDTH / 2,    // 转换为左上角坐标
      y: n.y - NODE_HEIGHT / 2,
    },
    sourcePosition: Position.Right,   // 边从右侧出
    targetPosition: Position.Left,    // 边从左侧进
  };
});
```

### 2.7 操作方法

```typescript
// 检查节点/边是否存在
g.hasNode('nodeId');    // boolean
g.hasEdge('src', 'tgt'); // boolean

// 移除节点/边
g.removeNode('nodeId');
g.removeEdge('src', 'tgt');

// 获取节点/边列表
g.nodes();              // string[]
g.edges();              // Array<{v, w}>

// 获取邻接表
g.successors('nodeId'); // 后继节点 ID[]
g.predecessors('nodeId'); // 前驱节点 ID[]

// 访问者（BFS/DFS）
g.nodeCount();          // number
g.edgeCount();          // number
```

### 2.8 重要注意

- **Dagre 是静态布局** — 计算完成后节点位置不再变化
- **不支持运行时更新节点** — 需要重新调用 `dagre.layout()`
- **没有内置碰撞检测** — React Flow 中手动拖拽时需要自己检查（CodeXray 使用 `pushOverlapping()`）
- **秩约束强制** — 设置 `rank` 属性可以强制节点在特定层（CodeXray 用此实现根居中布局）

---

## 3. CodeXray 集成模式

### 3.1 数据流向

```
主仓库 (Extension Host)
  │
  ├─→ Webview 加载 HTML
  │
  ├─→ 发送 postMessage: { type: 'graphData', data: { nodes, edges } }
  │
  └─→ 接收 postMessage: { action: 'gotoSymbol', file, line, column }

GraphPage (React 组件)
  │
  ├─→ 接收 nodes/edges 数据
  │
  ├─→ 调用 getLayoutedElements(nodes, edges, graphType, rootNodeIds)
  │
  ├─→ 保存快照供撤销
  │
  └─→ 传给 GraphCore 组件

GraphCore (ReactFlow 容器)
  │
  ├─→ 显示图
  │
  ├─→ 处理用户交互（点击、拖拽、右键等）
  │
  └─→ 发送 postMessage 给主仓库

graphLayout.ts (纯函数)
  │
  ├─→ computeDirectedRanks() — 计算有向层级
  │
  ├─→ getLayoutedElements() — 执行 Dagre 布局
  │
  └─→ pushOverlapping() — 拖拽时碰撞检测
```

### 3.2 自定义节点（GraphNode）

**文件**: `resources/ui/src/visualization/GraphNode.tsx`

```typescript
import { Handle, Position, type NodeProps } from 'reactflow';
import type { FlowNodeData } from './adapters/callGraph';

export function GraphNode({ data, selected }: NodeProps) {
  const flowData = data as FlowNodeData;
  const label = String(flowData?.label ?? '');
  const isRoot = flowData?.isRoot === true;

  // 计算高度（根据行数动态调整）
  const lines = label.split('\n').filter(Boolean);
  const displayLines = lines.length > 0 ? lines : [label || ''];

  const measuredHeight = /* 计算实际高度 */;

  return (
    <>
      {/* 左侧入口（边的目标） */}
      <Handle type="target" position={Position.Left} />

      {/* 节点内容 */}
      <div className={`graph-node-inner${selected ? ' graph-node-selected' : ''}${isRoot ? ' graph-node-root' : ''}`}
           style={{
             minWidth: 200,
             maxWidth: 360,
             height: measuredHeight,
             paddingTop: 8,
             paddingBottom: 8,
             paddingLeft: 12,
             paddingRight: 12,
             lineHeight: 1.4,
             textAlign: 'center',
             whiteSpace: 'normal',
             wordBreak: 'break-word',
             fontSize: 'var(--vscode-font-size, 13px)',
             fontFamily: 'var(--vscode-font-family, monospace)',
             cursor: 'grab',
           }}
           title={label}>
        {displayLines.map((line, i) => (
          <div key={i}>{line}</div>
        ))}
      </div>

      {/* 右侧出口（边的源） */}
      <Handle type="source" position={Position.Right} />
    </>
  );
}
```

### 3.3 布局与合并（graphLayout.ts）

```typescript
import dagre from 'dagre';
import { Position, type Node, type Edge } from 'reactflow';

const NODE_WIDTH = 280;
const NODE_HEIGHT = 60;
const RANK_SEP = 120;
const NODE_SEP = 40;

/**
 * 计算有向层级（用于根居中布局）
 * - 负层级：调用者（callers）
 * - 0：根节点（root）
 * - 正层级：被调用者（callees）
 */
function computeDirectedRanks(
  nodeIds: Set<string>,
  edges: Edge[],
  rootIds: Set<string>
): Map<string, number> {
  const ranks = new Map<string, number>();
  const forward = new Map<string, string[]>();  // source → targets
  const backward = new Map<string, string[]>(); // target → sources

  // 构建邻接表
  for (const id of nodeIds) {
    forward.set(id, []);
    backward.set(id, []);
  }
  for (const e of edges) {
    if (nodeIds.has(e.source) && nodeIds.has(e.target)) {
      forward.get(e.source)!.push(e.target);
      backward.get(e.target)!.push(e.source);
    }
  }

  // 初始化根节点
  for (const rid of rootIds) {
    if (nodeIds.has(rid)) ranks.set(rid, 0);
  }

  // 正向 BFS：root → callees（+1, +2, ...）
  const forwardQueue = Array.from(rootIds).filter(r => nodeIds.has(r));
  while (forwardQueue.length > 0) {
    const cur = forwardQueue.shift()!;
    const curRank = ranks.get(cur)!;
    for (const next of forward.get(cur) ?? []) {
      if (!ranks.has(next)) {
        ranks.set(next, curRank + 1);
        forwardQueue.push(next);
      }
    }
  }

  // 反向 BFS：root → callers（-1, -2, ...）
  const backwardQueue = Array.from(rootIds).filter(r => nodeIds.has(r));
  while (backwardQueue.length > 0) {
    const cur = backwardQueue.shift()!;
    const curRank = ranks.get(cur)!;
    for (const prev of backward.get(cur) ?? []) {
      if (!ranks.has(prev)) {
        ranks.set(prev, curRank - 1);
        backwardQueue.push(prev);
      }
    }
  }

  // 未连通节点分配到最右列
  let maxRank = 0;
  for (const r of ranks.values()) {
    if (r > maxRank) maxRank = r;
  }
  for (const id of nodeIds) {
    if (!ranks.has(id)) ranks.set(id, maxRank + 1);
  }

  return ranks;
}

/**
 * 布局主函数
 */
export function getLayoutedElements<T = Record<string, unknown>>(
  nodes: Node<T>[],
  edges: Edge[],
  graphType: string = 'call_graph',
  rootNodeIds?: Set<string>
): Node<T>[] {
  if (nodes.length === 0) return nodes;

  try {
    const g = new dagre.graphlib.Graph();
    g.setGraph({
      rankdir: 'LR',
      nodesep: NODE_SEP,
      ranksep: RANK_SEP,
      marginx: 20,
      marginy: 20,
    });
    g.setDefaultEdgeLabel(() => ({}));

    // 计算有向层级
    const nodeIdSet = new Set(nodes.map(n => n.id));
    const hasRoots = rootNodeIds && rootNodeIds.size > 0;
    let directedRanks: Map<string, number> | null = null;
    let rankOffset = 0;

    if (hasRoots) {
      directedRanks = computeDirectedRanks(nodeIdSet, edges, rootNodeIds);
      let minRank = 0;
      for (const r of directedRanks.values()) {
        if (r < minRank) minRank = r;
      }
      rankOffset = -minRank;  // 偏移使最小层级为 0
    }

    // 添加节点
    nodes.forEach(node => {
      const nodeData: { width: number; height: number; rank?: number } = {
        width: NODE_WIDTH,
        height: NODE_HEIGHT,
      };
      if (directedRanks) {
        const rank = directedRanks.get(node.id);
        if (rank != null) nodeData.rank = rank + rankOffset;
      }
      g.setNode(node.id, nodeData);
    });

    // 添加边
    edges.forEach(edge => {
      if (g.hasNode(edge.source) && g.hasNode(edge.target)) {
        g.setEdge(edge.source, edge.target);
      }
    });

    // 执行布局
    dagre.layout(g);

    // 返回带位置的节点
    return nodes.map(node => {
      const n = g.node(node.id);
      if (n == null) return node;
      return {
        ...node,
        position: {
          x: n.x - NODE_WIDTH / 2,
          y: n.y - NODE_HEIGHT / 2,
        },
        sourcePosition: Position.Right,
        targetPosition: Position.Left,
      };
    });
  } catch {
    // 出错时使用网格后备布局
    return getFallbackLayout(nodes);
  }
}

/**
 * 拖拽碰撞检测
 */
export function pushOverlapping<T>(
  draggedId: string,
  allNodes: Node<T>[]
): Node<T>[] | null {
  const dragged = allNodes.find(n => n.id === draggedId);
  if (!dragged) return null;

  const dLeft = dragged.position.x;
  const dTop = dragged.position.y;
  const dRight = dLeft + NODE_WIDTH;
  const dBottom = dTop + NODE_HEIGHT;

  let changed = false;
  const result = allNodes.map(node => {
    if (node.id === draggedId) return node;

    const nLeft = node.position.x;
    const nTop = node.position.y;
    const nRight = nLeft + NODE_WIDTH;
    const nBottom = nTop + NODE_HEIGHT;

    // 检查重叠
    const overlapX = !(nRight + 20 <= dLeft || nLeft - 20 >= dRight);
    const overlapY = !(nBottom + 20 <= dTop || nTop - 20 >= dBottom);

    if (!overlapX || !overlapY) return node;

    // 计算四个方向的推开距离
    const pushRight = dRight + 20 - nLeft;
    const pushLeft = nRight + 20 - dLeft;
    const pushDown = dBottom + 20 - nTop;
    const pushUp = nBottom + 20 - dTop;

    // 选择最小穿透方向
    const minPush = Math.min(pushRight, pushLeft, pushDown, pushUp);

    changed = true;
    let newX = node.position.x;
    let newY = node.position.y;

    if (minPush === pushRight) {
      newX = dRight + 20;
    } else if (minPush === pushLeft) {
      newX = dLeft - NODE_WIDTH - 20;
    } else if (minPush === pushDown) {
      newY = dBottom + 20;
    } else {
      newY = dTop - NODE_HEIGHT - 20;
    }

    return { ...node, position: { x: newX, y: newY } };
  });

  return changed ? result : null;
}
```

---

## 4. 类型定义

### 4.1 CodeXray 共享类型

**文件**: `resources/ui/src/shared/types.ts`

```typescript
/** 图类型 */
export type GraphType = 'call_graph' | 'class_graph' | 'data_flow' | 'control_flow';

/** 节点数据（流程图） */
export interface FlowNodeData {
  label: string;                    // 显示名称
  usr?: string;                     // 统一符号引用
  definition?: {
    file: string;
    line: number;
    column?: number;
  };
  definition_range?: {
    start: { line: number; column: number };
    end: { line: number; column: number };
  };
  isRoot?: boolean;                 // 是否为根节点
  expanded?: boolean;               // 用于折叠状态
}

/** 消息类型（主仓库 ↔ UI） */
export type GraphToHostMessage =
  | { action: 'gotoSymbol'; file: string; line: number; column: number }
  | { action: 'queryPredecessors'; graphType: GraphType; nodeId: string }
  | { action: 'querySuccessors'; graphType: GraphType; nodeId: string };

export type HostToGraphMessage =
  | { type: 'graphData'; data: { nodes: Node[]; edges: Edge[] } }
  | { type: 'graphAppend'; nodes: Node[]; edges: Edge[] }
  | { type: 'error'; message: string };
```

---

## 5. 常见问题

### Q1: 如何获取当前节点数据？

**答**: ReactFlow v11 不提供 `getNodeData()` 方法。改用 state 查询：

```typescript
const node = nodes.find(n => n.id === 'some-id');
const data = node?.data;
```

### Q2: 如何实现自定义布局？

**答**: 使用 Dagre 或实现自己的布局算法，返回带 `position` 的节点数组，然后通过 `setNodes()` 更新。

### Q3: 如何添加节点/边？

**答**:

```typescript
setNodes(prev => [...prev, { id: 'new-id', data: {...}, position: {x, y} }]);
setEdges(prev => [...prev, { id: 'e-new', source: 'n1', target: 'n2' }]);
```

### Q4: 如何删除选中节点？

**答**:

```typescript
const deleteSelected = () => {
  const selectedNodeIds = new Set(nodes.filter(n => n.selected).map(n => n.id));
  setNodes(prev => prev.filter(n => !selectedNodeIds.has(n.id)));
  setEdges(prev => prev.filter(e => !selectedNodeIds.has(e.source) && !selectedNodeIds.has(e.target)));
};
```

### Q5: 为什么拖拽后节点会重叠？

**答**: Dagre 只计算一次布局，不会动态调整。CodeXray 使用 `pushOverlapping()` 在 `onNodeDrag` 中实时检测碰撞并推开节点。

### Q6: 如何实现 Undo/Redo？

**答**: ReactFlow v11 不包含内置支持。参考 1.10 节的手动实现方案（快照 + 键盘快捷键）。

### Q7: Handle 的 `type="target"` 和 `type="source"` 有什么区别？

**答**:
- `target`: 边的 **终点**（入口）
- `source`: 边的 **起点**（出口）

对于调用图，通常左侧为 `target`（被调用者的入口），右侧为 `source`（调用者的出口）。

### Q8: 如何自定义边样式？

**答**:

```typescript
const edges: Edge[] = [
  {
    id: 'e1',
    source: 'n1',
    target: 'n2',
    type: 'smoothstep',
    pathOptions: { borderRadius: 8 },
    style: { stroke: '#ff0000', strokeWidth: 2 },
    markerEnd: { type: MarkerType.ArrowClosed, color: '#ff0000' },
  }
];
```

### Q9: Dagre 的 `rank` 属性有什么用？

**答**: 强制节点在指定的层级（rank）中。CodeXray 用它实现根居中布局（根 rank=0，callees rank>0，callers rank<0）。

### Q10: 为什么 Dagre 布局后节点坐标需要调整？

**答**: Dagre 返回的 `(x, y)` 是节点 **中心** 坐标，React Flow 需要 **左上角** 坐标：

```typescript
position = {
  x: n.x - NODE_WIDTH / 2,
  y: n.y - NODE_HEIGHT / 2,
}
```

---

## 参考链接

- **ReactFlow 官方文档**: https://reactflow.dev
- **Dagre GitHub**: https://github.com/dagrejs/dagre
- **CodeXray 项目**: `/Volumes/SSD_1T/develop/CodeXray`
- **GraphCore 源代码**: `resources/ui/src/visualization/GraphCore.tsx`
- **布局实现**: `resources/ui/src/visualization/graphLayout.ts`
- **自定义节点**: `resources/ui/src/visualization/GraphNode.tsx`
