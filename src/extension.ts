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
import { registerEditorCommands } from './editor/editorIntegration';
import { VisualizationProvider } from './views/visualizationProvider';
import { SidebarView } from './views/sidebarView';

const log = createLogger('extension');

let config: Config;
let parserService: ParserService;
let agentService: AgentService;
let statusBar: StatusBar;
let visualizationProvider: VisualizationProvider;
let sidebarView: SidebarView;

export function activate(context: vscode.ExtensionContext): void {
  log.info('activate 开始');

  config = new Config();
  config.init(context);
  setLogPath(config.getLogPath() ?? undefined);
  setLogLevel(config.getLogLevel());

  const root = config.getWorkspaceRoot();
  const parserPath = config.getParserPath();
  log.info('CodeXray 已激活', { workspaceRoot: root?.fsPath ?? '(无)', parserPath });

  parserService = new ParserService(config);
  agentService = new AgentService(config);
  statusBar = new StatusBar();
  visualizationProvider = new VisualizationProvider();
  sidebarView = new SidebarView(context);
  sidebarView.setDeps({ config, parserService, agentService });

  context.subscriptions.push(
    vscode.window.registerWebviewViewProvider('codexray.sidebar', sidebarView)
  );

  parserService.onProgress((percent) => {
    statusBar.updateProgress(percent);
  });

  context.subscriptions.push(
    registerGotoSymbolCommand(context)
  );

  registerEditorCommands(context, parserService, visualizationProvider);

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
    vscode.commands.registerCommand('codexray.focusVisualization', () => {
      void vscode.window.showInformationMessage('请从编辑器右键或查询命令打开可视化标签后，点击对应标签聚焦。');
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
  visualizationProvider?.dispose();
}
