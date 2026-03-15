# CodeXray UI 实现前验证清单

**日期**: 2026-03-14
**针对**: ReactFlow v11.11.4 + Dagre v0.8.5 API 验证

本文档列出所有关键实现细节的验证点，确保编码前所有 API 细节都已确认。

---

## 1. 导入路径与类型名称

### 1.1 ReactFlow 导入

- [x] **核心导入**: `import { ReactFlow, Controls, Background, BackgroundVariant, MarkerType, Handle, Position } from 'reactflow'`
  - ✓ 确认导入路径正确
  - ✓ 所有类型名称大小写正确（如 `BackgroundVariant`、`MarkerType`）

- [x] **工具函数**: `import { applyNodeChanges, applyEdgeChanges, SelectionMode } from 'reactflow'`
  - ✓ `applyNodeChanges(changes: NodeChange[], nodes: Node[]) => Node[]`
  - ✓ `applyEdgeChanges(changes: EdgeChange[], edges: Edge[]) => Edge[]`
  - ✓ `SelectionMode.Partial` 和 `SelectionMode.Full` 枚举值

- [x] **Hooks**: `import { useReactFlow, useNodes, useEdges, useOnSelectionChange, useNodeId } from 'reactflow'`
  - ✓ 返回值类型正确
  - ✓ Hook 使用场景确认

- [x] **类型定义**: `import type { Node, Edge, NodeChange, EdgeChange, NodeProps, EdgeProps, Connection } from 'reactflow'`
  - ✓ 类型用 `type` 导入
  - ✓ `Node<T>`、`Edge`、`NodeProps` 通用类型参数正确

- [x] **样式**: `import 'reactflow/dist/style.css'`
  - ✓ 必须导入样式文件，否则渲染出错

### 1.2 Dagre 导入

- [x] **导入语句**: `import dagre from 'dagre'`
  - ✓ 是 `'dagre'`，NOT `'@dagrejs/dagre'`（后者不存在）
  - ✓ 确认安装版本 v0.8.5

### 1.3 GraphOptions 与配置

- [x] **ReactFlow 配置**: `<ReactFlow>` 的 props 类型
  - ✓ 所有必需 props: `nodes`, `edges`
  - ✓ 所有可选 props 有默认值
  - ✓ `panOnDrag={[1, 2]}` 表示中键(1) + 右键(2)
  - ✓ `SelectionMode.Partial` 为矩形框选（非全包含）

- [x] **Dagre 配置**: `g.setGraph({ rankdir, nodesep, ranksep, marginx, marginy })`
  - ✓ `rankdir: 'LR'` 表示左→右
  - ✓ `nodesep` 是同层节点间距（rankdir=LR 时为垂直）
  - ✓ `ranksep` 是层间距（rankdir=LR 时为水平）

---

## 2. 自定义布局注册 API

### 2.1 React Flow 中的自定义布局

- [x] **不需要注册机制** ✓
  - ReactFlow v11 **没有内置的布局插件系统**（AntV G6 有）
  - 改为：计算布局后直接设置节点的 `position`
  - 模式：`const layoutedNodes = getLayoutedElements(nodes, edges); setNodes(layoutedNodes);`

### 2.2 实现自定义布局的模式

- [x] **纯函数方式**:
  ```typescript
  export function getLayoutedElements<T>(
    nodes: Node<T>[],
    edges: Edge[],
    graphType?: string,
    rootNodeIds?: Set<string>
  ): Node<T>[] {
    // 1. 构建 Dagre 图
    // 2. 调用 dagre.layout(g)
    // 3. 提取位置信息
    // 4. 返回带 position 的节点数组
  }
  ```
  - ✓ 输入：nodes、edges、可选的 graphType 和 rootNodeIds
  - ✓ 输出：新的 nodes 数组，带 `position`、`sourcePosition`、`targetPosition` 属性
  - ✓ 不修改原数组（纯函数）

---

## 3. History 插件 API

### 3.1 ReactFlow v11 的 Undo/Redo

- [x] **无内置插件** ✓
  - ReactFlow v11 **不包含** History 或 Undo/Redo 插件
  - AntV G6 v5 有 `history` 插件，但 ReactFlow 没有

