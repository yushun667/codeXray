import React from 'react';
import { createRoot } from 'react-dom/client';
import { GraphPage } from './GraphPage';

const root = document.getElementById('root');
if (root) {
  createRoot(root).render(
    <React.StrictMode>
      <GraphPage />
    </React.StrictMode>
  );
}
