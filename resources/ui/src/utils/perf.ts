import type { PerfReport } from '../types';

/** Performance measurement utility */
export class PerfTimer {
  private _marks: Map<string, number> = new Map();
  private _durations: Map<string, number> = new Map();

  mark(name: string): void {
    this._marks.set(name, performance.now());
  }

  measure(name: string): number {
    const start = this._marks.get(name);
    if (start === undefined) return 0;
    const dur = performance.now() - start;
    this._durations.set(name, dur);
    return dur;
  }

  get(name: string): number {
    return Math.round(this._durations.get(name) ?? 0);
  }

  buildReport(trigger: string, nodeCount: number, edgeCount: number): PerfReport {
    return {
      trigger,
      nodeCount,
      edgeCount,
      phases: {
        computeLayout: this.get('computeLayout'),
        resolveCollisions: this.get('resolveCollisions'),
        layoutWorkerTime: this.get('layoutWorkerTime'),
        buildG6Data: this.get('buildG6Data'),
        setData: this.get('setData'),
        render: this.get('render'),
        fitView: this.get('fitView'),
        total: this.get('total'),
      },
    };
  }
}
