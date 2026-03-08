/**
 * Codicon 名到 class 的映射（与 VSCode Codicon 一致）
 */

export const Codicon = {
  play: 'codicon-play',
  history: 'codicon-history',
  send: 'codicon-send',
  'symbol-reference': 'codicon-symbol-reference',
  project: 'codicon-project',
  'comment-discussion': 'codicon-comment-discussion',
  sync: 'codicon-sync',
  check: 'codicon-check',
  error: 'codicon-error',
  folder: 'codicon-folder',
  'folder-opened': 'codicon-folder-opened',
} as const;

export function getIconClass(name: keyof typeof Codicon): string {
  return Codicon[name] ?? `codicon codicon-${name}`;
}
