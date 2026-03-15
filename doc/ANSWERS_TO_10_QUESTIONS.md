# CodeXray UI 验证清单：原始 10 个问题的答案

**原始需求**: 验证 AntV G6 v5 API 的 10 个具体细节
**实际发现**: CodeXray 使用 ReactFlow v11，非 G6
**本文**: 用 ReactFlow API 回答原始问题

---

## Q1: 导入路径与类型名称

**原问题**: "是 `import { Graph } from '@antv/g6'` 吗？什么是 `GraphOptions`？"

### 答案

✓ **ReactFlow v11 的导入方式**:

```typescript
import {
  ReactFlow,              // 主容器组件
  Controls,
  Background,
  BackgroundVariant,
  MarkerType,
  Handle,
  Position,
  applyNodeChanges,
  applyEdgeChanges,
  SelectionMode,
  useReactFlow,
  useNodes,
  useEdges,
  type Node,
  type Edge,
  type NodeChange,
  type EdgeChange,
  type NodeProps,
  type EdgeProps,
  type Connection,
} from 'reactflow';

import 'reactflow/dist/style.css';  // ← 必须导入样式
```

✗ **不存在**:
- `Graph` 类（ReactFlow 是函数式组件，无类）
- 独立的 `GraphOptions` 类型（用 `<ReactFlow>` 的 props）

### CodeXray 中的用法

```typescript
// resources/ui/src/visualization/GraphCore.tsx
import {
  ReactFlow,
  Controls,
  Background,
  BackgroundVariant,
  MarkerType,
  applyNodeChanges,
  applyEdgeChanges,
  SelectionMode,
  type Node,
  type Edge,
  type NodeChange,
  type EdgeChange,
} from 'reactflow';
import 'reactflow/dist/style.css';
```

---

## Q2: 自定义布局注册 API

**原问题**: "自定义布局注册 API：`register`, `ExtensionCategory` — 确切的导入路径和类接口？"

### 答案

✗ **ReactFlow 无自定义布局注册机制**

与 AntV G6 不同，ReactFlow **没有插件系统**。改为：

#### 方法 1: 纯函数方式（推荐）

```typescript
// resources/ui/src/visualization/graphLayout.ts

export function getLayoutedElements<T = Record<string, unknown>>(
  nodes: Node<T>[],
  edges: Edge[],
  graphType: string = 'call_graph',
  rootNodeIds?: Set<string>
): Node<T>[] {
  // 1. 使用 Dagre 计算布局
  const g = new dagre.graphlib.Graph();
  g.setGraph({ rankdir: 'LR', nodesep: 40, ranksep: 120 });
  g.setDefaultEdgeLabel(() => ({}));

  // 2. 添加节点和边
  nodes.forEach(node => {
    g.setNode(node.id, { width: 280, height: 60 });
  });
  edges.forEach(edge => {
    if (g.hasNode(edge.source) && g.hasNode(edge.target)) {
      g.setEdge(edge.source, edge.target);
    }
  });

  // 3. 执行布局
  dagre.layout(g);

  // 4. 提取位置信息
  return nodes.map(node => {
    const n = g.node(node.id);
    return {
      ...node,
      position: {
        x: n.x - 280 / 2,
        y: n.y - 60 / 2,
      },
      sourcePosition: Position.Right,
      targetPosition: Position.Left,
    };
  });
}
```

#### 方法 2: 在组件中使用

```typescript
// resources/ui/src/visualization/GraphPage.tsx

export function GraphPage() {
  const [nodes, setNodes] = useState<Node[]>([...]);
  const [edges, setEdges] = useState<Edge[]>([...]);
  const rootNodeIds = new Set(['main']);

  // 计算布局
  const layoutedNodes = getLayoutedElements(nodes, edges, 'call_graph', rootNodeIds);

  // 更新状态
  useEffect(() => {
    setNodes(layoutedNodes);
  }, []);

  return <GraphCore nodes={nodes} edges={edges} setNodes={setNodes} setEdges={setEdges} />;
}
```

