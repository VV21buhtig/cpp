// Preload: только явные каналы, никакого node в renderer напрямую.
import { contextBridge, ipcRenderer } from 'electron';

export interface EngBridge {
  scene(): Promise<string>;
  frame(): Promise<Buffer>;
  frames(): Promise<number>;
  play(on: boolean): Promise<void>;
  switch(domain: string, backend: string): Promise<boolean>;
  resize(w: number, h: number): Promise<[number, number]>;
  ack(): void;
  onFrame(cb: (buf: Buffer) => void): void;
  ping(): Promise<unknown>;
}

declare global {
  interface Window {
    eng: EngBridge;
  }
}

const api: EngBridge = {
  scene: () => ipcRenderer.invoke('scene'),
  frame: () => ipcRenderer.invoke('frame'),
  frames: () => ipcRenderer.invoke('frames'),
  play: (on: boolean) => ipcRenderer.invoke('play', on),
  switch: (domain: string, backend: string) => ipcRenderer.invoke('switch', domain, backend),
  resize: (w: number, h: number) => ipcRenderer.invoke('resize', w, h),
  ack: () => ipcRenderer.send('frame-ack'),
  onFrame: (cb: (buf: Buffer) => void) => {
    ipcRenderer.on('frame', (_e, buf: Buffer) => cb(buf));
  },
  ping: () => ipcRenderer.invoke('ping'),
};

contextBridge.exposeInMainWorld('eng', api);
