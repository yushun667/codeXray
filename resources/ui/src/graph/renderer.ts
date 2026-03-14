import { Graph } from '@antv/g6';
import type { GraphNode, GraphEdge, LayoutNode, QueryType, ThemeColors } from '../types';
import { getThemeColors, detectDarkMode } from '../utils/theme';
import { PerfTimer } from '../utils/perf';
import { LayoutWorkerBridge } from './layout-worker-bridge';
import { selectPorts } from './port-manager';
import { calcNodeWidth, buildDisplayLabel } from './layout';

// ── 类型 ──

/**
 * GraphRenderer 初始化选项
 * @param container 图容器 DOM 元素
 * @param onGotoSymbol 跳转到符号定义的回调
 * @param onPerfReport 性能报告回调
 */
export interface GraphRendererOptions {
  container: HTMLElement;
  onGotoSymbol?: (file: string, line: number, column: number) => void;
  onQueryPredecessors?: (symbol: string, file: string, queryDepth?: number) => void;
  onQuerySuccessors?: (symbol: string, file: string, queryDepth?: number) => void;
  onPerfReport?: (report: unknown) => void;
}

/**
 * 内部节点包装，存储原始数据和布局位置
 */
interface InternalNode {
  raw: GraphNode;
  layoutNode?: LayoutNode;
}

// ── 常量 ──
/** 大图阈值：超过此元素数量时启用性能优化 */
const LARGE_GRAPH_THRESHOLD = 500;

/**
 * GraphRenderer：G6 图的核心管理类。
 * 负责图的创建、布局计算、节点/边样式映射、交互行为配置，
 * 以及折叠/展开、undo/redo 等高级功能。
 */
export class GraphRenderer {
  private _graph: Graph | null = null;
  private _container: HTMLElement;
  private _options: GraphRendererOptions;
  private _workerBridge: LayoutWorkerBridge;
  private _colors: ThemeColors;
  private _isDark: boolean;
  private _rootId: string = '';
  private _graphType: QueryType = 'call_graph';
  private _queryDepth?: number;
  private _nodeMap: Map<string, InternalNode> = new Map();
  private _edgeSet: Set<string> = new Set(); // "caller->callee" 去重
  private _nodes: GraphNode[] = [];
  private _edges: GraphEdge[] = [];
  /** 被折叠隐藏的节点 ID 集合 */
  private _collapsedChildren: Map<string, Set<string>> = new Map();
  /** 已折叠的节点 ID 集合 */
  private _collapsedNodes: Set<string> = new Set();

  /**
   * 构造函数
   * @param options 渲染器选项
   */
  constructor(options: GraphRendererOptions) {
    this._options = options;
    this._container = options.container;
    this._isDark = detectDarkMode();
    this._colors = getThemeColors(this._isDark);
    this._workerBridge = new LayoutWorkerBridge();
  }

  // ── 公共 API ──