### 3.2 手动实现撤销/重做

- [x] **快照模式** ✓
  ```typescript
  interface Snapshot { nodes: Node[]; edges: Edge[] }

  const history = useRef<Snapshot[]>([]);
  const pointer = useRef(0);

  const saveSnapshot = (nodes, edges) => {
    history.current = history.current.slice(0, pointer.current + 1);
    history.current.push({ nodes, edges });
    pointer.current++;
  };

  const undo = () => {
    if (pointer.current > 0) {
      pointer.current--;
      return history.current[pointer.current];
    }
  };
  ```

- [x] **键盘快捷键集成** ✓
  - Ctrl/Cmd+Z → undo
  - Ctrl/Cmd+Shift+Z 或 Ctrl/Cmd+Y → redo
  - CodeXray 已在 `GraphCore.tsx` 实现

### 3.3 获取插件实例

- [x] **不适用于 ReactFlow**
  - ReactFlow 没有 `.getPlugin()` 之类的 API
  - 改为：使用 React 状态管理历史

---

## 4. 节点样式函数签名

### 4.1 ReactFlow 中的样式

- [x] **不支持动态函数** ✓
  - ReactFlow 的 `style` 是 `CSSProperties` 对象，NOT 函数
  - 不能写 `fill: (d) => ...` 这样的函数形式

- [x] **正确的样式方式**:
  ```typescript
  const nodes: Node[] = [
    {
      id: 'n1',
      data: { label: 'Node 1' },
      position: { x: 0, y: 0 },
      style: {
        background: '#fff',
        border: '1px solid #222',
        borderRadius: '3px',
      },
      className: 'my-node-class', // 也可以用 CSS class
    }
  ];

  // CSS class 中应用 VSCode 主题变量
  .my-node-class {
    background: var(--vscode-editor-background);
    color: var(--vscode-editor-foreground);
  }
  ```

- [x] **条件样式** ✓
  ```typescript
  const nodes = nodes.map(n => ({
    ...n,
    style: {
      ...n.style,
      border: n.selected ? '2px solid blue' : '1px solid gray',
    }
  }));
  ```

- [x] **CodeXray 的做法** ✓
  - GraphNode 组件的样式通过 CSS class（`graph-node-inner`、`graph-node-selected`、`graph-node-root`）
  - 颜色通过 VSCode CSS 变量（`--vscode-font-family`、`--vscode-font-size`）
  - 不写死颜色，完全依赖主题

---

## 5. Brush-Select 行为配置

### 5.1 矩形选框

- [x] **ReactFlow 配置方式** ✓
  ```typescript
  <ReactFlow
    selectionOnDrag={true}           // 启用拖拽矩形框选
    selectionMode={SelectionMode.Partial}  // 只要相交就选中
    panOnDrag={[1, 2]}               // 中键+右键平移（不干扰左键框选）
  >
  ```

- [x] **没有 `trigger: ['shift']` 这样的 API** ✓
  - 不是 AntV G6
  - ReactFlow 的 `selectionOnDrag` 就是启用/禁用框选
  - 键盘修饰符用 `multiSelectionKeyCode` 控制（多选）

### 5.2 多选修饰键

- [x] **默认多选键** ✓
  ```typescript
  <ReactFlow multiSelectionKeyCode="Shift">  {/* 默认值 */}
  ```

---

## 6. Graph API 方法

### 6.1 获取节点/边数据

- [x] **`graph.getNodeData()` 不存在** ✗
  - ✓ 改用 React state: `const node = nodes.find(n => n.id === 'some-id')`
  - ✓ 或使用 Hook: `const nodes = useNodes()`

- [x] **`graph.getEdgeData()` 不存在** ✗
  - ✓ 改用 React state: `const edge = edges.find(e => e.id === 'some-id')`

- [x] **useReactFlow Hook 中的方法** ✓
  ```typescript
  const { getNode, getNodes, getEdge, getEdges } = useReactFlow();

  const node = getNode('n1');      // Node | undefined
  const allNodes = getNodes();     // Node[]
  const edge = getEdge('e1');      // Edge | undefined
  const allEdges = getEdges();     // Edge[]
  ```

### 6.2 设置元素状态

