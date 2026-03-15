# CodeXray UI API 研究 - 完整文档索引

**日期**: 2026-03-14
**研究范围**: ReactFlow v11.11.4 + Dagre v0.8.5 API 完整验证
**状态**: ✓ 已完成

---

## 📋 核心发现

**原始假设**: 项目使用 AntV G6 v5
**实际情况**: 项目使用 ReactFlow v11 + Dagre v0.8
**文档生成**: 4 份完整参考文档

---

## 📚 文档清单

### 1. **RESEARCH_SUMMARY.md** ← 阅读这个来了解全貌
**位置**: `/Volumes/SSD_1T/develop/CodeXray/doc/RESEARCH_SUMMARY.md`

**内容**:
- 调查过程与关键发现
- 实际 vs 假设的技术栈对比
- 10 个问题的快速答案表
- 核心 API 速查表
- 避免的陷阱列表
- 总体结论与准备状态

**何时阅读**:
- 快速了解情况（5 分钟）
- 想知道发生了什么（管理者视角）

---

### 2. **ANSWERS_TO_10_QUESTIONS.md** ← 直接回答你的问题
**位置**: `/Volumes/SSD_1T/develop/CodeXray/doc/ANSWERS_TO_10_QUESTIONS.md`

**内容**:
- 原始 10 个问题逐一回答
- 每个问题配完整代码示例
- CodeXray 项目中的实际用法
- 对比 AntV G6 vs ReactFlow 的差异
- 快速对照表
- 快速开发指南

**何时阅读**:
- 实现某个具体功能时
- 想知道某个 API 的确切用法
- 对比 G6 和 ReactFlow 差异

**问题覆盖**:
1. 导入路径与类型名称
2. 自定义布局注册 API
3. History 插件 API
4. 节点样式函数签名
5. Brush-select 行为配置
6. getNodeData/getEdgeData
7. setElementState
8. addNodeData/removeNodeData
9. 事件处理器签名
10. fitView/fitCenter

---

### 3. **API_REFERENCE.md** ← 完整 API 手册
**位置**: `/Volumes/SSD_1T/develop/CodeXray/doc/API_REFERENCE.md`

**内容**:
- **第 1 节**: ReactFlow v11 完整 API（1000+ 行）
  - 导入路径详解
  - 节点 & 边类型定义
  - 变更事件类型
  - 主容器 Props（30+ 配置）
  - Handle 用法
  - 事件处理模式
  - 工具函数与 Hooks
  - 撤销/重做实现
  - 常见问题解答

- **第 2 节**: Dagre v0.8 布局 API
  - 图对象创建
  - 节点/边添加
  - 布局执行
  - 结果提取
  - 操作方法
  - 重要限制与秩约束

- **第 3 节**: CodeXray 集成模式
  - 数据流向图
  - 自定义节点实现
  - 布局与合并
  - 撤销/重做机制

- **第 4 节**: 类型定义
  - GraphType 枚举
  - FlowNodeData 接口
  - 消息类型

**何时阅读**:
- 深入学习某个组件或 API
- 了解完整的类型结构
- 寻找特定的配置选项
- 看完整的代码示例

**特点**:
- 每个 API 都有类型签名
- 所有代码都有注释
- CodeXray 项目中的实际用法示例

---

### 4. **IMPLEMENTATION_CHECKLIST.md** ← 开发前验证
**位置**: `/Volumes/SSD_1T/develop/CodeXray/doc/IMPLEMENTATION_CHECKLIST.md`

**内容**:
- **13 个检查部分**:
  1. 导入路径与类型名称
  2. 自定义布局注册
  3. History 插件
  4. 节点样式函数
  5. Brush-select 配置
  6. Graph API 方法
  7. 数据操作
  8. 事件处理
  9. 视图操作
  10. Dagre 特定 API
  11. CodeXray 特定实现
  12. 版本确认
  13. 常见错误预防

- **每项包含**:
  - ✓/✗ 验证标记
  - 代码示例
  - 关键说明
  - 常见错误