### 为什么没有注册机制？

- **ReactFlow 理念**: 组件化 + React 生态
- **布局**: 独立算法（如 Dagre），返回新的 nodes 数组
- **无插件系统**: 所有功能通过 props 或 Hooks 注册

---

## Q3: History 插件 API

**原问题**: "history 插件 API：如何获取插件实例，call undo/redo？"

### 答案

✗ **ReactFlow v11 无内置 History 插件**

AntV G6 有 history 插件，但 ReactFlow 没有。

#### 手动实现方案（CodeXray 已使用）

**File**: `resources/ui/src/visualization/GraphCore.tsx`

```typescript
import { useCallback, useEffect } from 'react';

interface GraphCoreProps {
  nodes: Node<FlowNodeData>[];
  edges: Edge[];
  setNodes: React.Dispatch<React.SetStateAction<Node<FlowNodeData>[]>>;
  setEdges: React.Dispatch<React.SetStateAction<Edge[]>>;
  onUndo?: () => void;
  onRedo?: () => void;
  onBeforeDrag?: () => void;
}

export function GraphCore({
  nodes,
  edges,
  setNodes,
  setEdges,
  onUndo,
  onRedo,
  onBeforeDrag,
}: GraphCoreProps) {
  // 键盘快捷键：Ctrl/Cmd+Z 撤销，Ctrl/Cmd+Shift+Z 或 Ctrl/Cmd+Y 恢复
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const mod = e.metaKey || e.ctrlKey;
      if (!mod) return;

      // Ctrl+Shift+Z 或 Ctrl+Y → 恢复
      if ((e.key === 'z' || e.key === 'Z') && e.shiftKey) {
        e.preventDefault();
        onRedo?.();
        return;
      }
      if (e.key === 'y') {
        e.preventDefault();
        onRedo?.();
        return;
      }

      // Ctrl+Z → 撤销
      if (e.key === 'z' && !e.shiftKey) {
        e.preventDefault();
        onUndo?.();
        return;
      }
    };
    document.addEventListener('keydown', handler);
    return () => document.removeEventListener('keydown', handler);
  }, [onUndo, onRedo]);

  // 节点拖拽开始：保存拖拽前快照
  const onNodeDragStart = useCallback(() => {
    onBeforeDrag?.();
  }, [onBeforeDrag]);

  return (
    <div style={{ width: '100%', height: '100%' }}>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        // ... 其他配置
        onNodeDragStart={onNodeDragStart}
      >
        <Controls />
        <Background variant={BackgroundVariant.Dots} gap={20} size={1} />
      </ReactFlow>
    </div>
  );
}
```

**File**: `resources/ui/src/visualization/GraphPage.tsx`

```typescript
import { useCallback, useRef } from 'react';
import { GraphCore } from './GraphCore';

interface Snapshot {
  nodes: Node<FlowNodeData>[];
  edges: Edge[];
}

export function GraphPage() {
  const [nodes, setNodes] = useState<Node<FlowNodeData>[]>([...]);
  const [edges, setEdges] = useState<Edge[]>([...]);

  // 历史栈
  const historyRef = useRef<Snapshot[]>([]);
  const pointerRef = useRef(0);

  // 保存快照
  const saveSnapshot = useCallback((nodes: Node<FlowNodeData>[], edges: Edge[]) => {
    historyRef.current = historyRef.current.slice(0, pointerRef.current + 1);
    historyRef.current.push({ nodes, edges });
    pointerRef.current++;
  }, []);

  // 撤销
  const undo = useCallback(() => {
    if (pointerRef.current <= 0) return;
    pointerRef.current--;
    const snap = historyRef.current[pointerRef.current];
    setNodes(snap.nodes);
    setEdges(snap.edges);
  }, []);

  // 恢复
  const redo = useCallback(() => {
    if (pointerRef.current >= historyRef.current.length - 1) return;
    pointerRef.current++;
    const snap = historyRef.current[pointerRef.current];
    setNodes(snap.nodes);
    setEdges(snap.edges);
  }, []);

  return (
    <GraphCore
      nodes={nodes}
      edges={edges}
      setNodes={setNodes}
      setEdges={setEdges}
      onUndo={undo}
      onRedo={redo}
      onBeforeDrag={() => saveSnapshot(nodes, edges)}
    />
  );
}
```

