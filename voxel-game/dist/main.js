"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
const electron_1 = require("electron");
const fs = require("fs");
const path = require("path");
const settingsPath = path.join(electron_1.app.getAppPath(), "settings.cfg");
function parseSettings() {
    const s = { gamma: 2.2, exposure: 1.0, fog: 1.0, fov: 1.6, shadow: 1 };
    try {
        for (const line of fs.readFileSync(settingsPath, "utf8").split("\n")) {
            const [k, v] = line.trim().split(/\s+/);
            const f = parseFloat(v);
            if (!isFinite(f))
                continue;
            if (k === "gamma")
                s.gamma = Math.min(4, Math.max(0.5, f));
            else if (k === "exposure")
                s.exposure = Math.min(4, Math.max(0.1, f));
            else if (k === "fog")
                s.fog = Math.min(3, Math.max(0, f));
            else if (k === "fov")
                s.fov = Math.min(4, Math.max(0.5, f));
            else if (k === "shadow")
                s.shadow = f >= 0.5 ? 1 : 0;
        }
    }
    catch {
        /* нет файла — дефолты */
    }
    return s;
}
function createWindow() {
    electron_1.ipcMain.handle("settings:load", () => parseSettings());
    electron_1.ipcMain.handle("settings:save", (_e, s) => {
        const cur = parseSettings();
        const out = {
            gamma: isFinite(s.gamma) ? Math.min(4, Math.max(0.5, s.gamma)) : cur.gamma,
            exposure: isFinite(s.exposure) ? Math.min(4, Math.max(0.1, s.exposure)) : cur.exposure,
            fog: isFinite(s.fog) ? Math.min(3, Math.max(0, s.fog)) : cur.fog,
            fov: isFinite(s.fov) ? Math.min(4, Math.max(0.5, s.fov)) : cur.fov,
            shadow: s.shadow >= 0.5 ? 1 : 0,
        };
        fs.writeFileSync(settingsPath, `gamma ${out.gamma.toFixed(3)}\nexposure ${out.exposure.toFixed(3)}\nfog ${out.fog.toFixed(3)}\nfov ${out.fov.toFixed(3)}\nshadow ${out.shadow}\n`);
    });
    const win = new electron_1.BrowserWindow({
        width: 480,
        height: 360,
        autoHideMenuBar: true,
        backgroundColor: "#101216",
        webPreferences: {
            nodeIntegration: false,
            contextIsolation: true,
            preload: path.join(__dirname, "preload.js"),
        },
    });
    win.loadFile(path.join(__dirname, "..", "index.html"));
}
electron_1.app.whenReady().then(() => {
    createWindow();
    electron_1.app.on("activate", () => {
        if (electron_1.BrowserWindow.getAllWindows().length === 0)
            createWindow();
    });
});
electron_1.app.on("window-all-closed", () => {
    if (process.platform !== "darwin")
        electron_1.app.quit();
});
