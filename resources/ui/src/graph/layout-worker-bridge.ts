import type { GraphNode, GraphEdge, LayoutNode, LayoutConfig, LayoutWorkerInput } from '../types';
import { computeBidirectionalLayout, resolveCollisions } from './layout';

/**
 * Bridge between main thread and layout Web Worker.
 * Falls back to synchronous main-thread computation if Worker creation fails.
 */
export class LayoutWorkerBridge {
  private _worker: Worker | null = null;
  private _pendingResolve: ((nodes: LayoutNode[]) => void) | null = null;
  private _fallback = false;

  constructor() {
    try {
      this._worker = new Worker(
        new URL('./layout-worker.ts', import.meta.url),
        { type: 'module' },
      );
      this._worker.onmessage = (e: MessageEvent) => {
        if (e.data?.type === 'result' && this._pendingResolve) {
          this._pendingResolve(e.data.layoutNodes);
          this._pendingResolve = null;
        }
      };
      this._worker.onerror = () => {
        console.warn('[LayoutWorkerBridge] Worker error, falling back to main thread');
        this._fallback = true;
        this._worker?.terminate();
        this._worker = null;
      };
    } catch {
      console.warn('[LayoutWorkerBridge] Worker creation failed, using main thread');
      this._fallback = true;
    }
  }

  /**
   * Compute layout. Returns a promise that resolves with positioned nodes.
   * Uses Worker if available, otherwise computes synchronously on main thread.
   */
  compute(
    nodes: GraphNode[],
    edges: GraphEdge[],
    rootId: string,
    config?: Partial<LayoutConfig>,
  ): Promise<LayoutNode[]> {
    if (this._fallback || !this._worker) {
      return this._computeSync(nodes, edges, rootId, config);
    }

    return new Promise<LayoutNode[]>((resolve) => {
      // If there's a pending computation, resolve it with empty (it will be superseded)
      if (this._pendingResolve) {
        this._pendingResolve([]);
      }

      this._pendingResolve = resolve;

      const msg: LayoutWorkerInput = { nodes, edges, rootId, config };
      try {
        this._worker!.postMessage(msg);
      } catch {
        this._fallback = true;
        this._pendingResolve = null;
        this._computeSync(nodes, edges, rootId, config).then(resolve);
      }
    });
  }

  private _computeSync(
    nodes: GraphNode[],
    edges: GraphEdge[],
    rootId: string,
    config?: Partial<LayoutConfig>,
  ): Promise<LayoutNode[]> {
    const layoutNodes = computeBidirectionalLayout(nodes, edges, rootId, config);
    resolveCollisions(layoutNodes, config?.maxCollisionIter ?? 30);
    return Promise.resolve(layoutNodes);
  }

  destroy(): void {
    this._worker?.terminate();
    this._worker = null;
    this._pendingResolve = null;
  }
}
