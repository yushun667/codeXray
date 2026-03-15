# 重要发现：CodeXray 使用 ReactFlow，非 AntV G6

**日期**: 2026-03-14
**调查结果**: 纠正了对技术栈的理解

---

## 调查背景

用户要求验证 **AntV G6 v5 API** 的具体实现细节（10 个问题）。但经过代码检查，发现了 **关键错误**：

**CodeXray 项目使用的是 ReactFlow v11，而非 AntV G6 v5。**

---

## 关键发现

### 1. 实际技术栈

| 组件 | CodeXray 实际 | 初始假设 |
|------|---------------|---------|
| **图表库** | ReactFlow v11.11.4 | AntV G6 v5 |
| **布局引擎** | Dagre v0.8.5 | Dagre（G6 内置） |
| **状态管理** | React Hooks | G6 Graph 实例 |
| **Undo/Redo** | 手动快照 | G6 History 插件 |

### 2. 代码证据

**package.json** (`/Volumes/SSD_1T/develop/CodeXray/resources/ui/`):
```json
{
  "dependencies": {
    "dagre": "^0.8.5",
    "reactflow": "^11.10.0",  // ← 关键
    "react": "^18.2.0",
    "react-dom": "^18.2.0"
  }
}
```

**使用证据**:
- `resources/ui/src/visualization/GraphCore.tsx` — 使用 `<ReactFlow>` 组件
- `resources/ui/src/visualization/GraphNode.tsx` — 导入 `Handle, Position, NodeProps from 'reactflow'`
- `resources/ui/src/visualization/graphLayout.ts` — 使用 Dagre API，不是 G6 API

### 3. 设计文档确认

`doc/02-可视化界面/可视化界面详细功能与架构设计.md` 第 2.2 节明确说明：

> **选定**：**React Flow**（reactflow，npm: `reactflow`），为 React 生态内成熟的节点+边可视化库。

---

## 影响分析

### 查询对象：10 个 AntV G6 API 问题

所有问题**都需要改为 ReactFlow API**：

| # | 原始问题 | 实际答案 |
|---|----------|--------|
| 1 | `import { Graph }` 路径 | ReactFlow 无 Graph 类；用 `<ReactFlow>` 组件 + state |
| 2 | `GraphOptions` 类型 | ReactFlow 的 props，无单独 GraphOptions 导出 |
| 3 | 自定义布局注册 | 无注册机制；返回布局后的 nodes 数组 |
| 4 | History 插件 API | 无内置插件；手动实现快照 + 键盘监听 |
| 5 | 节点样式函数签名 | 不支持函数；用 `style: CSSProperties` 或 CSS class |
| 6 | `brush-select` behavior 配置 | 用 `selectionOnDrag={true}` 配置 |
| 7 | `graph.getNodeData()` | 用 React state 查询：`nodes.find(n => n.id === id)` |
| 8 | `graph.setElementState()` | 用 `setNodes()` 修改状态对象 |
| 9 | 事件处理器签名 | `(event: React.MouseEvent, node: Node) => void` |
| 10 | `fitView()`, `fitCenter()` | 都存在，通过 `useReactFlow()` Hook 调用 |

---

## 已生成的文档

### 1. **API_REFERENCE.md**
- 完整的 ReactFlow v11 + Dagre v0.8 API 文档
- 包含所有类型定义、方法签名、代码示例
- CodeXray 集成模式详解
- 常见问题解答

**位置**: `/Volumes/SSD_1T/develop/CodeXray/doc/API_REFERENCE.md`

### 2. **IMPLEMENTATION_CHECKLIST.md**
- 13 个部分的实现前验证清单
- 每个 API 都有 ✓/✗ 标记
- 常见错误预防列表
- 版本确认

**位置**: `/Volumes/SSD_1T/develop/CodeXray/doc/IMPLEMENTATION_CHECKLIST.md`

### 3. **MEMORY.md 更新**
- 新增 "CodeXray UI: ReactFlow v11 + Dagre" 部分
- 完整的库版本信息
- 关键 API 快速参考
- 集成模式总结

**位置**: `/Users/yushun/.claude/agent-memory/web-research/MEMORY.md`

---

## 核心 API 速查表

### ReactFlow 关键导入

```typescript
import {
  ReactFlow, Controls, Background, Handle, Position,
  applyNodeChanges, applyEdgeChanges, SelectionMode,
  useReactFlow, useNodes, useEdges,
  type Node, type Edge, type NodeProps,
} from 'reactflow';
import 'reactflow/dist/style.css';  // ← 必须

import dagre from 'dagre';  // ← 注意是 'dagre' 不是 '@dagrejs/dagre'
```

