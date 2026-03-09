/**
 * 扩展入口：组装 Config、Logger、Services、Views，注册命令与视图
 * 参考：主仓库详细功能与架构设计 §4.1
 */

import * as vscode from 'vscode';
import { Config } from './config';
import { createLogger, setLogLevel, setLogPath } from './logger';
import { ParserService } from './services/parserService';
import { AgentService } from './services/agentService';
import { StatusBar } from './statusBar';
import { registerGotoSymbolCommand } from './editor/gotoSymbol';
import { SidebarView } from './views/sidebarView';

const log = createLogger('extension');

let config: Config;
let parserService: ParserService;
let agentService: AgentService;
let statusBar: StatusBar;
let sidebarView: SidebarView;

export function activate(context: vscode.ExtensionContext): void {
  log.info('activate 开始');

  try {
    config = new Config();
    config.init(context);
    setLogPath(config.getLogPath() ?? undefined);
    setLogLevel(config.getLogLevel());
  } catch (e) {
    log.error('Config 初始化失败', e instanceof Error ? e.message : String(e));
    throw e;
  }

  const root = config.getWorkspaceRoot();
  const parserPath = config.getParserPath();
  log.info('CodeXray 已激活', { workspaceRoot: root?.fsPath ?? '(无)', parserPath });

  parserService = new ParserService(config);
  agentService = new AgentService(config);
  statusBar = new StatusBar();
  sidebarView = new SidebarView(context);
  sidebarView.setDeps({ config, parserService, agentService, statusBar });

  // 必须先注册视图提供程序，否则侧边栏会报「没有可提供视图数据的已注册数据提供程序」
  context.subscriptions.push(
    vscode.window.registerWebviewViewProvider('codexray.sidebar', sidebarView, {
      webviewOptions: { retainContextWhenHidden: true },
    })
  );

  // 激活后聚焦侧边栏视图，确保 resolveWebviewView 被调用
  void vscode.commands.executeCommand('workbench.view.extension.codexray');

  parserService.onProgress((percent) => {
    statusBar.updateProgress(percent);
    sidebarView.postToWebview({ action: 'parseProgress', percent });
  });

  context.subscriptions.push(
    registerGotoSymbolCommand(context)
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('codexray.runParse', async () => {
      statusBar.updateProgress(0);
      const result = await parserService.parse();
      if (result.status === 'ok') {
        statusBar.setDone();
        sidebarView.postToWebview({ action: 'parseResult', result });
      } else {
        statusBar.setFailed(result.message);
        sidebarView.postToWebview({ action: 'parseResult', result });
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('codexray.listParseHistory', async () => {
      const runs = await parserService.listRuns(20);
      sidebarView.postToWebview({ action: 'parseHistory', runs });
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('codexray.openAIChat', () => {
      void vscode.commands.executeCommand('workbench.view.extension.codexray');
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('codexray.openProject', () => {
      void vscode.commands.executeCommand('vscode.openFolder');
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('codexray.setCompileCommands', async () => {
      const current = config.getCompileCommandsPath();
      const value = await vscode.window.showInputBox({
        title: 'CodeXray: compile_commands.json 路径',
        value: current || 'compile_commands.json',
        placeHolder: '相对工作区根或绝对路径',
      });
      if (value !== undefined) {
        const cfg = vscode.workspace.getConfiguration('codexray');
        await cfg.update('compileCommandsPath', value.trim(), vscode.ConfigurationTarget.Workspace);
        void vscode.window.showInformationMessage('已更新 compile_commands 路径');
      }
    })
  );

  void agentService.connect();

  log.info('activate 完成');
}

export function deactivate(): void {
  log.info('deactivate');
  parserService?.dispose();
  agentService?.dispose();
  statusBar?.dispose();
}