  /**
   * 初始化图：计算布局、创建 G6 实例、渲染。
   * @param nodes 节点数组
   * @param edges 边数组
   * @param rootId 根节点 ID
   * @param graphType 图类型
   * @param queryDepth 查询深度
   */
  async init(
    nodes: GraphNode[],
    edges: GraphEdge[],
    rootId: string,
    graphType: QueryType,
    queryDepth?: number,
  ): Promise<void> {
    const perf = new PerfTimer();
    perf.mark('total');

    this._rootId = rootId;
    this._graphType = graphType;
    this._queryDepth = queryDepth;
    this._nodeMap.clear();
    this._edgeSet.clear();
    this._nodes = nodes;
    this._edges = edges;
    this._collapsedChildren.clear();
    this._collapsedNodes.clear();

    for (const n of nodes) {
      this._nodeMap.set(n.id, { raw: n });
    }
    for (const e of edges) {
      const key = `${e.caller || e.source}->${e.callee || e.target}`;
      this._edgeSet.add(key);
    }

    // 通过 Worker 计算布局
    perf.mark('layoutWorkerTime');
    const layoutNodes = await this._workerBridge.compute(nodes, edges, rootId);
    perf.measure('layoutWorkerTime');

    // 存储布局位置
    for (const ln of layoutNodes) {
      const internal = this._nodeMap.get(ln.id);
      if (internal) internal.layoutNode = ln;
    }

    // 构建 G6 数据
    perf.mark('buildG6Data');
    const g6Nodes = this._buildG6Nodes(layoutNodes);
    const g6Edges = this._buildG6Edges(edges, layoutNodes);
    perf.measure('buildG6Data');

    // 销毁之前的图实例
    if (this._graph) {
      this._graph.destroy();
      this._graph = null;
    }

    const isLarge = nodes.length + edges.length > LARGE_GRAPH_THRESHOLD;

    // 创建 G6 图实例
    this._graph = new Graph({
      container: this._container,
      width: this._container.clientWidth,
      height: this._container.clientHeight,
      autoResize: true,
      theme: this._isDark ? 'dark' : 'light',
      data: { nodes: g6Nodes, edges: g6Edges } as any,
      node: {
        type: 'rect',
        style: (d: any) => this._nodeStyleMapper(d),
        state: {
          selected: { lineWidth: 2, stroke: this._colors.selectedStroke },
          highlight: { lineWidth: 2, stroke: this._colors.highlightStroke },
          dimmed: { opacity: this._colors.dimmedOpacity },
          active: {
            lineWidth: 2,
            stroke: this._colors.highlightStroke,
            ports: [
              { key: 'top', placement: [0.5, 0] as [number, number], r: 3, fill: '#4fc3f7', lineWidth: 1, stroke: '#4fc3f7' },
              { key: 'bottom', placement: [0.5, 1] as [number, number], r: 3, fill: '#4fc3f7', lineWidth: 1, stroke: '#4fc3f7' },
              { key: 'left', placement: [0, 0.5] as [number, number], r: 3, fill: '#4fc3f7', lineWidth: 1, stroke: '#4fc3f7' },
              { key: 'right', placement: [1, 0.5] as [number, number], r: 3, fill: '#4fc3f7', lineWidth: 1, stroke: '#4fc3f7' },
            ],
          },
          pathGlow: {
            lineWidth: 3,
            stroke: this._colors.highlightStroke,
            shadowColor: this._colors.highlightStroke,
            shadowBlur: 12,
          },
        },
      },
      edge: {
        type: isLarge ? 'line' : 'cubic-horizontal',
        style: (d: any) => this._edgeStyleMapper(d),
        state: {
          highlight: { stroke: this._colors.highlightStroke, lineWidth: 2 },
          dimmed: { opacity: this._colors.dimmedOpacity },
          pathGlow: {
            stroke: this._colors.highlightStroke,
            lineWidth: 3,
            lineDash: [10, 5],
            shadowColor: this._colors.highlightStroke,
            shadowBlur: 12,
          },
        },
      },
      behaviors: this._buildBehaviors(isLarge) as any,
      plugins: [{ type: 'history', key: 'history', stackSize: 50 }],
      animation: false,
    });

    perf.mark('render');
    await this._graph.render();
    perf.measure('render');

    perf.mark('fitView');
    await this._graph.fitView({ padding: 40 } as any);
    perf.measure('fitView');

    perf.measure('total');

    if (this._options.onPerfReport) {
      this._options.onPerfReport(perf.buildReport('init', nodes.length, edges.length));
    }
  }

  /**
   * 增量追加节点和边（去重后添加到图中）。
   * @param newNodes 新增节点
   * @param newEdges 新增边
   */
  async append(newNodes: GraphNode[], newEdges: GraphEdge[]): Promise<void> {
    if (!this._graph) return;

    const perf = new PerfTimer();
    perf.mark('total');

    // 去重节点
    const addedNodes: GraphNode[] = [];
    for (const n of newNodes) {
      if (!this._nodeMap.has(n.id)) {
        this._nodeMap.set(n.id, { raw: n });
        this._nodes.push(n);
        addedNodes.push(n);
      }
    }

    // 去重边
    const addedEdges: GraphEdge[] = [];
    for (const e of newEdges) {
      const key = `${e.caller || e.source}->${e.callee || e.target}`;
      if (!this._edgeSet.has(key)) {
        this._edgeSet.add(key);
        this._edges.push(e);
        addedEdges.push(e);
      }
    }

    if (addedNodes.length === 0 && addedEdges.length === 0) return;

    // 重新计算全量布局
    perf.mark('layoutWorkerTime');
    const layoutNodes = await this._workerBridge.compute(this._nodes, this._edges, this._rootId);
    perf.measure('layoutWorkerTime');

    for (const ln of layoutNodes) {
      const internal = this._nodeMap.get(ln.id);
      if (internal) internal.layoutNode = ln;
    }

    perf.mark('buildG6Data');
    const g6Nodes = this._buildG6Nodes(layoutNodes);
    const g6Edges = this._buildG6Edges(this._edges, layoutNodes);
    perf.measure('buildG6Data');

    perf.mark('setData');
    this._graph.setData({ nodes: g6Nodes, edges: g6Edges } as any);
    perf.measure('setData');

    perf.mark('render');
    await this._graph.draw();
    perf.measure('render');

    perf.mark('fitView');
    await this._graph.fitView({ padding: 40 } as any);
    perf.measure('fitView');

    perf.measure('total');

    if (this._options.onPerfReport) {
      this._options.onPerfReport(perf.buildReport('append', this._nodes.length, this._edges.length));
    }
  }