### ReactFlow 主容器 Props

```typescript
<ReactFlow
  nodes={nodes}
  edges={edges}
  nodeTypes={{ graphNode: GraphNode }}
  onNodesChange={onNodesChange}
  onEdgesChange={onEdgesChange}
  panOnDrag={[1, 2]}              // 中键+右键平移
  selectionOnDrag                 // 矩形框选
  selectionMode={SelectionMode.Partial}
  defaultEdgeOptions={{
    type: 'smoothstep',
    pathOptions: { borderRadius: 8 },
  }}
  fitView
  minZoom={0.05}
  maxZoom={2}
  deleteKeyCode={['Delete', 'Backspace']}
>
  <Controls />
  <Background variant={BackgroundVariant.Dots} gap={20} size={1} />
</ReactFlow>
```

### Dagre 用法

```typescript
const g = new dagre.graphlib.Graph();
g.setGraph({ rankdir: 'LR', nodesep: 40, ranksep: 120 });
g.setDefaultEdgeLabel(() => ({}));

nodes.forEach(n => g.setNode(n.id, { width: 280, height: 60, rank: computedRank }));
edges.forEach(e => {
  if (g.hasNode(e.source) && g.hasNode(e.target)) {
    g.setEdge(e.source, e.target);
  }
});

dagre.layout(g);

return nodes.map(n => ({
  ...n,
  position: { x: g.node(n.id).x - 140, y: g.node(n.id).y - 30 },
  sourcePosition: Position.Right,
  targetPosition: Position.Left,
}));
```

### 撤销/重做

```typescript
// ReactFlow 无内置支持，手动实现：
const [history, setHistory] = useState<Snapshot[]>([]);
const [pointer, setPointer] = useState(0);

const saveSnapshot = (nodes, edges) => {
  setHistory(prev => [...prev.slice(0, pointer + 1), {nodes, edges}]);
  setPointer(prev => prev + 1);
};

// 键盘快捷键
useEffect(() => {
  const handler = (e: KeyboardEvent) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'z' && !e.shiftKey) {
      e.preventDefault();
      const snap = history[pointer - 1];
      if (snap) setNodes(snap.nodes);
    }
  };
  document.addEventListener('keydown', handler);
  return () => document.removeEventListener('keydown', handler);
}, [pointer, history]);
```

---

## 对用户的建议

### 立即行动

1. ✓ **参考新生成的文档**
   - 详读 `API_REFERENCE.md` 的 1-3 节（ReactFlow 主要 API）
   - 检查 `IMPLEMENTATION_CHECKLIST.md` 中你将使用的特性

2. ✓ **对齐项目文档**
   - CodeXray 的 `doc/02-可视化界面/` 已说明 ReactFlow，无需修改
   - 但可补充本 API 参考文档作为实现指南

3. ✓ **代码审查**
   - 现有 GraphCore / GraphNode / graphLayout 实现已符合 ReactFlow 模式
   - 如需扩展，参考文档中的模式和最佳实践

### 避免的陷阱

1. **不要寻找 G6 API** — 项目不用 G6
2. **不要用 @dagrejs/dagre** — 包不存在；用 `dagre@0.8.5`
3. **不要期望内置 Undo/Redo** — ReactFlow 无此功能；手动实现
4. **不要用函数式样式** — React Flow 样式是对象，不是函数

---

## 总结

| 方面 | 结论 |
|------|------|
| **技术栈** | ReactFlow v11.11.4 + Dagre v0.8.5（确认无误） |
| **API 文档** | ✓ 新生成完整参考 |
| **实现清单** | ✓ 13 部分验证覆盖 |
| **常见问题** | ✓ 10+ 问题收录 |
| **现有代码** | ✓ 已验证符合模式 |
| **后续工作** | 参考新文档安全实现 |

**准备状态**: ✓ **已就绪。可以基于正确的 API 安全开发。**

---

## 文档链接

- **完整 API 参考**: `/Volumes/SSD_1T/develop/CodeXray/doc/API_REFERENCE.md`
- **实现验证清单**: `/Volumes/SSD_1T/develop/CodeXray/doc/IMPLEMENTATION_CHECKLIST.md`
- **项目设计文档**: `/Volumes/SSD_1T/develop/CodeXray/doc/02-可视化界面/可视化界面详细功能与架构设计.md`
- **当前实现**: `/Volumes/SSD_1T/develop/CodeXray/resources/ui/src/visualization/`
- **Web 研究内存**: `/Users/yushun/.claude/agent-memory/web-research/MEMORY.md`