### 关键点

- **无插件实例获取** — 改为直接调用回调函数
- **快照存储** — 用 `useRef` 保存历史栈
- **键盘绑定** — 在 GraphCore 中监听，调用父组件提供的回调
- **状态恢复** — `setNodes` 和 `setEdges` 更新视图

---

## Q4: 节点样式函数签名

**原问题**: "节点样式函数签名：是否 `fill: (d) => ...` 工作？什么是 `d` 参数类型？"

### 答案

✗ **ReactFlow 不支持函数式样式**

与 AntV G6 不同，ReactFlow 的样式是静态对象，不支持函数。

#### 正确的方式

```typescript
// 方式 1: 内联样式（CSSProperties）
const nodes: Node[] = [
  {
    id: 'n1',
    data: { label: 'Node 1' },
    position: { x: 0, y: 0 },
    style: {
      background: '#fff',
      border: '1px solid #ccc',
      borderRadius: '3px',
      padding: '10px',
    },
  }
];

// 方式 2: CSS class
const nodes: Node[] = [
  {
    id: 'n1',
    data: { label: 'Node 1' },
    position: { x: 0, y: 0 },
    className: 'my-node',
  }
];

// CSS
.my-node {
  background: var(--vscode-editor-background);
  color: var(--vscode-editor-foreground);
  border: 1px solid var(--vscode-input-border);
}

// 方式 3: 条件样式（通过 selected 状态）
const updatedNodes = nodes.map(n => ({
  ...n,
  style: {
    ...n.style,
    border: n.selected ? '2px solid blue' : '1px solid gray',
    background: n.selected ? '#eee' : '#fff',
  }
}));
```

#### CodeXray 中的做法

**File**: `resources/ui/src/visualization/GraphNode.tsx`

```typescript
export function GraphNode({ data, selected }: NodeProps) {
  const isRoot = data?.isRoot === true;

  return (
    <div
      className={`graph-node-inner${selected ? ' graph-node-selected' : ''}${isRoot ? ' graph-node-root' : ''}`}
      style={{
        minWidth: 200,
        maxWidth: 360,
        paddingTop: 8,
        paddingBottom: 8,
        paddingLeft: 12,
        paddingRight: 12,
        fontSize: 'var(--vscode-font-size, 13px)',
        fontFamily: 'var(--vscode-font-family, monospace)',
        cursor: 'grab',
      }}
    >
      {data?.label}
    </div>
  );
}
```

**File**: `resources/ui/src/visualization/graph.css`

```css
.graph-node-inner {
  background: var(--vscode-editor-background);
  color: var(--vscode-editor-foreground);
  border: 1px solid var(--vscode-input-border);
  text-align: center;
}

.graph-node-inner.graph-node-selected {
  border: 2px solid var(--vscode-focusBorder);
  box-shadow: 0 0 0 2px var(--vscode-focusBorder, rgba(0, 100, 200, 0.3));
}

.graph-node-inner.graph-node-root {
  background: var(--vscode-notebookSelectedCellBackground, rgba(200, 200, 255, 0.1));
  border: 2px solid var(--vscode-notebookSelectedCellBorder, #4a90e2);
}
```

### 为什么是对象而非函数？

- **React 理念**: JSX 和纯组件
- **样式即数据**: 对象便于序列化、持久化、diff
- **动态样式**: 通过条件渲染或 className 切换，不是函数参数

---

## Q5: Brush-Select 行为配置

**原问题**: "`brush-select` behavior 配置 — 是否 `trigger: ['shift']` 工作？"