  /**
   * 删除指定节点及其关联边，并重新布局。
   * 使用 G6 的 removeNodeData/removeEdgeData 以支持 history undo/redo。
   * @param ids 要删除的节点 ID 数组
   */
  async removeNodes(ids: string[]): Promise<void> {
    if (!this._graph || ids.length === 0) return;

    // 删除根节点时清空画布
    if (ids.includes(this._rootId)) {
      this._graph.setData({ nodes: [], edges: [] } as any);
      await this._graph.draw();
      this._nodeMap.clear();
      this._edgeSet.clear();
      this._nodes = [];
      this._edges = [];
      return;
    }

    const removeSet = new Set(ids);

    // 收集需要删除的边 ID
    const edgeIdsToRemove: string[] = [];
    const edgeData = this._graph.getEdgeData();
    for (const ed of edgeData) {
      const srcId = String(ed.source);
      const tgtId = String(ed.target);
      if (removeSet.has(srcId) || removeSet.has(tgtId)) {
        edgeIdsToRemove.push(String(ed.id));
      }
    }

    // 使用 G6 removeEdgeData/removeNodeData（被 history 插件跟踪）
    try {
      if (edgeIdsToRemove.length > 0) {
        this._graph.removeEdgeData(edgeIdsToRemove);
      }
      this._graph.removeNodeData(ids);
      await this._graph.draw();
    } catch (err) {
      console.warn('[renderer] removeNodes G6 API failed, fallback to setData:', err);
      // 降级：使用 setData 重建
      this._rebuildAfterRemove(removeSet);
      return;
    }

    // 同步内部状态
    this._nodes = this._nodes.filter((n) => !removeSet.has(n.id));
    for (const id of ids) this._nodeMap.delete(id);
    this._edges = this._edges.filter((e) => {
      const src = e.caller || e.source || '';
      const tgt = e.callee || e.target || '';
      const keep = !removeSet.has(src) && !removeSet.has(tgt);
      if (!keep) this._edgeSet.delete(`${src}->${tgt}`);
      return keep;
    });
  }

  /**
   * 折叠节点：隐藏该节点的所有后代子树。
   * 使用 G6 的 hideElement 实现可逆隐藏（异步方法，内部含 draw）。
   * @param nodeId 要折叠的节点 ID
   */
  async collapseNode(nodeId: string): Promise<void> {
    const graph = this._graph;
    if (!graph) return;

    // BFS 找所有后代（仅沿 caller→callee 方向）
    const descendants = new Set<string>();
    const queue = [nodeId];
    while (queue.length > 0) {
      const current = queue.shift()!;
      for (const e of this._edges) {
        const src = e.caller || e.source || '';
        const tgt = e.callee || e.target || '';
        if (src === current && !descendants.has(tgt) && tgt !== nodeId) {
          descendants.add(tgt);
          queue.push(tgt);
        }
      }
    }

    if (descendants.size === 0) return;

    // 收集需隐藏的边（包括连接到后代节点的所有边，以及折叠节点到后代的直连边）
    const edgesToHide: string[] = [];
    const edgeData = graph.getEdgeData();
    for (const ed of edgeData) {
      const srcId = String(ed.source);
      const tgtId = String(ed.target);
      if (descendants.has(srcId) || descendants.has(tgtId)) {
        edgesToHide.push(String(ed.id));
      }
    }

    // 记录折叠状态（在 hideElement 之前设置，以便 badge 能正确显示）
    this._collapsedChildren.set(nodeId, descendants);
    this._collapsedNodes.add(nodeId);

    // 隐藏节点和边（异步，内部含 draw）
    const allToHide = [...Array.from(descendants), ...edgesToHide];
    try {
      await graph.hideElement(allToHide);
    } catch (err) {
      console.warn('[renderer] collapseNode hideElement failed:', err);
    }

    // 更新折叠节点的 badge（触发重绘以更新图标）
    await this._updateCollapseBadge(nodeId);
  }

