import { computeBidirectionalLayout, resolveCollisions } from './layout';
import type { LayoutWorkerInput, LayoutWorkerResult } from '../types';

// Web Worker entry point for layout computation
self.onmessage = ({ data }: MessageEvent<LayoutWorkerInput>) => {
  const { nodes, edges, rootId, config } = data;
  const layoutNodes = computeBidirectionalLayout(nodes, edges, rootId, config);
  resolveCollisions(layoutNodes, config?.maxCollisionIter ?? 30);
  const result: LayoutWorkerResult = { type: 'result', layoutNodes };
  self.postMessage(result);
};
