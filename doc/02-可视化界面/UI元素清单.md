# CodeXray 全部 UI 元素清单

本文档在**单独文件**中列出扩展内出现的**所有 UI 元素**，按来源与位置分类。实现文件与 `package.json` 贡献点一并标注，便于维护与对照。

---

## 1. VSCode 扩展贡献点（package.json）

### 1.1 活动栏

| 元素 | 类型 | contributes | 说明 |
|------|------|-------------|------|
| CodeXray 图标 | 活动栏图标 | `viewsContainers.activitybar` | id: `codexray`，title: "CodeXray"，icon: `$(circuit-board)`，点击后显示侧边栏 |

### 1.2 侧边栏视图

| 元素 | 类型 | contributes | 说明 |
|------|------|-------------|------|
| CodeXray 侧边栏 | Webview 视图 | `views.codexray` | type: webview，id: `codexray.sidebar`，name: "CodeXray"；内容为 `resources/ui/dist/sidebar.html`（React 双标签） |

### 1.3 命令（命令面板 / 快捷键）

| 命令 ID | 标题 | 说明 |
|---------|------|------|
| `codexray.openProject` | CodeXray: 打开/选择工程目录 | 选择工程根目录 |
| `codexray.setCompileCommands` | CodeXray: 设置 compile_commands.json 路径 | 设置编译数据库路径 |
| `codexray.runParse` | CodeXray: 执行解析 | 触发解析 |
| `codexray.listParseHistory` | CodeXray: 查看历史解析记录 | 拉取并展示历史解析列表（侧边栏等） |
| `codexray.openAIChat` | CodeXray: 打开 AI 对话 | 聚焦/打开侧边栏 AI 对话 |
| `codexray.gotoSymbolInEditor` | CodeXray: 定位到代码 | 内部用，when: "false"；由图视图节点点击触发 |
| `codexray.queryCallGraph` | CodeXray: 查看调用链 | 以当前符号查询调用链并在编辑区打开图 |
| `codexray.queryClassGraph` | CodeXray: 查看类关系 | 以当前符号查询类关系图 |
| `codexray.queryDataFlow` | CodeXray: 查看数据流 | 以当前符号查询数据流图 |
| `codexray.queryControlFlow` | CodeXray: 查看控制流 | 以当前符号查询控制流图 |

### 1.4 编辑器右键菜单（editor/context）

| 菜单项 | 命令 | when | 说明 |
|--------|------|------|------|
| CodeXray: 查看调用链 | `codexray.queryCallGraph` | editorLangId == c \|\| editorLangId == cpp | C/C++ 文件中显示 |
| CodeXray: 查看类关系 | `codexray.queryClassGraph` | 同上 | 同上 |
| CodeXray: 查看数据流 | `codexray.queryDataFlow` | 同上 | 同上 |
| CodeXray: 查看控制流 | `codexray.queryControlFlow` | 同上 | 同上 |

### 1.5 设置页（configuration）

| 配置键 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `codexray.compileCommandsPath` | string | "" | compile_commands.json 路径 |
| `codexray.databasePath` | string | "" | 本地数据库路径 |
| `codexray.parserParallelism` | number | 0 | 解析并行度 |
| `codexray.parserLazy` | boolean | true | 启用懒解析模式 |
| `codexray.parserPriorityDirs` | array of string | [] | 优先解析目录 |
| `codexray.parserIncremental` | boolean | true | 默认使用增量更新解析 |
| `codexray.queryDepth` | number | 2 | 图查询默认展开深度（1–10） |
| `codexray.agentEndpoint` | string | "" | Agent 服务地址 |
| `codexray.llmProvider` | string | "" | 大模型 provider |
| `codexray.llmModel` | string | "" | 大模型标识 |
| `codexray.llmEndpoint` | string | "" | 大模型 API 端点 |
| `codexray.llmApiKey` | string | "" | API Key |
| `codexray.logLevel` | string | "info" | 日志级别（debug/info/warn/error） |
| `codexray.logPath` | string | "" | 日志落盘路径 |

---

## 2. 侧边栏 Webview 内 UI 元素（resources/ui）

入口：`sidebar.html` → `SidebarContainer.tsx`。

### 2.1 容器与标签切换

| 元素 | 组件 | 类型 | 说明 |
|------|------|------|------|
| 标签「解析管理」 | SidebarContainer | 按钮（Tab） | Codicon `project`，点击切换至解析管理 |
| 标签「AI 对话」 | SidebarContainer | 按钮（Tab） | Codicon `comment-discussion`，点击切换至 AI 对话 |

### 2.2 解析管理标签页（ParseTab.tsx）

