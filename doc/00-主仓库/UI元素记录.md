# 主仓库 UI 元素记录

本文档记录扩展内添加的 UI 元素：位置、作用、交互逻辑。

## 1. 侧边栏 · CodeXray

| 位置 | 说明 | 交互逻辑 |
|------|------|----------|
| 活动栏 | CodeXray 图标（与资源管理器、搜索等并列） | 点击后侧边栏显示 CodeXray 视图 |
| 侧边栏视图 | 单 Webview，内嵌两个标签「解析管理」「AI 对话」 | 通过标签按钮切换显示对应面板 |

### 1.1 解析管理标签页

| 元素 | 位置 | 作用 | 交互逻辑 |
|------|------|------|----------|
| 工程路径 | 面板顶部只读文本 | 显示当前工作区根路径 | 无交互；由 getProject 消息回填 |
| 「解析」按钮 | 解析管理面板 | 触发全量/增量解析 | 点击 → postMessage runParse → 扩展调用 parserService.parse()，状态栏显示进度，结果通过 parseResult 回传 |
| 「历史」按钮 | 解析管理面板 | 查看历史解析记录 | 点击 → postMessage listParseHistory → 扩展调用 parserService.listRuns()，列表通过 parseHistory 回传 |
| 历史解析记录列表 | 解析管理面板下方 | 展示 run_id、时间、mode、status | 只读展示；由 parseHistory 消息更新 |

### 1.2 AI 对话标签页

| 元素 | 位置 | 作用 | 交互逻辑 |
|------|------|------|----------|
| 消息列表 | 面板上方区域 | 展示用户与 Agent 对话 | 用户发送后追加 user 消息；Agent 回复通过 chatReply 追加 assistant 消息 |
| 「引用当前符号」按钮 | 消息列表下方 | 将当前编辑器符号作为上下文 | 点击 → postMessage getContext → 扩展取 getCurrentSymbol()，回传 context，输入框 placeholder 显示当前符号 |
| 输入框 | 多行文本框 | 输入要发送的消息 | 用户输入后点击发送 |
| 「发送」按钮 | 输入框下方 | 发送消息给 Agent | 点击 → postMessage sendChat(message) → 扩展调用 agentService.sendChat()，回复经 onReply 以 chatReply 回传并追加到消息列表 |

## 2. 编辑器

| 位置 | 说明 | 交互逻辑 |
|------|------|----------|
| C/C++ 文件右键菜单 | 当 editorLangId 为 c 或 cpp 时 | 显示「CodeXray: 查看调用链」「查看类关系图」「查看数据流」「查看控制流」 | 点击 → 以当前光标/选区符号调用 parserService.query(type) → visualizationProvider.openGraph(type, data) 在编辑区新标签打开图 |
| 可视化标签（编辑区） | 与代码文件标签并列 | 展示调用链/类图/数据流/控制流 | 节点点击 → postMessage gotoSymbol(uri, line, column) → 扩展执行 gotoSymbol.execute()，编辑器定位到对应文件行列 |

## 3. 状态栏

| 位置 | 说明 | 交互逻辑 |
|------|------|----------|
| 状态栏右侧 | 解析进度 / 完成 / 失败 | runParse 进行中显示「CodeXray 解析 N%」；完成显示「解析完成」；失败显示「解析失败」 | 由 parserService.onProgress 与 runParse 结果驱动，无用户点击 |

## 4. 命令面板

所有命令均在命令面板中暴露：打开工程、设置 compile_commands、执行解析、查看历史、四种查询、打开 AI 对话、聚焦可视化、定位到代码等；交互逻辑见各命令在 extension.ts 中的实现与上文对应 UI。
