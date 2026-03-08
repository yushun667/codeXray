import React from 'react';
import { createRoot } from 'react-dom/client';
import { SidebarContainer } from './SidebarContainer';
import './sidebar.css';

const root = document.getElementById('root');
if (root) {
  createRoot(root).render(
    <React.StrictMode>
      <SidebarContainer />
    </React.StrictMode>
  );
}