**何时阅读**:
- 开始编码前（清单）
- 避免常见错误
- 验证依赖版本
- 确认理解正确

**特点**:
- 清单式格式，易于逐项确认
- 所有错误列举与修正
- 版本检查命令

---

## 🔗 文档关系图

```
RESEARCH_SUMMARY.md (总结，5 分钟快读)
  ↓
  ├─→ ANSWERS_TO_10_QUESTIONS.md (逐问回答)
  │   └─→ 实现某个功能时参考
  │
  ├─→ API_REFERENCE.md (完整手册)
  │   └─→ 深入学习某个 API
  │
  └─→ IMPLEMENTATION_CHECKLIST.md (开发清单)
      └─→ 开始编码前完成
```

---

## 📖 按用途选择阅读

### "我想快速了解发生了什么"
→ **RESEARCH_SUMMARY.md** (5 分钟)

### "我想知道如何实现特定功能"
→ **ANSWERS_TO_10_QUESTIONS.md** 对应章节 (15 分钟)

### "我想深入学习某个 API"
→ **API_REFERENCE.md** 对应章节 (30-60 分钟)

### "我想开始编码但要确保没有遗漏"
→ **IMPLEMENTATION_CHECKLIST.md** (30 分钟)

### "我需要完整的参考资料"
→ 全部阅读，按顺序：总结 → 问答 → 手册 → 清单

---

## 🎯 关键速查

### ReactFlow 基础

```typescript
// 导入
import { ReactFlow, Handle, Position, ... } from 'reactflow';
import 'reactflow/dist/style.css';

// 组件
<ReactFlow
  nodes={nodes}
  edges={edges}
  nodeTypes={{ graphNode: GraphNode }}
  onNodesChange={onNodesChange}
  onEdgesChange={onEdgesChange}
  panOnDrag={[1, 2]}
  selectionOnDrag
  selectionMode={SelectionMode.Partial}
>
  <Controls />
  <Background />
</ReactFlow>

// Hook
const { getNode, setNodes, fitView } = useReactFlow();
```

### Dagre 基础

```typescript
const g = new dagre.graphlib.Graph();
g.setGraph({ rankdir: 'LR', nodesep: 40, ranksep: 120 });
g.setDefaultEdgeLabel(() => ({}));

nodes.forEach(n => g.setNode(n.id, { width: 280, height: 60 }));
edges.forEach(e => g.setEdge(e.source, e.target));

dagre.layout(g);

return nodes.map(n => ({...n, position: { x: g.node(n.id).x - 140, y: ... }}));
```

### 常见操作

```typescript
// 获取节点数据
const node = nodes.find(n => n.id === 'id');

// 修改节点状态
setNodes(prev => prev.map(n => n.id === 'id' ? {...n, selected: true} : n));

// 添加节点
setNodes(prev => [...prev, newNode]);

// 移除节点
setNodes(prev => prev.filter(n => !targetIds.has(n.id)));

// 撤销/重做
const [history, pointer] = useRef([]), useRef(0);
const undo = () => pointer-- && setNodes(history[pointer].nodes);
```

---

## 📌 重要提醒

### ✗ 不要做

- ✗ 寻找 `Graph` 类或 `GraphOptions` 类型
- ✗ 使用 `@dagrejs/dagre`（不存在）
- ✗ 期望内置 Undo/Redo 插件
- ✗ 用函数式样式（`fill: (d) => ...`）
- ✗ 调用 `getNodeData()`, `setElementState()` 等不存在的方法
- ✗ 配置 `behaviors` 系统（ReactFlow 没有）

### ✓ 应该做

- ✓ 导入 `reactflow` 包
- ✓ 使用 `<ReactFlow>` 组件与 React state
- ✓ 手动实现 Undo/Redo（快照 + 键盘）
- ✓ 用 CSS class 和对象样式
- ✓ 通过 React state 访问数据
- ✓ 用 props 配置交互（`selectionOnDrag`, `panOnDrag` 等）

---

## 🔧 版本确认

```bash
npm list reactflow dagre

# 应该输出
├── dagre@0.8.5
└── reactflow@11.11.4
```