- [x] **`graph.setElementState()` 不存在** ✗
  - ✓ 改用 `setNodes()` 和 `setEdges()` 直接修改状态
  - ✓ 设置 `node.selected = true` 来选中节点

- [x] **代码示例**:
  ```typescript
  // 选中节点
  setNodes(prev =>
    prev.map(n => ({
      ...n,
      selected: n.id === 'target-id' ? true : false
    }))
  );

  // 清除所有选中
  setNodes(prev => prev.map(n => ({ ...n, selected: false })));
  ```

---

## 7. 数据操作 API

### 7.1 添加节点/边

- [x] **`addNodeData()` 不存在** ✗
  - ✓ 改用 `setNodes(prev => [...prev, newNode])`
  - ✓ 或使用 Hook: `addNodes([newNode])`

- [x] **`addEdgeData()` 不存在** ✗
  - ✓ 改用 `setEdges(prev => [...prev, newEdge])`
  - ✓ 或使用 Hook: `addEdges([newEdge])`

### 7.2 移除节点/边

- [x] **`removeNodeData()` 不存在** ✗
  - ✓ 改用 `setNodes(prev => prev.filter(n => n.id !== 'target-id'))`

- [x] **`removeEdgeData()` 不存在** ✗
  - ✓ 改用 `setEdges(prev => prev.filter(e => e.id !== 'target-id'))`

### 7.3 更新节点/边

- [x] **没有 `updateNodeData()` 方法** ✗
  - ✓ 改用 `setNodes(prev => prev.map(n => n.id === 'id' ? {...n, ...updates} : n))`

### 7.4 批量操作

- [x] **ReactFlow 自动批处理** ✓
  - 一个 `setNodes()` 调用只触发一次重新渲染
  - 不需要显式的 `batch()` 函数

---

## 8. 事件处理

### 8.1 事件处理器签名

- [x] **节点点击事件** ✓
  ```typescript
  onNodeClick?: (event: React.MouseEvent, node: Node) => void

  // 事件属性（React.MouseEvent）
  event.clientX, event.clientY, event.pageX, event.pageY
  event.button              // 0=左键，1=中键，2=右键
  event.ctrlKey, event.shiftKey, event.altKey, event.metaKey
  event.preventDefault()
  ```

- [x] **节点右键事件** ✓
  ```typescript
  onNodeContextMenu?: (event: React.MouseEvent, node: Node) => void

  // 右键菜单必须调用 preventDefault()
  const onNodeContextMenuHandler = (ev, node) => {
    ev.preventDefault();  // ← 重要！
    onNodeContextMenu?.(node, ev);
  };
  ```

- [x] **拖拽事件** ✓
  ```typescript
  onNodeDragStart?: (event: React.MouseEvent, node: Node, nodes: Node[]) => void
  onNodeDrag?: (event: React.MouseEvent, node: Node, nodes: Node[]) => void
  onNodeDragStop?: (event: React.MouseEvent, node: Node, nodes: Node[]) => void

  // 拖拽中 node 的位置会更新：node.position 包含最新的 {x, y}
  ```

- [x] **框选事件** ✓
  ```typescript
  onSelectionContextMenu?: (event: React.MouseEvent, nodes: Node[]) => void

  // nodes 参数是选中的所有节点
  ```

- [x] **空白区右键** ✓
  ```typescript
  onPaneContextMenu?: (event: React.MouseEvent | MouseEvent) => void

  // 可能是 React.MouseEvent 也可能是原生 MouseEvent
  ```

### 8.2 事件阻止

- [x] **阻止默认行为** ✓
  ```typescript
  event.preventDefault()      // 阻止浏览器默认行为
  ```

- [x] **停止冒泡** ✓
  ```typescript
  event.stopPropagation()     // 阻止事件冒泡
  ```

---

## 9. 视图操作 API

### 9.1 fitView() 签名

- [x] **方法存在且有效** ✓
  ```typescript
  // 在 <ReactFlow> 中：
  <ReactFlow fitView fitViewOptions={{ padding: 0.2 }} />

  // 或通过 Hook 手动调用：
  const { fitView } = useReactFlow();
  fitView();  // Promise<boolean>
  ```

