// Preload: только явные каналы, никакого node в renderer напрямую.
const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('eng', {
  scene: () => ipcRenderer.invoke('scene'),
  frame: () => ipcRenderer.invoke('frame'),
  frames: () => ipcRenderer.invoke('frames'),
  play: (on) => ipcRenderer.invoke('play', on),
  switch: (domain, backend) => ipcRenderer.invoke('switch', domain, backend),
  resize: (w, h) => ipcRenderer.invoke('resize', w, h),
  ping: () => ipcRenderer.invoke('ping'),
});
