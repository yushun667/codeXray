import React, { useEffect, useRef, useState } from 'react';
import type { Node, Edge } from 'reactflow';
import type { GraphData, GraphType } from '../shared/types';
import { graphDataToReactFlow } from './dataAdapter';
import { getLayoutedElements } from './graphLayout';
import { mergeGraph } from './graphMerge';
import { GraphCore } from './GraphCore';

declare const acquireVsCodeApi: (() => { postMessage: (msg: unknown) => void }) | undefined;
const vscode = typeof acquireVsCodeApi !== 'undefined' ? acquireVsCodeApi() : null;

const GRAPH_TYPE_TITLE: Record<GraphType, string> = {
  call_graph: '调用链',
  class_graph: '类关系图',
  data_flow: '数据流',
  control_flow: '控制流',
};

export function GraphApp() {
  const [graphType, setGraphType] = useState<GraphType | null>(null);
  const [nodes, setNodes] = useState<Node[]>([]);
  const [edges, setEdges] = useState<Edge[]>([]);
  const nodesRef = useRef<Node[]>([]);
  const edgesRef = useRef<Edge[]>([]);
  const graphTypeRef = useRef<GraphType | null>(null);
  nodesRef.current = nodes;
  edgesRef.current = edges;
  graphTypeRef.current = graphType;

  useEffect(() => {
    if (vscode) vscode.postMessage({ action: 'graphReady' });
  }, []);

  useEffect(() => {
    const handler = (event: MessageEvent<{
      type?: string;
      graphType?: GraphType;
      data?: GraphData;
      nodes?: unknown[];
      edges?: unknown[];
    }>) => {
      const m = event.data;
      if (!m || typeof m !== 'object') return;
      if (m.type === 'initGraph') {
        setGraphType(m.graphType ?? null);
        if (m.data?.nodes?.length || m.data?.edges?.length) {
          const { nodes: n, edges: e } = graphDataToReactFlow(m.data, m.graphType);
          const layouted = getLayoutedElements(n, e);
          setNodes(layouted);
          setEdges(e);
        } else {
          setNodes([]);
          setEdges([]);
        }
      }
      if (m.type === 'graphAppend') {
        const appendData: GraphData = {
          nodes: (m.nodes ?? []) as GraphData['nodes'],
          edges: (m.edges ?? []) as GraphData['edges'],
        };
        const { nodes: appendNodes, edges: appendEdges } = graphDataToReactFlow(appendData, graphTypeRef.current ?? undefined);
        const currentNodes = nodesRef.current;
        const currentEdges = edgesRef.current;
        const { nodes: mergedNodes, edges: mergedEdges } = mergeGraph(
          currentNodes,
          currentEdges,
          appendNodes,
          appendEdges
        );
        const layouted = getLayoutedElements(mergedNodes, mergedEdges);
        setNodes(layouted);
        setEdges(mergedEdges);
      }
    };
    window.addEventListener('message', handler);
    return () => window.removeEventListener('message', handler);
  }, []);

  return (
    <div className="graph-app">
      <h2 className="graph-title">{graphType ? GRAPH_TYPE_TITLE[graphType] : '图'}</h2>
      <GraphCore initialNodes={nodes} initialEdges={edges} graphType={graphType} />
    </div>
  );
}
