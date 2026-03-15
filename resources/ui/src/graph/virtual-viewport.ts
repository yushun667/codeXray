import type { Graph } from '@antv/g6';

/** 可视区域外的缓冲边距（像素） */
const VIEWPORT_MARGIN = 200;
/** 视口变换去抖延迟（毫秒） */
const DEBOUNCE_MS = 200;
/** 启用视口裁剪的最小节点数 */
const ENABLE_THRESHOLD = 2000;

/**
 * 大图视口裁剪插件（>2000 节点时启用）。
 * 监听视口变换事件，隐藏可视区域外的节点以提高渲染性能。
 * hideElement/showElement 是异步方法（内部含 draw），必须 await。
 */
export class VirtualViewport {
  private _graph: Graph | null = null;
  private _enabled = false;
  private _timer: ReturnType<typeof setTimeout> | null = null;
  private _hiddenNodes: Set<string> = new Set();
  /** 防止并发 _cull 执行的锁 */
  private _culling = false;

  /**
   * 挂载到图实例并启动视口裁剪。
   * @param graph G6 Graph 实例
   */
  attach(graph: Graph): void {
    this._graph = graph;

    const nodeCount = graph.getNodeData().length;
    this._enabled = nodeCount >= ENABLE_THRESHOLD;

    if (!this._enabled) return;

    // 监听视口变换事件（pan/zoom）
    graph.on('afterTransform', () => this._debouncedCull());

    // 初始裁剪
    this._cull();
  }

  /**
   * 去抖裁剪：避免频繁视口变换导致过度裁剪计算。
   */
  private _debouncedCull(): void {
    if (this._timer) clearTimeout(this._timer);
    this._timer = setTimeout(() => this._cull(), DEBOUNCE_MS);
  }

  /**
   * 执行视口裁剪：计算可视区域，隐藏区域外的节点，显示区域内的节点。
   * hideElement/showElement 是异步方法，需要 await。
   * 使用 _culling 标志防止并发执行。
   */
  private async _cull(): Promise<void> {
    if (!this._graph || !this._enabled || this._culling) return;

    this._culling = true;
    const graph = this._graph;

    try {
      // 获取当前视口在画布坐标系中的可视区域
      const [canvasW, canvasH] = [
        graph.getCanvas().getConfig().width ?? 800,
        graph.getCanvas().getConfig().height ?? 600,
      ];

      // 通过视口到画布的坐标转换计算可见区域
      const topLeft = graph.getCanvasByViewport([0, 0]);
      const bottomRight = graph.getCanvasByViewport([canvasW, canvasH]);

      const minX = (topLeft?.[0] ?? 0) - VIEWPORT_MARGIN;
      const minY = (topLeft?.[1] ?? 0) - VIEWPORT_MARGIN;
      const maxX = (bottomRight?.[0] ?? canvasW) + VIEWPORT_MARGIN;
      const maxY = (bottomRight?.[1] ?? canvasH) + VIEWPORT_MARGIN;

      const nodeData = graph.getNodeData();
      const toShow: string[] = [];
      const toHide: string[] = [];

      for (const nd of nodeData) {
        const id = String(nd.id);
        const pos = graph.getElementPosition(id);
        const x = Array.isArray(pos) ? pos[0] : 0;
        const y = Array.isArray(pos) ? pos[1] : 0;

        const isVisible = x >= minX && x <= maxX && y >= minY && y <= maxY;

        if (isVisible && this._hiddenNodes.has(id)) {
          toShow.push(id);
          this._hiddenNodes.delete(id);
        } else if (!isVisible && !this._hiddenNodes.has(id)) {
          toHide.push(id);
          this._hiddenNodes.add(id);
        }
      }

      // 应用可见性变更（异步方法，内部含 draw）
      if (toHide.length > 0) {
        await graph.hideElement(toHide);
      }
      if (toShow.length > 0) {
        await graph.showElement(toShow);
      }
    } catch (err) {
      console.warn('[VirtualViewport] cull error:', err);
    } finally {
      this._culling = false;
    }
  }

  /**
   * 卸载视口裁剪：显示所有隐藏的节点并清理状态。
   * showElement 是异步方法，但 detach 通常在销毁前调用，不阻塞。
   */
  detach(): void {
    if (this._timer) clearTimeout(this._timer);

    // 显示所有隐藏的节点（fire-and-forget，因为图即将销毁）
    if (this._graph && this._hiddenNodes.size > 0) {
      try {
        this._graph.showElement(Array.from(this._hiddenNodes));
      } catch { /* ignore */ }
    }

    this._hiddenNodes.clear();
    this._graph = null;
    this._enabled = false;
    this._culling = false;
  }
}
