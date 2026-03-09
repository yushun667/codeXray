# 主仓库 UI 元素记录

本文档记录扩展内添加的 UI 元素：位置、作用、交互逻辑。

## 1. 侧边栏 · CodeXray

| 位置 | 说明 | 交互逻辑 |
|------|------|----------|
| 活动栏 | CodeXray 图标（与资源管理器、搜索等并列） | 点击后侧边栏显示 CodeXray 视图 |
| 侧边栏视图 | 单 Webview，内嵌两个标签「解析管理」「AI 对话」 | 通过标签按钮切换显示对应面板；内容优先加载 **resources/ui/dist/sidebar.html**（React 构建产物），失败时回退内联 HTML |

### 1.1 解析管理标签页

| 元素 | 位置 | 作用 | 交互逻辑 |
|------|------|------|----------|
| 工程路径 | 面板顶部只读文本 | 显示当前工作区根路径 | 无交互；由 getProject / initState 回填 |
| 「解析」按钮 | 解析管理面板 | 触发全量/增量解析 | 点击 → postMessage runParse → 扩展调用 parserService.parse()，状态栏与侧栏显示进度，结果通过 parseResult 回传 |
| 「历史」按钮 | 解析管理面板 | 查看历史解析记录 | 点击 → postMessage listParseHistory → 扩展调用 parserService.listRuns()，列表通过 parseHistory 回传 |
| 解析进度（解析中… N%） | 解析管理面板、解析按钮下方 | 解析进行时显示百分比 | 由 parseProgress 消息更新；解析结束即隐藏 |
| 解析结果摘要（最近: X 个文件, Y 失败 / 解析失败: …） | 解析管理面板、进度下方 | 最近一次解析结果摘要 | 由 parseResult 消息更新；成功时显示 files_parsed、files_failed，失败时显示 message |
| 历史解析记录列表 | 解析管理面板下方 | 展示 run_id、时间、mode、status | 只读展示；由 parseHistory 消息更新 |

### 1.2 AI 对话标签页

| 元素 | 位置 | 作用 | 交互逻辑 |
|------|------|------|----------|
| 消息列表 | 面板上方区域 | 展示用户与 Agent 对话 | 用户发送后追加 user 消息；Agent 回复通过 chatReply 追加 assistant 消息 |
| 「引用当前符号」按钮 | 消息列表下方 | 将当前编辑器符号作为上下文 | 点击 → postMessage getContext → 扩展取 getCurrentSymbol()，回传 context，输入框 placeholder 显示当前符号 |
| 输入框 | 多行文本框 | 输入要发送的消息 | 用户输入后点击发送 |
| 「发送」按钮 | 输入框下方 | 发送消息给 Agent | 点击 → postMessage sendChat(message) → 扩展调用 agentService.sendChat()，回复经 onReply 以 chatReply 回传并追加到消息列表 |

## 2. 编辑区 · 图标签

| 位置 | 说明 | 交互逻辑 |
|------|------|----------|
| 编辑区新标签 | WebviewPanel 加载 **resources/ui/dist/graph.html**，与代码文件标签并列 | 通过编辑器右键「查看调用链/类关系/数据流/控制流」或命令面板打开；标题为「CodeXray · 调用链图」等 |

### 2.1 图画布

| 元素 | 位置 | 作用 | 交互逻辑 |
|------|------|------|----------|
| 画布 | 图标签主体 | 展示节点与边（React Flow） | 支持缩放（滚轮）、平移（拖拽空白）、fitView |
| 节点 | 画布上 | 表示函数/类/变量/基本块等 | 单击 → postMessage(gotoSymbol) → 扩展执行 showTextDocument + revealRange 定位到代码；可选中、框选、拖拽移动、Delete 删除 |
| 边 | 节点之间 | 表示调用/继承/数据流/控制流 | 随节点布局显示 |
| 右键菜单 | 节点右键 | 扩展查询 | 显示「继续查询前置节点」「继续查询后置节点」→ postMessage(queryPredecessors/querySuccessors) → 扩展 query 后 graphAppend 合并到当前图 |
| 控件 | 画布一角 | 缩放等 | React Flow Controls 组件 |

### 2.2 图与 Host 消息协议

- **图 → Host**：`gotoSymbol`（file, line, column）、`queryPredecessors`、`querySuccessors`（graphType, nodeId, symbol, file）。
- **Host → 图**：`initGraph`（graphType, nodes, edges）、`graphAppend`（nodes, edges）。

## 3. 状态栏

| 位置 | 说明 | 交互逻辑 |
|------|------|----------|
| 状态栏右侧 | 解析进度 / 完成 / 失败 | runParse 进行中显示「CodeXray 解析 N%」；完成显示「解析完成」；失败显示「解析失败」 | 由 parserService.onProgress 与 runParse 结果驱动，无用户点击 |

## 4. 消息协议（postMessage）补充

- **Host → 侧边栏**：`initState`（初次加载 projectPath、compileCommandsPath）、`parseProgress`（percent）、`replyChunk`（流式片段）、`replyDone`、`projectInfo`、`parseHistory`、`parseResult`、`chatReply`、`context`、`error`。
- **侧边栏 → Host**：`runParse`、`listParseHistory`、`getProject`、`setCompileCommands`、`sendChat`、`getContext`。
- **Host → 图**：`initGraph`、`graphAppend`（见 2.2）。
- **图 → Host**：`gotoSymbol`、`queryPredecessors`、`querySuccessors`（见 2.2）。

## 5. 命令面板与编辑器菜单

所有命令均在命令面板中暴露：打开工程、设置 compile_commands、执行解析、查看历史、打开 AI 对话、定位到代码、查看调用链/类关系/数据流/控制流等。C/C++ 编辑器右键菜单（when: editorLangId 为 c 或 cpp）提供「CodeXray: 查看调用链」「CodeXray: 查看类关系」「CodeXray: 查看数据流」「CodeXray: 查看控制流」；交互逻辑见 extension.ts、editorIntegration.ts、visualizationProvider.ts 与上文对应 UI。
