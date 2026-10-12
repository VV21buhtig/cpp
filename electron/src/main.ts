// Main process: труба C-ABI -> IPC. Никакой математики сцены тут нет.
import path from 'path';
import { app, BrowserWindow, ipcMain } from 'electron';
import { EngineAPI, Domain, Backend, ViewHandle, FW, FH, VW_MIN, VW_MAX } from './engine_abi';

const ROOT = path.join(__dirname, '..', '..');
// Движок ищет ассеты/шейдеры от корня репо — работаем оттуда.
process.chdir(ROOT);
// Headless-проверка тракта: EL_CDP=порт -> DevTools для скриншота/оценок.
if (process.env['EL_CDP']) app.commandLine.appendSwitch('remote-debugging-port', process.env['EL_CDP'] as string);

let view: ViewHandle | null = null;
let domain: Domain = Domain.Voxel;
let backend: Backend = Backend.GL;
let playing = false;
let vw = FW, vh = FH; // текущий размер кадра (динамика через resize)
let frameBuf: Buffer = Buffer.alloc(vw * vh * 4);
let mainWin: BrowserWindow | null = null;

function boot(): boolean {
  if (view) EngineAPI.close(view);
  view = EngineAPI.open(domain, backend);
  return view !== null;
}

function createWindow(): void {
  const win = new BrowserWindow({
    width: 1400, height: 900,
    backgroundColor: '#1e1e1e',
    webPreferences: { preload: path.join(__dirname, 'preload.js') },
  });
  win.loadFile(path.join(__dirname, 'index.html'));
  mainWin = win;
}

function clamp(v: number): number {
  return Math.min(VW_MAX, Math.max(VW_MIN, Math.round(v)));
}

app.whenReady().then(() => {
  console.log('engine:', EngineAPI.version());
  console.log('boot:', boot() ? 'OK' : 'FAIL');
  // Кадр тикает тут (main) и ТОЛКАЕТСЯ в renderer (push, не pull):
  // rAF-цикл renderer забирает последний готовый буфер без IPC round-trip.
  // Самопейсинг: следующий шлём только после ack отрисовки (без очереди).
  let acked = true;
  ipcMain.on('frame-ack', () => {
    acked = true;
  });
  setInterval(() => {
    if (view && playing && acked && mainWin && !mainWin.isDestroyed()) {
      acked = false;
      EngineAPI.frame(view, frameBuf, vw, vh);
      mainWin.webContents.send('frame', frameBuf);
    }
  }, 16);
  ipcMain.handle('scene', (): string => {
    if (!view) return '{"entities":[]}';
    try {
      return JSON.stringify(EngineAPI.scene(view));
    } catch {
      return '{"entities":[]}';
    }
  });
  ipcMain.handle('frame', (): Buffer => {
    if (view && !playing) EngineAPI.frame(view, frameBuf, vw, vh);
    return frameBuf;
  });
  ipcMain.handle('frames', (): number => (view ? EngineAPI.frames(view) : 0));
  ipcMain.handle('play', (_e, on: unknown): void => {
    playing = !!on;
  });
  ipcMain.handle('switch', (_e, d: string, b: string): boolean => {
    const dm = { voxel: Domain.Voxel, sdf: Domain.Sdf, vector: Domain.Vector, pixel: Domain.Pixel, mesh: Domain.Mesh } as const;
    const bm = { webgpu: Backend.WebGPU, gl: Backend.GL, vk: Backend.VK } as const;
    if (d in dm) domain = dm[d as keyof typeof dm];
    if (b in bm) backend = bm[b as keyof typeof bm];
    return boot();
  });
  // Динамика вьюпорта: ResizeObserver сцены -> бекенд -> новый буфер.
  // Возвращает [w, h] после clamp — renderer ставит в canvas.
  ipcMain.handle('resize', (_e, w: unknown, h: unknown): [number, number] => {
    if (!view) return [vw, vh];
    const cw = clamp(Number(w)), ch = clamp(Number(h));
    if (cw === vw && ch === vh) return [vw, vh];
    if (!EngineAPI.resize(view, cw, ch)) return [vw, vh];
    vw = cw;
    vh = ch;
    frameBuf = Buffer.alloc(vw * vh * 4);
    return [vw, vh];
  });
  ipcMain.handle('ping', () => ({ domain, backend, playing }));
  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', () => {
  if (view) EngineAPI.close(view);
  view = null;
  if (process.platform !== 'darwin') app.quit();
});
