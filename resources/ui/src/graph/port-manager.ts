import type { PortKey, PortSelection } from '../types';

/**
 * Determine source/target ports based on the angle between two nodes.
 * Four-directional port selection using angle-based algorithm.
 */
export function selectPorts(
  sx: number, sy: number,
  tx: number, ty: number,
): PortSelection {
  const dx = tx - sx;
  const dy = ty - sy;

  // Special case: nearly same column → use top/bottom
  if (Math.abs(dx) < 10) {
    return dy > 0
      ? { sourcePort: 'bottom', targetPort: 'top' }
      : { sourcePort: 'top', targetPort: 'bottom' };
  }

  const angle = Math.atan2(dy, dx) * (180 / Math.PI);

  if (angle >= -45 && angle < 45) {
    return { sourcePort: 'right', targetPort: 'left' };
  } else if (angle >= 45 && angle < 135) {
    return { sourcePort: 'bottom', targetPort: 'top' };
  } else if (angle >= -135 && angle < -45) {
    return { sourcePort: 'top', targetPort: 'bottom' };
  } else {
    return { sourcePort: 'left', targetPort: 'right' };
  }
}

/** Port position offsets relative to node center (0-1 ratio) */
export const PORT_POSITIONS: Record<PortKey, [number, number]> = {
  top: [0.5, 0],
  bottom: [0.5, 1],
  left: [0, 0.5],
  right: [1, 0.5],
};

/**
 * Build G6 port definitions for a node.
 * All 4 ports defined but invisible by default (r: 0).
 */
export function buildPortStyles(isHovered: boolean) {
  const r = isHovered ? 3 : 0;
  const fill = '#4fc3f7';
  return {
    ports: [
      { key: 'top', placement: [0.5, 0] as [number, number], r, fill },
      { key: 'bottom', placement: [0.5, 1] as [number, number], r, fill },
      { key: 'left', placement: [0, 0.5] as [number, number], r, fill },
      { key: 'right', placement: [1, 0.5] as [number, number], r, fill },
    ],
  };
}