  /**
   * 展开节点：显示之前被隐藏的后代子树。
   * 使用 G6 的 showElement 实现（异步方法，内部含 draw）。
   * 注意：如果后代中有仍处于折叠状态的节点，其隐藏的子节点不应被显示。
   * @param nodeId 要展开的节点 ID
   */
  async expandNode(nodeId: string): Promise<void> {
    const graph = this._graph;
    if (!graph) return;

    const hidden = this._collapsedChildren.get(nodeId);
    if (!hidden || hidden.size === 0) return;

    // 清除当前节点的折叠状态（在 showElement 之前设置，以便 badge 能正确显示）
    this._collapsedChildren.delete(nodeId);
    this._collapsedNodes.delete(nodeId);

    // 计算实际应显示的节点：排除仍处于折叠状态的子节点的后代
    // 例如：展开 B 时，如果 A（B 的后代）仍然折叠，A 的后代不应被显示
    const stillHiddenByOthers = new Set<string>();
    for (const [collapsedId, collapsedDescendants] of this._collapsedChildren) {
      if (hidden.has(collapsedId)) {
        // 这个折叠节点是当前展开节点的后代，其后代应保持隐藏
        for (const desc of collapsedDescendants) {
          stillHiddenByOthers.add(desc);
        }
      }
    }

    // 过滤出实际应显示的节点
    const nodesToShow = Array.from(hidden).filter((id) => !stillHiddenByOthers.has(id));

    // 收集需显示的边（仅显示两端都可见的边）
    const showSet = new Set(nodesToShow);
    const edgesToShow: string[] = [];
    const edgeData = graph.getEdgeData();
    for (const ed of edgeData) {
      const srcId = String(ed.source);
      const tgtId = String(ed.target);
      // 边的两端必须都是可见的（不在 stillHiddenByOthers 中）才显示
      const srcVisible = showSet.has(srcId) || (!hidden.has(srcId));
      const tgtVisible = showSet.has(tgtId) || (!hidden.has(tgtId));
      if ((showSet.has(srcId) || showSet.has(tgtId)) && srcVisible && tgtVisible) {
        edgesToShow.push(String(ed.id));
      }
    }

    // 显示节点和边（异步，内部含 draw）
    const allToShow = [...nodesToShow, ...edgesToShow];
    if (allToShow.length > 0) {
      try {
        await graph.showElement(allToShow);
      } catch (err) {
        console.warn('[renderer] expandNode showElement failed:', err);
      }
    }

    // 更新折叠节点的 badge
    await this._updateCollapseBadge(nodeId);
  }

  /**
   * 切换节点的折叠/展开状态。
   * @param nodeId 节点 ID
   */
  async toggleCollapse(nodeId: string): Promise<void> {
    if (this._collapsedNodes.has(nodeId)) {
      await this.expandNode(nodeId);
    } else {
      await this.collapseNode(nodeId);
    }
  }

  /**
   * 判断节点是否已折叠。
   * @param nodeId 节点 ID
   */
  isCollapsed(nodeId: string): boolean {
    return this._collapsedNodes.has(nodeId);
  }

  /**
   * 判断节点是否有后继节点（callees），用于决定是否显示折叠/展开图标。
   * @param nodeId 节点 ID
   */
  hasCallees(nodeId: string): boolean {
    for (const e of this._edges) {
      const src = e.caller || e.source || '';
      if (src === nodeId) return true;
    }
    return false;
  }

  /**
   * 重新布局：重新计算所有节点位置。
   */
  async relayout(): Promise<void> {
    if (!this._graph) return;

    const layoutNodes = await this._workerBridge.compute(this._nodes, this._edges, this._rootId);
    for (const ln of layoutNodes) {
      const internal = this._nodeMap.get(ln.id);
      if (internal) internal.layoutNode = ln;
    }

    const g6Nodes = this._buildG6Nodes(layoutNodes);
    const g6Edges = this._buildG6Edges(this._edges, layoutNodes);

    this._graph.setData({ nodes: g6Nodes, edges: g6Edges } as any);
    await this._graph.draw();
    await this._graph.fitView({ padding: 40 } as any);
  }