---

## 📂 项目文件结构参考

```
/Volumes/SSD_1T/develop/CodeXray/
├── doc/
│   ├── RESEARCH_SUMMARY.md            ← 总结
│   ├── ANSWERS_TO_10_QUESTIONS.md     ← 问答
│   ├── API_REFERENCE.md               ← 手册
│   ├── IMPLEMENTATION_CHECKLIST.md    ← 清单
│   ├── 02-可视化界面/                  ← 原项目文档
│   │   └── 可视化界面详细功能与架构设计.md
│   └── ...
├── resources/ui/
│   ├── src/
│   │   ├── visualization/
│   │   │   ├── GraphCore.tsx          ← ReactFlow 主容器
│   │   │   ├── GraphNode.tsx          ← 自定义节点
│   │   │   ├── graphLayout.ts         ← Dagre 布局
│   │   │   ├── GraphContextMenu.tsx   ← 右键菜单
│   │   │   └── ...
│   │   ├── shared/
│   │   │   ├── protocol.ts            ← 通信协议
│   │   │   └── types.ts               ← 类型定义
│   │   └── ...
│   ├── package.json                   ← reactflow 11.11.4, dagre 0.8.5
│   └── ...
└── ...
```

---

## 🚀 下一步

1. **快速上手** (15 分钟)
   - 阅读 RESEARCH_SUMMARY.md
   - 浏览 ANSWERS_TO_10_QUESTIONS.md

2. **深入学习** (60 分钟)
   - 学习 API_REFERENCE.md 的需要部分
   - 查看项目中的实际代码

3. **开始编码** (前置)
   - 完成 IMPLEMENTATION_CHECKLIST.md
   - 确认理解所有要点
   - 创建测试文件验证理解

4. **编码实现** (安心开发)
   - 参考文档中的代码模式
   - 遇到问题时查阅对应章节
   - CodeXray 项目代码是最好的参考

---

## 📞 常见问题速查

| 问题 | 位置 |
|------|------|
| 如何导入 ReactFlow？ | API_REFERENCE.md 第 1.1 节 |
| 如何实现撤销/重做？ | API_REFERENCE.md 第 1.10 节 |
| 如何添加/移除节点？ | ANSWERS_TO_10_QUESTIONS.md Q8 |
| 如何自定义节点样式？ | ANSWERS_TO_10_QUESTIONS.md Q4 |
| Dagre 的秩约束怎么用？ | API_REFERENCE.md 第 2.8 节 |
| 如何处理事件？ | ANSWERS_TO_10_QUESTIONS.md Q9 |
| 拖拽时碰撞检测？ | API_REFERENCE.md 第 3.3 节 |
| 所有版本都正确吗？ | IMPLEMENTATION_CHECKLIST.md 第 12 节 |

---

## ✅ 完成状态

- [x] 技术栈确认（ReactFlow v11 + Dagre v0.8）
- [x] 10 个原始问题的完整回答
- [x] 完整 API 参考文档（1000+ 行）
- [x] 实现前验证清单
- [x] 常见错误预防列表
- [x] 代码示例与模式
- [x] 与项目现有代码的集成说明
- [x] Web 研究内存更新

**状态**: ✓ 所有资料已准备，可以安全开发

---

## 🎓 学习路径建议

### 初级开发者
1. 阅读 RESEARCH_SUMMARY.md
2. 阅读 ANSWERS_TO_10_QUESTIONS.md
3. 查看 CodeXray 现有代码（src/visualization/）
4. 参考 API_REFERENCE.md 实现新功能

### 有经验的开发者
1. 浏览 RESEARCH_SUMMARY.md（快速了解）
2. 按需查阅 ANSWERS_TO_10_QUESTIONS.md
3. API_REFERENCE.md 作为参考手册
4. 直接编码

### 架构师/PM
1. 阅读 RESEARCH_SUMMARY.md
2. 检查版本确认部分
3. 了解技术栈变更（G6 → ReactFlow）

---

**祝编码愉快！** 🚀