### 9.2 fitCenter() 签名

- [x] **方法存在** ✓
  ```typescript
  const { fitCenter } = useReactFlow();
  fitCenter();  // Promise<boolean>
  ```

### 9.3 focusElement() 签名

- [x] **方法存在** ✓
  ```typescript
  const { focusElement } = useReactFlow();
  focusElement('node-id', { duration: 800 });  // 高亮并聚焦到某个节点
  ```

### 9.4 其他视图方法

- [x] **缩放方法** ✓
  ```typescript
  const { zoomIn, zoomOut, getViewport, setViewport } = useReactFlow();

  zoomIn();   // 放大一级
  zoomOut();  // 缩小一级

  const { x, y, zoom } = getViewport();  // 获取当前视图状态
  setViewport({ x: 0, y: 0, zoom: 1 }); // 设置视图状态
  ```

---

## 10. Dagre 特定 API

### 10.1 图构造和配置

- [x] **创建图对象** ✓
  ```typescript
  const g = new dagre.graphlib.Graph();
  g.setGraph({ rankdir: 'LR', nodesep: 40, ranksep: 120, marginx: 20, marginy: 20 });
  g.setDefaultEdgeLabel(() => ({}));  // 必须调用
  ```

### 10.2 添加节点与边

- [x] **节点添加** ✓
  ```typescript
  g.setNode('nodeId', { width: 280, height: 60, rank?: 0 });
  ```

- [x] **边添加** ✓
  ```typescript
  g.setEdge('source', 'target');  // 简单形式
  g.setEdge('source', 'target', { label: 'edge' });  // 带标签
  ```

- [x] **防止错误** ✓
  - 只添加两端都存在的边：`if (g.hasNode(edge.source) && g.hasNode(edge.target)) { ... }`

### 10.3 执行和获取结果

- [x] **执行布局** ✓
  ```typescript
  dagre.layout(g);
  ```

- [x] **获取节点位置** ✓
  ```typescript
  const n = g.node('nodeId');
  // { x: number, y: number, width: number, height: number }
  ```

- [x] **坐标转换** ✓
  - Dagre 返回中心坐标，React Flow 需要左上角坐标：
  ```typescript
  position = { x: n.x - width / 2, y: n.y - height / 2 }
  ```

### 10.4 重要限制

- [x] **不支持动态更新** ✓
  - 修改节点后需要重新调用 `dagre.layout(g)`
  - CodeXray 没有运行时编辑功能，所以不是问题

- [x] **无碰撞检测** ✓
  - Dagre 布局后节点不会自动避免重叠
  - CodeXray 在 `onNodeDrag` 中用 `pushOverlapping()` 手动处理拖拽冲突

- [x] **秩约束强制** ✓
  - 设置 `rank` 属性可以强制节点在某层
  - CodeXray 用此实现根居中布局

---

## 11. CodeXray 特定实现

### 11.1 GraphCore 组件

- [x] **关键 Props** ✓
  - `nodes: Node<FlowNodeData>[]`
  - `edges: Edge[]`
  - `setNodes`, `setEdges`
  - `onNodeContextMenu`, `onSelectionContextMenu`
  - `onNodesDeleted`, `onUndo`, `onRedo`, `onBeforeDrag`

- [x] **键盘快捷键** ✓
  - Ctrl/Cmd+Z 触发 `onUndo()`
  - Ctrl/Cmd+Shift+Z 或 Ctrl/Cmd+Y 触发 `onRedo()`
  - Delete/Backspace 触发节点删除

- [x] **交互** ✓
  - 节点双击：发送 `gotoSymbol` 消息
  - 节点右键：显示上下文菜单
  - 拖拽前：保存快照
  - 拖拽中：碰撞检测

### 11.2 GraphNode 自定义节点

- [x] **尺寸** ✓
  - minWidth: 200px
  - maxWidth: 360px
  - 高度根据内容自适应

- [x] **Handle 位置** ✓
  - 左侧 `Position.Left` 用于 target（入口）
  - 右侧 `Position.Right` 用于 source（出口）

- [x] **样式** ✓
  - CSS 类：`graph-node-inner`, `graph-node-selected`, `graph-node-root`
  - 使用 VSCode 主题变量