  /**
   * 适应画布：缩放使所有元素可见。
   */
  async fitView(): Promise<void> {
    if (!this._graph) return;
    await this._graph.fitView({ padding: 40 } as any);
  }

  /**
   * 获取当前选中的节点 ID 列表。
   * @returns 选中的节点 ID 数组
   */
  getSelectedNodeIds(): string[] {
    if (!this._graph) return [];
    try {
      const selected = this._graph.getElementDataByState('node', 'selected');
      return selected.map((n: Record<string, unknown>) => String(n.id));
    } catch {
      return [];
    }
  }

  /**
   * 撤销上一步操作（通过 G6 history 插件）。
   * history 插件跟踪 addData/removeNodeData/removeEdgeData/updateNodeData 等操作。
   * undo 后需要同步 renderer 内部状态以保持一致。
   */
  undo(): void {
    if (!this._graph) return;
    try {
      const history = this._graph.getPluginInstance('history') as any;
      if (history?.undo) {
        history.undo();
        this._syncInternalStateFromGraph();
      }
    } catch { /* ignore */ }
  }

  /**
   * 重做上一步撤销的操作（通过 G6 history 插件）。
   * redo 后需要同步 renderer 内部状态以保持一致。
   */
  redo(): void {
    if (!this._graph) return;
    try {
      const history = this._graph.getPluginInstance('history') as any;
      if (history?.redo) {
        history.redo();
        this._syncInternalStateFromGraph();
      }
    } catch { /* ignore */ }
  }

  /**
   * 检查是否可以撤销。
   * @returns 是否有可撤销的操作
   */
  canUndo(): boolean {
    if (!this._graph) return false;
    try {
      const history = this._graph.getPluginInstance('history') as any;
      return history?.canUndo?.() ?? false;
    } catch { return false; }
  }

  /**
   * 检查是否可以重做。
   * @returns 是否有可重做的操作
   */
  canRedo(): boolean {
    if (!this._graph) return false;
    try {
      const history = this._graph.getPluginInstance('history') as any;
      return history?.canRedo?.() ?? false;
    } catch { return false; }
  }

  /** 获取 G6 Graph 实例 */
  getGraph(): Graph | null {
    return this._graph;
  }

  /** 获取根节点 ID */
  getRootId(): string {
    return this._rootId;
  }

  /** 获取图类型 */
  getGraphType(): QueryType {
    return this._graphType;
  }

  /** 获取查询深度 */
  getQueryDepth(): number | undefined {
    return this._queryDepth;
  }

  /** 设置查询深度 */
  setQueryDepth(depth: number): void {
    this._queryDepth = depth;
  }

  /** 获取节点的原始数据 */
  getNodeRaw(id: string): GraphNode | undefined {
    return this._nodeMap.get(id)?.raw;
  }

  /** 获取节点总数 */
  getNodeCount(): number {
    return this._nodes.length;
  }

  /** 获取边总数 */
  getEdgeCount(): number {
    return this._edges.length;
  }

  /** 获取所有节点 ID */
  getAllNodeIds(): string[] {
    return this._nodes.map((n) => n.id);
  }

  /** 获取所有节点 */
  getAllNodes(): GraphNode[] {
    return this._nodes;
  }

  /** 获取所有边 */
  getAllEdges(): GraphEdge[] {
    return this._edges;
  }

  /** 获取当前图状态（用于导出） */
  getState(): { nodes: GraphNode[]; edges: GraphEdge[]; rootId: string; graphType: QueryType } {
    return {
      nodes: [...this._nodes],
      edges: [...this._edges],
      rootId: this._rootId,
      graphType: this._graphType,
    };
  }