### 答案

✗ **ReactFlow 无 behavior 配置系统**

AntV G6 有 `behaviors` 配置，ReactFlow 改为 props。

#### 正确的方式

```typescript
<ReactFlow
  selectionOnDrag={true}                      // ← 启用矩形框选
  selectionMode={SelectionMode.Partial}       // ← 只要相交就选中
  multiSelectionKeyCode="Shift"                // ← 多选修饰键（默认值）
  panOnDrag={[1, 2]}                           // ← 中键+右键平移（不干扰框选）
>
  {/* ... */}
</ReactFlow>
```

#### 各配置的含义

```typescript
// selectionOnDrag: 启用/禁用拖拽框选
<ReactFlow selectionOnDrag={true} />    // ✓ 可框选
<ReactFlow selectionOnDrag={false} />   // ✗ 无法框选

// selectionMode: 选中的判定规则
SelectionMode.Partial                   // 节点边界相交即选中（推荐）
SelectionMode.Full                      // 节点完全在框内才选中

// multiSelectionKeyCode: 多选修饰键
<ReactFlow multiSelectionKeyCode="Shift" />     // Shift+点击 多选
<ReactFlow multiSelectionKeyCode="Meta" />      // Cmd/Win+点击 多选
<ReactFlow multiSelectionKeyCode="Control" />   // Ctrl+点击 多选

// panOnDrag: 哪些鼠标按钮可以拖拽平移
panOnDrag={true}                        // 任何鼠标按钮（包括左键，会干扰框选）
panOnDrag={false}                       // 禁用平移
panOnDrag={[1, 2]}                      // 仅中键(1) + 右键(2)，左键(0)不影响
```

#### CodeXray 的配置

**File**: `resources/ui/src/visualization/GraphCore.tsx`

```typescript
<ReactFlow
  nodes={nodes}
  edges={edges}
  nodeTypes={nodeTypes}
  onNodesChange={onNodesChange}
  onEdgesChange={onEdgesChange}
  onNodeDoubleClick={onNodeDoubleClick}
  onNodeDragStart={onNodeDragStart}
  onNodeDrag={onNodeDrag}
  onNodeContextMenu={onNodeContextMenuHandler}
  onSelectionContextMenu={onSelectionContextMenuHandler}
  onPaneContextMenu={onPaneContextMenu}
  defaultEdgeOptions={defaultEdgeOptions}
  panOnDrag={[1, 2]}                    // ← 仅中键+右键，左键用于框选
  selectionOnDrag                       // ← 启用框选
  selectionMode={SelectionMode.Partial} // ← 相交即选中
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

---

## Q6: getNodeData / getEdgeData 方法

**原问题**: "`graph.getNodeData()`, `graph.getEdgeData()` — 返回类型？"

### 答案

✗ **ReactFlow 无此方法**

改为通过 React state 或 Hook 访问。

#### 方法 1: React State（推荐）

```typescript
const [nodes, setNodes] = useState<Node<FlowNodeData>[]>([...]);
const [edges, setEdges] = useState<Edge[]>([...]);

// 获取节点
const node = nodes.find(n => n.id === 'some-id');
const nodeData = node?.data;

// 获取边
const edge = edges.find(e => e.id === 'some-edge-id');
const edgeData = edge?.data;
```

#### 方法 2: useReactFlow Hook

```typescript
import { useReactFlow } from 'reactflow';

function MyComponent() {
  const { getNode, getNodes, getEdge, getEdges } = useReactFlow();

  // 获取单个节点
  const node = getNode('node-id');          // Node | undefined
  const nodeData = node?.data;

  // 获取所有节点
  const allNodes = getNodes();               // Node[]

  // 获取单个边
  const edge = getEdge('edge-id');           // Edge | undefined
  const edgeData = edge?.data;

  // 获取所有边
  const allEdges = getEdges();               // Edge[]
}
```

#### 返回类型

```typescript
// Node<T> 结构
{
  id: string;
  data: T;
  position: { x: number; y: number };
  type?: string;
  selected?: boolean;
  dragging?: boolean;
  // ... 其他属性
}

