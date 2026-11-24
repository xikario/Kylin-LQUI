const { contextBridge, ipcRenderer, webUtils } = require('electron');

const IN = ['claude', 'play', 'say', 'quiet', 'whisper', 'chat-thinking', 'chat-reply', 'fling', 'fling-v', 'bounce', 'config', 'whisper-now'];
const OUT = ['pet-shape', 'ignore-mouse', 'drag-start', 'drag-end', 'menu', 'open-chat'];

contextBridge.exposeInMainWorld('petApi', {
  filePath: file => webUtils.getPathForFile(file),
  drop: payload => ipcRenderer.invoke('station-drop', payload),
  nativeShape: process.platform === 'linux',
  on: (ch, fn) => {
    if (IN.includes(ch)) ipcRenderer.on(ch, (_e, data) => fn(data));
  },
  send: (ch, data) => {
    if (OUT.includes(ch)) ipcRenderer.send(ch, data);
  },
});