| 元素 | 类型 | 说明 |
|------|------|------|
| 工程路径 | 只读文本 | 显示当前工程根路径；(无工作区) 时显示「(无工作区)」 |
| 按钮「解析」 | 按钮 | Codicon `play`；action: `runParse`；解析中禁用 |
| 按钮「历史」 | 按钮 | Codicon `history`；action: `listParseHistory` |
| 解析进度文案 | 文本 | 解析中显示「解析中… N%」 |
| 最近解析结果摘要 | 文本 | 成功：「最近: N 个文件, M 失败」；失败：「解析失败: …」 |
| 历史解析记录列表 | 列表 | 每项：run_id、started_at、mode、status；空时「无记录」 |

### 2.3 AI 对话标签页（ChatTab.tsx）

| 元素 | 类型 | 说明 |
|------|------|------|
| 消息列表 | 可滚动区域 | 用户/助手消息气泡，Markdown 渲染（react-markdown + remark-gfm） |
| 流式回复气泡 | 消息气泡 | 回复未完成时显示已收到的流式内容 |
| 按钮「引用当前符号」 | 按钮 | Codicon `symbol-reference`；action: `getContext` |
| 输入框 | textarea | 多行 rows=3，placeholder「输入消息…」，Enter 发送、Shift+Enter 换行 |
| 按钮「发送」 | 按钮 | Codicon `send`；发送当前输入，action: `sendChat` |

---

## 3. 编辑区图视图 Webview 内 UI 元素（resources/ui）

入口：`graph.html` → `GraphPage.tsx`。

### 3.1 图页整体（GraphPage.tsx）

| 元素 | 类型 | 说明 |
|------|------|------|
| 加载中占位 | 整页文案 | 未 ready 时：「加载图中…」 |
| 空结果占位 | 整页文案 | 无节点时：「查询结果为空。请先解析工程，或在 C/C++ 文件中选中符号后右键「查看调用链」等。」 |
| 边数截断提示条 | 顶部横幅 | 边数 > 2500 时：「边数量较多，已按节点对去重并仅展示前 2500 条（原始约 N 条），以保持流畅。」 |

### 3.2 图核心画布（GraphCore.tsx）

| 元素 | 类型 | 说明 |
|------|------|------|
| 画布 | ReactFlow 容器 | 节点、边、拖拽、框选、缩放、平移 |
| 背景 | Background | 网格背景 |
| 缩放/适应控件 | Controls | React Flow 自带：缩放、适应视图等 |
| 图节点 | GraphNode | 自定义节点：左/右 Handle；框内多行 label；title 悬停全文；点击 postMessage(gotoSymbol) 跳转代码 |
| 边 | Edge | smoothstep、圆角；按 (source,target) 去重，总数上限 2500 |

### 3.3 节点右键菜单（GraphContextMenu.tsx）

| 元素 | 类型 | 说明 |
|------|------|------|
| 浮层菜单 | 固定定位 | 出现在右键位置 |
| 菜单项「继续查询前置节点」 | 按钮 | postMessage `queryPredecessors` |
| 菜单项「继续查询后置节点」 | 按钮 | postMessage `querySuccessors` |

---

## 4. 主仓库内非 Webview UI 元素（src）

### 4.1 状态栏（statusBar.ts）

| 元素 | 类型 | 说明 |
|------|------|------|
| CodeXray 状态栏项 | StatusBarItem | 右侧；解析中：「$(sync~spin) CodeXray 解析 N%」；完成：「$(check) CodeXray 解析完成」；失败：「$(error) CodeXray 解析失败」；可隐藏 |

### 4.2 编辑区图标签（visualizationProvider.ts）

| 元素 | 类型 | 说明 |
|------|------|------|
| 图 Webview 面板 | WebviewPanel | 编辑区新 Tab，标题随图类型；内容 `resources/ui/dist/graph.html` |

### 4.3 编辑器内提示（editorIntegration.ts 等）

| 元素 | 类型 | 说明 |
|------|------|------|
| 警告提示 | showWarningMessage | 执行「查看调用链」等时若未选中符号：「请将光标置于符号上或选中符号后再执行「…」」 |

---

## 5. 汇总统计

| 分类 | 数量 |
|------|------|
| 活动栏项 | 1 |
| 侧边栏视图 | 1 |
| 命令 | 10 |
| 编辑器右键菜单项 | 4 |
| 设置项 | 14 |
| 侧边栏内控件/文本/列表等 | 12+ |
| 图视图内控件/画布/节点/菜单等 | 11+ |
| 状态栏项 | 1 |
| 图 Webview 面板 | 1（多实例） |
| 编辑器内提示 | 1（按需弹出） |

以上为当前 CodeXray 扩展中**全部 UI 元素**的完整清单。