// Edge 结构
{
  id: string;
  source: string;
  target: string;
  type?: string;
  animated?: boolean;
  selected?: boolean;
  data?: Record<string, unknown>;
  // ... 其他属性
}
```

#### CodeXray 用法

```typescript
// 获取节点定义位置（用于 gotoSymbol）
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

---

## Q7: setElementState 方法

**原问题**: "`graph.setElementState()` — 确切的签名？"

### 答案

✗ **ReactFlow 无此方法**

改为直接修改 state 对象。

#### 正确的方式

```typescript
// 选中/取消选中单个节点
setNodes(prev =>
  prev.map(n => ({
    ...n,
    selected: n.id === 'target-id' ? true : false,
  }))
);

// 清除所有选中
setNodes(prev =>
  prev.map(n => ({ ...n, selected: false }))
);

// 选中多个节点
const targetIds = new Set(['n1', 'n2', 'n3']);
setNodes(prev =>
  prev.map(n => ({
    ...n,
    selected: targetIds.has(n.id),
  }))
);

// 修改节点其他状态
setNodes(prev =>
  prev.map(n =>
    n.id === 'target-id'
      ? {
          ...n,
          data: { ...n.data, expanded: true },
          style: { ...n.style, border: '2px solid blue' },
        }
      : n
  )
);
```

#### 边的状态

```typescript
// 修改边状态
setEdges(prev =>
  prev.map(e => ({
    ...e,
    selected: e.id === 'target-edge-id' ? true : false,
    animated: e.id === 'target-edge-id' ? true : false,
  }))
);
```

---

## Q8: addNodeData / removeNodeData 签名

**原问题**: "`graph.addNodeData()`, `graph.removeNodeData()` — 确切的签名？"

### 答案

✗ **ReactFlow 无此方法**

改为 React state 操作。

#### 添加节点

```typescript
// 添加单个节点
setNodes(prev => [
  ...prev,
  {
    id: 'new-node-id',
    data: { label: 'New Node' },
    position: { x: 100, y: 100 },
    type: 'graphNode',
  },
]);

// 添加多个节点
setNodes(prev => [
  ...prev,
  ...newNodes,  // newNodes: Node[]
]);

// 使用 Hook
const { addNodes } = useReactFlow();
addNodes([newNode1, newNode2]);
```

#### 移除节点

```typescript
// 移除单个节点
setNodes(prev => prev.filter(n => n.id !== 'target-id'));

// 移除多个节点
const targetIds = new Set(['n1', 'n2', 'n3']);
setNodes(prev =>
  prev.filter(n => !targetIds.has(n.id))
);

// 同时清理相关边
setNodes(prev => prev.filter(n => !targetIds.has(n.id)));
setEdges(prev =>
  prev.filter(e => !targetIds.has(e.source) && !targetIds.has(e.target))
);

// 使用 Hook
const { deleteElements } = useReactFlow();
deleteElements({ nodes: nodesToDelete });
```

#### 添加/移除边

```typescript
// 添加边
setEdges(prev => [
  ...prev,
  {
    id: 'new-edge-id',
    source: 'n1',
    target: 'n2',
    type: 'smoothstep',
  },
]);

// 移除边
setEdges(prev => prev.filter(e => e.id !== 'target-edge-id'));

// 使用 Hook
const { addEdges } = useReactFlow();
addEdges([newEdge1, newEdge2]);
```

---

## Q9: 事件处理器签名

**原问题**: "事件处理器签名：`graph.on('node:click', (event) => ...)` — event 有什么属性？"

### 答案

✓ **ReactFlow 事件处理器**

