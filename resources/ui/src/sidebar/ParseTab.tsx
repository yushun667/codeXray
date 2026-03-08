import React from 'react';
import { Codicon } from '../shared/icons';
import type { ParseRun } from '../shared/types';

interface ParseTabProps {
  projectRoot: string;
  parseProgress: number | null;
  historyRuns: ParseRun[];
  onMessage: ((msg: unknown) => void) | null;
}

export function ParseTab({ projectRoot, parseProgress, historyRuns, onMessage }: ParseTabProps) {
  const send = (action: string, payload?: unknown) => {
    onMessage?.(payload ? { action, payload } : { action });
  };

  return (
    <div className="panel parse-panel">
      <div className="section">
        <label>工程路径</label>
        <div className="project-root">{projectRoot || '(无工作区)'}</div>
      </div>
      <div className="section">
        <button
          className="btn"
          onClick={() => send('runParse')}
          disabled={parseProgress !== null}
        >
          <span className={Codicon.play} /> 解析
        </button>
        <button className="btn" onClick={() => send('listParseHistory')}>
          <span className={Codicon.history} /> 历史
        </button>
      </div>
      {parseProgress !== null && (
        <div className="section progress">解析中… {parseProgress}%</div>
      )}
      <div className="section">
        <label>历史解析记录</label>
        <div className="history-list">
          {historyRuns.length === 0
            ? '无记录'
            : historyRuns.map((r) => (
                <div key={r.run_id || ''} className="history-item">
                  {r.run_id} {r.started_at} {r.mode} {r.status}
                </div>
              ))}
        </div>
      </div>
    </div>
  );
}