  /**
   * 重新计算所有边的 sourcePort/targetPort。
   * 在节点被拖拽或重新布局后调用。
   */
  recomputeEdgePorts(): void {
    const graph = this._graph;
    if (!graph) return;

    // 从当前 G6 状态构建位置映射
    const posMap = new Map<string, { x: number; y: number }>();
    const nodeData = graph.getNodeData();
    for (const nd of nodeData) {
      const id = String(nd.id);
      const pos = graph.getElementPosition(id);
      if (Array.isArray(pos)) {
        posMap.set(id, { x: pos[0], y: pos[1] });
      }
    }

    const edgeData = graph.getEdgeData();
    const updates: Array<Record<string, unknown>> = [];

    for (const ed of edgeData) {
      const srcId = String(ed.source);
      const tgtId = String(ed.target);
      const srcPos = posMap.get(srcId);
      const tgtPos = posMap.get(tgtId);
      if (!srcPos || !tgtPos) continue;

      const ports = selectPorts(srcPos.x, srcPos.y, tgtPos.x, tgtPos.y);
      updates.push({
        id: String(ed.id),
        style: { sourcePort: ports.sourcePort, targetPort: ports.targetPort },
      });
    }

    if (updates.length > 0) {
      try {
        graph.updateEdgeData(updates);
      } catch { /* ignore */ }
    }
  }

  /** 销毁图实例并清理资源 */
  destroy(): void {
    this._graph?.destroy();
    this._graph = null;
    this._workerBridge.destroy();
    this._nodeMap.clear();
    this._edgeSet.clear();
    this._nodes = [];
    this._edges = [];
    this._collapsedChildren.clear();
    this._collapsedNodes.clear();
  }

  // ── 私有方法 ──

  /**
   * 从 G6 Graph 的当前数据同步 renderer 内部状态。
   * 在 undo/redo 后调用，确保 _nodes, _edges, _nodeMap, _edgeSet
   * 与 G6 内部模型保持一致。
   */
  private _syncInternalStateFromGraph(): void {
    const graph = this._graph;
    if (!graph) return;

    try {
      const nodeData = graph.getNodeData();
      const edgeData = graph.getEdgeData();

      // 重建 nodeMap：保留已有的 raw 数据，新增的从 G6 data 中提取
      const newNodeMap = new Map<string, InternalNode>();
      const newNodes: GraphNode[] = [];
      for (const nd of nodeData) {
        const id = String(nd.id);
        const existing = this._nodeMap.get(id);
        if (existing) {
          newNodeMap.set(id, existing);
          newNodes.push(existing.raw);
        } else {
          // undo 恢复的节点：从 G6 data 中尝试提取 raw
          const rawFromData = (nd.data as any)?.raw as GraphNode | undefined;
          if (rawFromData) {
            newNodeMap.set(id, { raw: rawFromData });
            newNodes.push(rawFromData);
          }
        }
      }

      // 构建已有边的 Map 以实现 O(1) 查找（避免 O(E²)）
      const existingEdgeMap = new Map<string, GraphEdge>();
      for (const e of this._edges) {
        const src = e.caller || e.source || '';
        const tgt = e.callee || e.target || '';
        existingEdgeMap.set(`${src}->${tgt}`, e);
      }

      // 重建 edgeSet
      const newEdgeSet = new Set<string>();
      const newEdges: GraphEdge[] = [];
      for (const ed of edgeData) {
        const src = String(ed.source);
        const tgt = String(ed.target);
        const key = `${src}->${tgt}`;
        newEdgeSet.add(key);
        // 从 Map 中 O(1) 查找已有边或从 G6 data 中提取 raw
        const existingEdge = existingEdgeMap.get(key);
        if (existingEdge) {
          newEdges.push(existingEdge);
        } else {
          const rawFromData = (ed.data as any)?.raw as GraphEdge | undefined;
          if (rawFromData) {
            newEdges.push(rawFromData);
          }
        }
      }

      this._nodeMap = newNodeMap;
      this._nodes = newNodes;
      this._edgeSet = newEdgeSet;
      this._edges = newEdges;
    } catch { /* ignore */ }
  }