```typescript
// 节点点击
onNodeClick?: (event: React.MouseEvent, node: Node) => void

// 节点双击
onNodeDoubleClick?: (event: React.MouseEvent, node: Node) => void

// 节点右键
onNodeContextMenu?: (event: React.MouseEvent, node: Node) => void

// 节点拖拽
onNodeDragStart?: (event: React.MouseEvent, node: Node, nodes: Node[]) => void
onNodeDrag?: (event: React.MouseEvent, node: Node, nodes: Node[]) => void
onNodeDragStop?: (event: React.MouseEvent, node: Node, nodes: Node[]) => void

// 边点击
onEdgeClick?: (event: React.MouseEvent, edge: Edge) => void

// 框选右键
onSelectionContextMenu?: (event: React.MouseEvent, nodes: Node[]) => void

// 空白区右键
onPaneContextMenu?: (event: React.MouseEvent | MouseEvent) => void

// 连接创建
onConnect?: (connection: Connection) => void
```

#### Event 对象属性（React.MouseEvent）

```typescript
event.clientX             // 相对于视口的 X 坐标
event.clientY             // 相对于视口的 Y 坐标
event.pageX               // 相对于页面的 X 坐标
event.pageY               // 相对于页面的 Y 坐标
event.screenX             // 相对于屏幕的 X 坐标
event.screenY             // 相对于屏幕的 Y 坐标

event.button              // 0=左键，1=中键，2=右键
event.buttons             // 按下的按钮位掩码

event.altKey              // Ctrl 键按下
event.ctrlKey             // Alt 键按下
event.shiftKey            // Shift 键按下
event.metaKey             // Cmd/Win 键按下

event.preventDefault()    // 阻止浏览器默认行为
event.stopPropagation()   // 阻止事件冒泡
event.currentTarget       // 当前绑定的元素
event.target              // 事件真实发生的元素
```

#### CodeXray 中的用法

```typescript
// 节点双击 → 跳转到定义
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

// 节点右键 → 显示菜单
const onNodeContextMenuHandler = useCallback(
  (ev: React.MouseEvent, node: Node<FlowNodeData>) => {
    ev.preventDefault();  // ← 阻止浏览器默认菜单
    onNodeContextMenu?.(node, ev);
  },
  [onNodeContextMenu]
);

// 节点拖拽 → 碰撞检测
const onNodeDrag = useCallback(
  (_: React.MouseEvent, draggedNode: Node<FlowNodeData>) => {
    setNodes((curNodes) => {
      const updated = curNodes.map((n) =>
        n.id === draggedNode.id ? { ...n, position: draggedNode.position } : n
      );
      const pushed = pushOverlapping(draggedNode.id, updated);
      return pushed ?? updated;
    });
  },
  [setNodes]
);
```

---

## Q10: fitView / fitCenter 签名

**原问题**: "`graph.fitView()`, `graph.fitCenter()` — 确切的签名？"

### 答案

✓ **ReactFlow 都支持**

#### 方式 1: Props 配置（自动）

```typescript
<ReactFlow
  fitView                                  // ← 挂载时自动 fitView
  fitViewOptions={{
    padding: 0.2,                         // 内边距（相对于视图大小）
    includeHiddenNodes: false,            // 是否包含隐藏的节点
    minZoom: 0.5,
    maxZoom: 2,
  }}
>
  {/* ... */}
</ReactFlow>
```

#### 方式 2: Hook 手动调用

```typescript
import { useReactFlow } from 'reactflow';

function MyComponent() {
  const { fitView, fitCenter } = useReactFlow();

  // fitView: 平衡地缩放和移动，使所有节点可见
  const handleFitView = async () => {
    const success = await fitView({
      padding: 0.2,
      minZoom: 0.5,
      maxZoom: 2,
      duration: 800,  // 动画时长（毫秒）
    });
    console.log('Fit view', success ? 'succeeded' : 'failed');
  };

  // fitCenter: 仅移动到中心，不改变缩放
  const handleFitCenter = async () => {
    const success = await fitCenter({
      zoom: 1,
      duration: 800,
    });
    console.log('Fit center', success ? 'succeeded' : 'failed');
  };

  return (
    <>
      <button onClick={handleFitView}>Fit View</button>
      <button onClick={handleFitCenter}>Fit Center</button>
    </>
  );
}
```

