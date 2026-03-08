import React from 'react';
import { createRoot } from 'react-dom/client';
import { GraphApp } from './GraphApp';
import './graph.css';

const root = document.getElementById('root');
if (root) {
  createRoot(root).render(
    <React.StrictMode>
      <GraphApp />
    </React.StrictMode>
  );
}
