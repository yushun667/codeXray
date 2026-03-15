import type { ThemeColors, GraphNode } from '../types';
import type { GraphRenderer } from './renderer';

/**
 * 右键菜单回调接口
 */
export interface ContextMenuCallbacks {
  /** 查询调用节点（向左扩展） */
  onQueryPredecessors: (symbol: string, file: string) => void;
  /** 查询被调用节点（向右扩展） */
  onQuerySuccessors: (symbol: string, file: string) => void;
  /** 删除单个节点（异步操作） */
  onDeleteNode: (id: string) => void | Promise<void>;
  /** 折叠子树（异步操作） */
  onCollapseSubtree: (id: string) => void | Promise<void>;
  /** 展开子树（异步操作） */
  onExpandSubtree: (id: string) => void | Promise<void>;
  /** 选择子树（异步操作） */
  onSelectSubtree: (id: string) => void | Promise<void>;
}

/**
 * 纯 DOM 实现的右键上下文菜单。
 * 支持查询扩展、折叠/展开、选择子树、删除节点等操作。
 */
export class NodeContextMenu {
  private _el: HTMLElement;
  private _renderer: GraphRenderer;
  private _callbacks: ContextMenuCallbacks;
  private _colors: ThemeColors;
  private _currentNodeId: string | null = null;
  private _boundHideOnClick: ((e: Event) => void) | null = null;
  private _cleanups: (() => void)[] = [];

  /**
   * 构造函数
   * @param container 菜单挂载的容器元素
   * @param renderer 图渲染器实例
   * @param callbacks 菜单操作回调
   * @param colors 主题颜色
   */
  constructor(
    container: HTMLElement,
    renderer: GraphRenderer,
    callbacks: ContextMenuCallbacks,
    colors: ThemeColors,
  ) {
    this._renderer = renderer;
    this._callbacks = callbacks;
    this._colors = colors;

    this._el = document.createElement('div');
    this._el.className = 'cx-context-menu';
    this._el.style.cssText = `
      position: fixed;
      z-index: 1000;
      display: none;
      min-width: 180px;
      background: ${colors.menuBg};
      border: 1px solid ${colors.menuBorder};
      border-radius: 6px;
      padding: 4px 0;
      box-shadow: 0 4px 12px rgba(0,0,0,0.3);
      font-size: 13px;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      color: ${colors.menuText};
    `;
    container.appendChild(this._el);

    // 点击其他区域隐藏菜单
    this._boundHideOnClick = (ev: Event) => {
      if (this._el.contains(ev.target as Node)) return;
      this.hide();
    };
    document.addEventListener('mousedown', this._boundHideOnClick);
  }

  /**
   * 显示菜单
   * @param x 屏幕 X 坐标
   * @param y 屏幕 Y 坐标
   * @param nodeId 右键点击的节点 ID
   */
  show(x: number, y: number, nodeId: string): void {
    this._currentNodeId = nodeId;
    const raw = this._renderer.getNodeRaw(nodeId);
    if (!raw) return;

    this._el.innerHTML = '';
    const items = this._buildMenuItems(raw, nodeId);
    for (const item of items) {
      this._el.appendChild(item);
    }

    this._el.style.display = 'block';

    // 防止溢出屏幕
    const rect = this._el.getBoundingClientRect();
    const vw = window.innerWidth;
    const vh = window.innerHeight;
    if (x + rect.width > vw) x = vw - rect.width - 8;
    if (y + rect.height > vh) y = vh - rect.height - 8;
    if (x < 0) x = 8;
    if (y < 0) y = 8;

    this._el.style.left = `${x}px`;
    this._el.style.top = `${y}px`;
  }

  /** 隐藏菜单 */
  hide(): void {
    this._el.style.display = 'none';
    this._currentNodeId = null;
  }

  /** 菜单是否可见 */
  isVisible(): boolean {
    return this._el.style.display !== 'none';
  }

  /** 销毁菜单，移除事件监听 */
  destroy(): void {
    if (this._boundHideOnClick) {
      document.removeEventListener('mousedown', this._boundHideOnClick);
    }
    for (const fn of this._cleanups) fn();
    this._cleanups = [];
    this._el.remove();
  }