  /**
   * removeNodes 的降级方案：当 G6 API 失败时使用 setData 重建。
   * @param removeSet 要删除的节点 ID 集合
   */
  private async _rebuildAfterRemove(removeSet: Set<string>): Promise<void> {
    this._nodes = this._nodes.filter((n) => !removeSet.has(n.id));
    for (const id of removeSet) this._nodeMap.delete(id);
    this._edges = this._edges.filter((e) => {
      const src = e.caller || e.source || '';
      const tgt = e.callee || e.target || '';
      const keep = !removeSet.has(src) && !removeSet.has(tgt);
      if (!keep) this._edgeSet.delete(`${src}->${tgt}`);
      return keep;
    });

    const layoutNodes = await this._workerBridge.compute(this._nodes, this._edges, this._rootId);
    for (const ln of layoutNodes) {
      const internal = this._nodeMap.get(ln.id);
      if (internal) internal.layoutNode = ln;
    }

    const g6Nodes = this._buildG6Nodes(layoutNodes);
    const g6Edges = this._buildG6Edges(this._edges, layoutNodes);

    this._graph!.setData({ nodes: g6Nodes, edges: g6Edges } as any);
    await this._graph!.draw();
    await this._graph!.fitView({ padding: 40 } as any);
  }

  /**
   * 更新折叠 badge：更新节点数据以触发样式映射器重新计算 badge 显示。
   * 异步方法，需要 await draw() 才能使视觉更新生效。
   * @param nodeId 需更新 badge 的节点 ID
   */
  private async _updateCollapseBadge(nodeId: string): Promise<void> {
    const graph = this._graph;
    if (!graph) return;

    const isCollapsed = this._collapsedNodes.has(nodeId);
    try {
      // 更新 data.collapsed 字段，触发 G6 重新调用 _nodeStyleMapper
      graph.updateNodeData([{
        id: nodeId,
        data: { collapsed: isCollapsed },
      }]);
      // draw() 是异步方法，必须 await 才能使样式更新可见
      await graph.draw();
    } catch { /* ignore */ }
  }

  /**
   * 构建 G6 行为配置。
   * @param isLarge 是否为大图模式
   */
  private _buildBehaviors(isLarge: boolean): Array<string | Record<string, unknown>> {
    const behaviors: Array<string | Record<string, unknown>> = [
      // 右键拖拽 = 平移画布（右键在任意位置均可触发，包括节点上方）
      {
        type: 'drag-canvas',
        key: 'drag-canvas',
        enable: (event: Record<string, unknown>) => {
          if (!('button' in event)) return false;
          return event.button === 2;
        },
      },
      // 滚轮缩放
      'zoom-canvas',
      // Shift+左键 = 多选（追加选中）
      {
        type: 'click-select',
        key: 'click-select',
        multiple: true,
        trigger: ['shift'],
      },
      // 左键拖空白 = 框选（仅画布空白处且非右键时触发）
      {
        type: 'brush-select',
        key: 'brush-select',
        enableElements: ['node'],
        immediately: true,
        trigger: [] as string[],
        enable: (event: Record<string, unknown>) => {
          if ('button' in event && event.button === 2) return false;
          return event.targetType === 'canvas';
        },
        style: { fill: '#4fc3f7', fillOpacity: 0.1, stroke: '#4fc3f7', lineWidth: 1 },
      },
      // 左键拖节点 = 移动节点
      {
        type: 'drag-element',
        key: 'drag-element',
        enableElements: ['node'],
      },
      // 悬停高亮相邻节点
      { type: 'hover-activate', degree: 1, direction: 'both' },
    ];

    if (isLarge) {
      behaviors.push({
        type: 'optimize-viewport-transform',
        debounce: 150,
      });
    }

    return behaviors;
  }