#### 签名详解

```typescript
// fitView 签名
async function fitView(options?: {
  padding?: number;              // 内边距，0-1 范围（默认 0.1）
  includeHiddenNodes?: boolean;  // 是否包含隐藏节点（默认 false）
  minZoom?: number;              // 最小缩放（默认无限制）
  maxZoom?: number;              // 最大缩放（默认无限制）
  duration?: number;             // 动画时长毫秒（默认 0，无动画）
}): Promise<boolean>;

// fitCenter 签名
async function fitCenter(options?: {
  zoom?: number;                 // 目标缩放级别（默认当前缩放）
  duration?: number;             // 动画时长毫秒（默认 0，无动画）
}): Promise<boolean>;
```

#### 返回值

- `true`: 操作成功
- `false`: 操作失败（通常因节点数为 0）

#### 其他视图操作

```typescript
const {
  zoomIn,              // 放大一级（通常 zoom * 1.2）
  zoomOut,             // 缩小一级（通常 zoom * 0.8）
  getViewport,         // 获取当前视图: {x, y, zoom}
  setViewport,         // 设置视图: {x, y, zoom}
  focusElement,        // 高亮并聚焦到某个节点
} = useReactFlow();

// 示例
zoomIn();
zoomOut();

const {x, y, zoom} = getViewport();
setViewport({x: 0, y: 0, zoom: 1});

focusElement('node-id', {
  duration: 800,
});
```

---

## 总结表格

| # | 原始问题 | ReactFlow 答案 |
|---|----------|--------------|
| 1 | 导入路径与 GraphOptions | `import { ReactFlow } from 'reactflow'`；无单独 GraphOptions |
| 2 | 自定义布局注册 | 无注册机制；返回布局后的 nodes 数组 |
| 3 | History 插件 API | 无内置；手动实现快照 + 键盘监听 |
| 4 | 节点样式函数 | 不支持函数；用 `style: CSSProperties` 或 CSS class |
| 5 | brush-select 配置 | `selectionOnDrag={true}` + `SelectionMode.Partial` |
| 6 | getNodeData/getEdgeData | 用 React state 或 useReactFlow Hook |
| 7 | setElementState | 直接修改 state：`setNodes(prev => prev.map(...))` |
| 8 | addNodeData/removeNodeData | 用 setNodes/setEdges 或 Hook 方法 |
| 9 | 事件处理器签名 | `(event: React.MouseEvent, node: Node) => void` |
| 10 | fitView/fitCenter | 都支持；通过 props 或 useReactFlow Hook |

---

## 快速开发指南

基于以上 10 个问题的答案，快速开发时的要点：

1. ✓ **不要找 Graph 类** — 用 `<ReactFlow>` 组件
2. ✓ **布局是纯函数** — 计算后设置到 nodes
3. ✓ **撤销/重做手动实现** — 快照 + 键盘监听
4. ✓ **样式用 CSS** — 不是函数式参数
5. ✓ **框选很简单** — 一个 prop 搞定
6. ✓ **访问数据用 state** — 不是 API 方法
7. ✓ **修改状态用 setState** — 不是 method call
8. ✓ **事件就是 React 事件** — 标准 React.MouseEvent
9. ✓ **视图操作很直观** — fitView/fitCenter 都有
10. ✓ **参考 CodeXray 现有代码** — 所有模式都已实现

---

## 参考文档

- **完整 API 参考**: `/Volumes/SSD_1T/develop/CodeXray/doc/API_REFERENCE.md`
- **实现清单**: `/Volumes/SSD_1T/develop/CodeXray/doc/IMPLEMENTATION_CHECKLIST.md`
- **当前实现**: `/Volumes/SSD_1T/develop/CodeXray/resources/ui/src/visualization/`