  /**
   * 绑定 G6 图事件以触发菜单。
   * 监听 node:contextmenu 和 canvas:contextmenu 事件。
   */
  bind(): void {
    const graph = this._renderer.getGraph();
    if (!graph) return;

    const onNodeCtx = (e: any) => {
      // 提取节点 ID
      const target = e.target as Record<string, unknown> | undefined;
      const id = target?.id ? String(target.id) : (e.targetId ? String(e.targetId) : undefined);
      if (!id) return;

      // 阻止浏览器默认右键菜单
      const origEvt = (e.originalEvent ?? e.nativeEvent) as Event | undefined;
      if (origEvt?.preventDefault) origEvt.preventDefault();

      // 从原始鼠标事件获取屏幕坐标
      let cx = 0, cy = 0;
      if (origEvt && 'clientX' in origEvt) {
        cx = (origEvt as MouseEvent).clientX;
        cy = (origEvt as MouseEvent).clientY;
      } else {
        // 降级：从 G6 事件的 client 属性获取
        const client = e.client as Record<string, number> | undefined;
        cx = client?.x ?? (e.clientX as number) ?? 0;
        cy = client?.y ?? (e.clientY as number) ?? 0;
      }

      this.show(cx, cy, id);
    };
    graph.on('node:contextmenu', onNodeCtx);
    this._cleanups.push(() => graph.off('node:contextmenu', onNodeCtx));

    const onCanvasCtx = (e: any) => {
      const origEvt = (e.originalEvent ?? e.nativeEvent) as Event | undefined;
      if (origEvt?.preventDefault) origEvt.preventDefault();
      this.hide();
    };
    graph.on('canvas:contextmenu', onCanvasCtx);
    this._cleanups.push(() => graph.off('canvas:contextmenu', onCanvasCtx));
  }

  /**
   * 构建菜单项列表。
   * 根据节点状态动态显示折叠/展开选项。
   * @param raw 节点原始数据
   * @param nodeId 节点 ID
   */
  private _buildMenuItems(raw: GraphNode, nodeId: string): HTMLElement[] {
    const items: HTMLElement[] = [];
    const c = this._colors;

    // 查询调用节点
    items.push(this._createItem('← 查询调用节点', c.menuText, () => {
      this._callbacks.onQueryPredecessors(raw.name, raw.definition?.file ?? raw.file ?? '');
      this.hide();
    }));

    // 查询被调用节点
    items.push(this._createItem('→ 查询被调用节点', c.menuText, () => {
      this._callbacks.onQuerySuccessors(raw.name, raw.definition?.file ?? raw.file ?? '');
      this.hide();
    }));

    items.push(this._createSeparator());

    // 折叠/展开子树（根据当前状态显示）
    if (this._renderer.hasCallees(nodeId)) {
      if (this._renderer.isCollapsed(nodeId)) {
        items.push(this._createItem('▶ 展开子树', c.menuText, async () => {
          await this._renderer.expandNode(nodeId);
          this.hide();
        }));
      } else {
        items.push(this._createItem('▼ 折叠子树', c.menuText, async () => {
          await this._renderer.collapseNode(nodeId);
          this.hide();
        }));
      }
    }

    // 选择子树
    items.push(this._createItem('选择子树', c.menuText, () => {
      this._callbacks.onSelectSubtree(nodeId);
      this.hide();
    }));

    items.push(this._createSeparator());

    // 删除选中节点（多个时显示数量）
    const selectedIds = this._renderer.getSelectedNodeIds();
    if (selectedIds.length > 1) {
      const rootId = this._renderer.getRootId();
      const deletableCount = selectedIds.filter((id) => id !== rootId).length;
      if (deletableCount > 0) {
        items.push(this._createItem(`✕ 删除选中节点 (${deletableCount})`, c.menuTextDanger, async () => {
          const toRemove = selectedIds.filter((id) => id !== rootId);
          await this._renderer.removeNodes(toRemove);
          this.hide();
        }));
      }
    }

    // 删除当前节点
    if (nodeId !== this._renderer.getRootId()) {
      items.push(this._createItem('✕ 删除节点', c.menuTextDanger, async () => {
        await this._callbacks.onDeleteNode(nodeId);
        this.hide();
      }));
    }

    return items;
  }

  /**
   * 创建菜单项 DOM 元素
   * @param label 菜单文字
   * @param color 文字颜色
   * @param onClick 点击回调
   */
  private _createItem(label: string, color: string, onClick: () => void): HTMLElement {
    const el = document.createElement('div');
    el.textContent = label;
    el.style.cssText = `
      padding: 6px 16px;
      cursor: pointer;
      color: ${color};
      transition: background 0.1s;
      white-space: nowrap;
    `;
    el.addEventListener('mouseenter', () => {
      el.style.background = this._colors.menuHover;
    });
    el.addEventListener('mouseleave', () => {
      el.style.background = 'transparent';
    });
    el.addEventListener('click', (e) => {
      e.stopPropagation();
      onClick();
    });
    return el;
  }

  /** 创建分隔线 DOM 元素 */
  private _createSeparator(): HTMLElement {
    const el = document.createElement('div');
    el.style.cssText = `
      height: 1px;
      margin: 4px 8px;
      background: ${this._colors.menuBorder};
    `;
    return el;
  }
}
