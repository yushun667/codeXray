import React, { useState, useEffect } from 'react';
import { ParseTab } from './ParseTab';
import { ChatTab } from './ChatTab';
import { Codicon } from '../shared/icons';
import type { HostToSidebarMessage } from '../shared/protocol';
import type { ParseRun } from '../shared/types';
import { getVscodeApi } from '../shared/vscodeApi';

export function SidebarContainer() {
  const [activeTab, setActiveTab] = useState<'parse' | 'chat'>('parse');
  const [projectRoot, setProjectRoot] = useState<string>('');
  const [parseProgress, setParseProgress] = useState<number | null>(null);
  const [lastParseResult, setLastParseResult] = useState<{
    status: string;
    files_parsed?: number;
    files_failed?: number;
    message?: string;
  } | null>(null);
  const [historyRuns, setHistoryRuns] = useState<ParseRun[]>([]);

  useEffect(() => {
    if (!getVscodeApi()) return;
    getVscodeApi()?.postMessage({ action: 'getProject' });
  }, []);

  useEffect(() => {
    const handler = (event: MessageEvent<HostToSidebarMessage>) => {
      const m = event.data;
      if (!m || typeof m !== 'object') return;
      const action = (m as { action?: string }).action;
      if (action === 'projectInfo' && (m as { project?: { root?: string } }).project) {
        setProjectRoot((m as { project: { root?: string } }).project.root ?? '');
      }
      if (action === 'initState') {
        const pm = m as { projectPath?: string };
        if (pm.projectPath !== undefined) setProjectRoot(pm.projectPath ?? '');
      }
      if (action === 'parseProgress' && typeof (m as { percent?: number }).percent === 'number') {
        setParseProgress((m as { percent: number }).percent);
      }
      if (action === 'parseResult' || action === 'parseDone') {
        setParseProgress(null);
        const res = (m as { result?: { status: string; files_parsed?: number; files_failed?: number; message?: string } }).result;
        if (res) setLastParseResult(res);
      }
      if (action === 'parseHistory' && Array.isArray((m as { runs?: ParseRun[] }).runs)) {
        setHistoryRuns((m as { runs: ParseRun[] }).runs);
      }
    };
    window.addEventListener('message', handler);
    return () => window.removeEventListener('message', handler);
  }, []);

  return (
    <div className="sidebar-container">
      <div className="tabs">
        <button
          className={`tab ${activeTab === 'parse' ? 'active' : ''}`}
          onClick={() => setActiveTab('parse')}
        >
          <span className={Codicon.project} /> 解析管理
        </button>
        <button
          className={`tab ${activeTab === 'chat' ? 'active' : ''}`}
          onClick={() => setActiveTab('chat')}
        >
          <span className={Codicon['comment-discussion']} /> AI 对话
        </button>
      </div>
      {activeTab === 'parse' && (
        <ParseTab
          projectRoot={projectRoot}
          parseProgress={parseProgress}
          lastParseResult={lastParseResult}
          historyRuns={historyRuns}
          onMessage={(msg) => getVscodeApi()?.postMessage(msg)}
        />
      )}
      {activeTab === 'chat' && <ChatTab onMessage={(msg) => getVscodeApi()?.postMessage(msg)} />}
    </div>
  );
}