  /**
   * 节点样式映射器：根据节点数据动态返回样式。
   * 包含折叠/展开 badge。
   * @param d G6 节点数据
   */
  private _nodeStyleMapper(d: any): Record<string, unknown> {
    const id = String(d.id);
    const internal = this._nodeMap.get(id);
    const raw = internal?.raw;
    const displayLabel = raw ? buildDisplayLabel(raw) : id;
    const isRoot = id === this._rootId;
    const nodeWidth = calcNodeWidth(displayLabel);

    // 构建 badges：有 callees 的节点显示折叠/展开图标
    const badges: Array<Record<string, unknown>> = [];
    if (this.hasCallees(id)) {
      const isCollapsed = this._collapsedNodes.has(id);
      const hiddenCount = this._collapsedChildren.get(id)?.size ?? 0;
      const badgeText = isCollapsed && hiddenCount > 0
        ? `▶ ${hiddenCount}`
        : isCollapsed ? '▶' : '▼';
      badges.push({
        text: badgeText,
        placement: 'right',
        fontSize: 10,
        fill: '#ffffff',
        backgroundFill: isCollapsed ? '#e8a145' : '#3a8ab0',
        backgroundRadius: 4,
        padding: [3, 6],
        cursor: 'pointer',
      });
    }

    return {
      x: (d.style as Record<string, unknown>)?.x ?? 0,
      y: (d.style as Record<string, unknown>)?.y ?? 0,
      size: [nodeWidth, 56],
      radius: 6,
      fill: isRoot ? this._colors.rootFill : this._colors.nodeFill,
      stroke: isRoot ? this._colors.rootStroke : this._colors.nodeStroke,
      lineWidth: isRoot ? 2 : 1,
      labelText: displayLabel,
      labelFill: '#ffffff',
      labelFontSize: 11,
      labelFontFamily: "'Consolas', 'Menlo', monospace",
      labelPlacement: 'center',
      labelMaxWidth: nodeWidth - 16,
      labelWordWrap: true,
      labelMaxLines: 3,
      labelTextOverflow: 'ellipsis',
      cursor: 'pointer',
      ports: [
        { key: 'top', placement: [0.5, 0] as [number, number], r: 0, fill: '#4fc3f7' },
        { key: 'bottom', placement: [0.5, 1] as [number, number], r: 0, fill: '#4fc3f7' },
        { key: 'left', placement: [0, 0.5] as [number, number], r: 0, fill: '#4fc3f7' },
        { key: 'right', placement: [1, 0.5] as [number, number], r: 0, fill: '#4fc3f7' },
      ],
      badges: badges.length > 0 ? badges : undefined,
    };
  }

  /**
   * 边样式映射器：根据边数据动态返回样式。
   * 支持从 data._dashOffset 读取流动虚线动画偏移量。
   * @param d G6 边数据
   */
  private _edgeStyleMapper(d: any): Record<string, unknown> {
    const sourcePort = (d.style as Record<string, unknown>)?.sourcePort ?? 'right';
    const targetPort = (d.style as Record<string, unknown>)?.targetPort ?? 'left';

    const result: Record<string, unknown> = {
      stroke: this._colors.edgeStroke,
      lineWidth: 1,
      endArrow: true,
      endArrowSize: 6,
      sourcePort,
      targetPort,
    };

    const dashOffset = (d.data as Record<string, unknown>)?._dashOffset;
    if (typeof dashOffset === 'number') {
      result.lineDashOffset = dashOffset;
    }

    return result;
  }

  /**
   * 构建 G6 节点数据数组。
   * @param layoutNodes 布局计算后的节点数组
   */
  private _buildG6Nodes(layoutNodes: LayoutNode[]): Array<Record<string, unknown>> {
    return layoutNodes.map((ln) => ({
      id: ln.id,
      style: { x: ln.x + ln.width / 2, y: ln.y + ln.height / 2 },
      data: { raw: ln.raw, collapsed: this._collapsedNodes.has(ln.id) },
    }));
  }

  /**
   * 构建 G6 边数据数组，包含端口选择。
   * @param edges 原始边数组
   * @param layoutNodes 布局计算后的节点数组
   */
  private _buildG6Edges(
    edges: GraphEdge[],
    layoutNodes: LayoutNode[],
  ): Array<Record<string, unknown>> {
    // 构建位置查找表
    const posMap = new Map<string, { x: number; y: number; w: number; h: number }>();
    for (const ln of layoutNodes) {
      posMap.set(ln.id, { x: ln.x + ln.width / 2, y: ln.y + ln.height / 2, w: ln.width, h: ln.height });
    }

    return edges
      .map((e, i) => {
        const src = e.caller || e.source || '';
        const tgt = e.callee || e.target || '';
        if (!src || !tgt) return null;

        const srcPos = posMap.get(src);
        const tgtPos = posMap.get(tgt);

        let sourcePort: string = 'right';
        let targetPort: string = 'left';

        if (srcPos && tgtPos) {
          const ports = selectPorts(srcPos.x, srcPos.y, tgtPos.x, tgtPos.y);
          sourcePort = ports.sourcePort;
          targetPort = ports.targetPort;
        }

        return {
          id: e.id || `edge-${src}-${tgt}-${i}`,
          source: src,
          target: tgt,
          style: { sourcePort, targetPort },
          data: { raw: e },
        };
      })
      .filter(Boolean) as Array<Record<string, unknown>>;
  }
}
