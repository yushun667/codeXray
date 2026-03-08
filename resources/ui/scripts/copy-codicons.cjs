const fs = require('fs');
const path = require('path');
const src = path.join(__dirname, '../node_modules/@vscode/codicons/dist');
const dest = path.join(__dirname, '../dist/codicons');
fs.mkdirSync(dest, { recursive: true });
['codicon.css', 'codicon.ttf'].forEach((f) => {
  try {
    fs.copyFileSync(path.join(src, f), path.join(dest, f));
  } catch (e) {
    console.warn('copy-codicons:', e.message);
  }
});
