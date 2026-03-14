import type { ThemeColors } from '../types';

/** Get theme colors based on dark/light mode */
export function getThemeColors(isDark: boolean): ThemeColors {
  if (isDark) {
    return {
      rootFill: '#c17d37',
      rootStroke: '#e8a145',
      nodeFill: '#2d6e8e',
      nodeStroke: '#3a8ab0',
      edgeStroke: '#8a9bae',
      highlightStroke: '#f0c040',
      selectedStroke: '#ff9800',
      dimmedOpacity: 0.15,
      pathAnimColor: '#f0c040',
      menuBg: '#252526',
      menuBorder: '#454545',
      menuText: '#cccccc',
      menuTextDanger: '#f48771',
      menuHover: '#094771',
      toolbarBg: '#1e1e1e',
      toolbarBorder: '#333333',
      bg: '#1e1e1e',
      fg: '#d4d4d4',
      searchBg: '#3c3c3c',
      searchBorder: '#555555',
    };
  }
  return {
    rootFill: '#d4873d',
    rootStroke: '#b06e2a',
    nodeFill: '#519aba',
    nodeStroke: '#3a7a9a',
    edgeStroke: '#bbbbbb',
    highlightStroke: '#d4a017',
    selectedStroke: '#e68a00',
    dimmedOpacity: 0.15,
    pathAnimColor: '#d4a017',
    menuBg: '#f3f3f3',
    menuBorder: '#cccccc',
    menuText: '#333333',
    menuTextDanger: '#c72e2e',
    menuHover: '#e8e8e8',
    toolbarBg: '#ffffff',
    toolbarBorder: '#dddddd',
    bg: '#fffffe',
    fg: '#333333',
    searchBg: '#ffffff',
    searchBorder: '#cccccc',
  };
}

/** Detect if VSCode is in dark mode */
export function detectDarkMode(): boolean {
  return document.body.classList.contains('vscode-dark') ||
    document.body.classList.contains('vscode-high-contrast') ||
    document.body.getAttribute('data-vscode-theme-kind') === 'vscode-dark' ||
    document.body.getAttribute('data-vscode-theme-kind') === 'vscode-high-contrast';
}
