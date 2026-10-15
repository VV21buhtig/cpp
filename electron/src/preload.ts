// Preload: только явные каналы, никакого node в renderer напрямую.
// Пикселей кадра тут нет и не будет (антипаттерн удалён): вьюпорт — direct_view.
import { contextBridge, ipcRenderer } from 'electron';

export interface EngBridge {
  scene(): Promise<string>;
  switch(domain: string, backend: string): Promise<{ ok: boolean; error: string }>;
  native(on: boolean): Promise<boolean>;
  shaders(): Promise<string[]>;
  shaderRead(id: string): Promise<string>;
  shader(id: string, code: string): Promise<{ ok: boolean; log: string }>;
  onDirect(cb: (on: boolean) => void): void;
  ping(): Promise<unknown>;
}

declare global {
  interface Window {
    eng: EngBridge;
  }
}

const api: EngBridge = {
  scene: () => ipcRenderer.invoke('scene'),
  switch: (domain: string, backend: string) => ipcRenderer.invoke('switch', domain, backend),
  native: (on: boolean): Promise<boolean> => ipcRenderer.invoke('native', on),
  shaders: () => ipcRenderer.invoke('shaders'),
  shaderRead: (id: string) => ipcRenderer.invoke('shader-read', id),
  shader: (id: string, code: string) => ipcRenderer.invoke('shader', id, code),
  onDirect: (cb: (on: boolean) => void) => {
    ipcRenderer.on('direct', (_e, on: boolean) => cb(on));
  },
  ping: () => ipcRenderer.invoke('ping'),
};

contextBridge.exposeInMainWorld('eng', api);
