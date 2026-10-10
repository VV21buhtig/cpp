"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
const electron_1 = require("electron");
// Мост в Node: страница правит только settings.cfg, больше ничего нельзя.
electron_1.contextBridge.exposeInMainWorld("sdfSettings", {
    load: () => electron_1.ipcRenderer.invoke("settings:load"),
    save: (s) => electron_1.ipcRenderer.invoke("settings:save", s),
});