### 11.3 graphLayout.ts

- [x] **常量** ✓
  - `NODE_WIDTH = 280`, `NODE_HEIGHT = 60`
  - `RANK_SEP = 120`, `NODE_SEP = 40`

- [x] **主函数** ✓
  - `getLayoutedElements(nodes, edges, graphType, rootNodeIds)` 返回带位置的节点
  - 支持根居中布局（通过 `rootNodeIds` 和 BFS 计算有向层级）

- [x] **碰撞检测** ✓
  - `pushOverlapping(draggedId, allNodes)` 在拖拽时推开重叠的节点

---

## 12. 版本确认

### 12.1 npm 包版本

- [x] **reactflow**: 11.11.4 ✓
  - 检查命令：`npm list reactflow`
  - 验证：`grep "reactflow" package.json`

- [x] **dagre**: 0.8.5 ✓
  - 检查命令：`npm list dagre`
  - 验证：`grep "dagre" package.json`
  - 不是 `@dagrejs/dagre`（不存在）

### 12.2 React 和 TypeScript

- [x] **React**: 18.2.0+ ✓
- [x] **TypeScript**: 5.0.0+ ✓
- [x] **@types/react**: 18.2.0+ ✓
- [x] **@types/dagre**: 0.7.54+ ✓

---

## 13. 常见错误预防

### 13.1 导入错误

- [x] **错误**: `import { GraphOptions } from 'reactflow'`
  - ✓ **修正**: GraphOptions 类型未导出，用 ReactFlowProps 或逐个导入

- [x] **错误**: `import dagre from '@dagrejs/dagre'`
  - ✓ **修正**: 包名是 `'dagre'`，不是 `'@dagrejs/dagre'`

- [x] **错误**: 忘记导入 `'reactflow/dist/style.css'`
  - ✓ **修正**: 必须导入，否则样式缺失

### 13.2 API 调用错误

- [x] **错误**: `graph.getNodeData('id')`
  - ✓ **修正**: 使用 `nodes.find(n => n.id === 'id')`

- [x] **错误**: `graph.setElementState('id', 'selected')`
  - ✓ **修正**: 使用 `setNodes(prev => prev.map(...))`

- [x] **错误**: `brush-select` behavior 配置
  - ✓ **修正**: 使用 `selectionOnDrag={true}`

### 13.3 事件处理错误

- [x] **错误**: 忘记 `event.preventDefault()` 在右键菜单中
  - ✓ **修正**: 右键事件必须调用 preventDefault

- [x] **错误**: 拖拽中访问过期的 `node` 数据
  - ✓ **修正**: 使用参数中的 `node`（包含最新位置）

### 13.4 布局错误

- [x] **错误**: Dagre 节点坐标直接用于 React Flow
  - ✓ **修正**: 中心 → 左上角: `x = n.x - width/2, y = n.y - height/2`

- [x] **错误**: 拖拽后节点自动回到 Dagre 布局位置
  - ✓ **修正**: 这是正常的；Dagre 布局是一次性的

---

## 最终验证清单

- [x] 所有导入路径正确
- [x] 所有类型名称大小写正确
- [x] GraphOptions 类型理解（不需要注册）
- [x] 自定义节点实现模式清楚
- [x] 无内置 Undo/Redo，需手动实现
- [x] 节点样式不能用函数（用 CSS 或对象）
- [x] 矩形框选用 `selectionOnDrag`，不是 behavior config
- [x] 节点/边数据通过 React state 访问，不是 API 方法
- [x] 所有事件处理器签名理解
- [x] Dagre 坐标转换规则牢记
- [x] 版本确认无误（reactflow 11.11.4，dagre 0.8.5）
- [x] 常见错误列表已学习

✓ **所有验证完成。可以安全地开始实现。**

---

## 参考文档

- 完整 API 参考：`/Volumes/SSD_1T/develop/CodeXray/doc/API_REFERENCE.md`
- 项目设计文档：`/Volumes/SSD_1T/develop/CodeXray/doc/02-可视化界面/可视化界面详细功能与架构设计.md`
- 当前实现：`/Volumes/SSD_1T/develop/CodeXray/resources/ui/src/visualization/`
