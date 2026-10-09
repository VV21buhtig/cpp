import { contextBridge, ipcRenderer } from "electron";

// Мост в Node: страница правит только settings.cfg, больше ничего нельзя.
contextBridge.exposeInMainWorld("sdfSettings", {
  load: (): Promise<{ gamma: number; exposure: number; fog: number }> =>
    ipcRenderer.invoke("settings:load"),
  save: (s: { gamma: number; exposure: number; fog: number }): Promise<void> =>
    ipcRenderer.invoke("settings:save", s),
});
