// Main process: труба C-ABI -> IPC. Никакой математики сцены тут нет.
import path from 'path';
import fs from 'fs';
import { spawn, ChildProcess } from 'child_process';
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
  view = null;
  try {
    view = EngineAPI.open(domain, backend);
    // Новое view рождается в дефолтном размере: сразу тянуть к текущему,
    // иначе frame() вечно мажет мимо (w/h mismatch -> return 0, счётчик стоит).
    if (view && (vw !== FW || vh !== FH)) EngineAPI.resize(view, vw, vh);
  } catch (err) {
    console.error('boot threw:', err);
    view = null;
  }
  return view !== null;
}

function createWindow(): void {
  // Рекомендуемый размер: инструменты и терминал видны, канвас не душит.
  const win = new BrowserWindow({
    width: 1600, height: 950,
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
  ipcMain.handle('switch', (_e, d: string, b: string): { ok: boolean; error: string } => {
    const dm = { voxel: Domain.Voxel, sdf: Domain.Sdf, vector: Domain.Vector, pixel: Domain.Pixel, mesh: Domain.Mesh } as const;
    const bm = { webgpu: Backend.WebGPU, gl: Backend.GL, vk: Backend.VK } as const;
    if (d in dm) domain = dm[d as keyof typeof dm];
    if (b in bm) backend = bm[b as keyof typeof bm];
    try {
      if (boot()) {
        // Прямое окно следит за доменом (перезапуск процесса на новый домен).
        if (directProc) spawnDirect();
        return { ok: true, error: '' };
      }
      return { ok: false, error: `no core for domain ${domain} x backend ${backend}` };
    } catch (err) {
      const m = err as Error;
      return { ok: false, error: String(m?.message || err) };
    }
  });
  // Шейдеры (Monaco): список из движка, чтение из VFS, reload в ядро.
  // id — путь как его знают ядра (repo-relative, см. слоты RhiGlProg).
  ipcMain.handle('shaders', (): string[] => {
    return [
      'shaders/gl/sky.vs', 'shaders/gl/sky.fs',
      'shaders/gl/lighting.vs', 'shaders/gl/lighting.fs',
      'shaders/sdf_gl/ray.vert', 'shaders/sdf_gl/ray.frag',
      'shaders/mesh/mesh.vert', 'shaders/mesh/mesh.frag',
      'shaders/blit/blit.vert', 'shaders/blit/blit.frag',
    ];
  });
  ipcMain.handle('shader-read', (_e, id: unknown): string => {
    if (typeof id !== 'string' || id.includes('..')) return '';
    try {
      return fs.readFileSync(path.join(ROOT, 'engine', String(id)), 'utf8');
    } catch {
      return '';
    }
  });
  ipcMain.handle(
    'shader',
    (_e, id: unknown, code: unknown): { ok: boolean; log: string } => {
      if (!view || typeof id !== 'string' || typeof code !== 'string')
        return { ok: false, log: 'нет вида/id/кода' };
      return EngineAPI.shader(view, id, code);
    }
  );
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
  // Крутилка вьюпорта: drag/zoom клиента -> угол камеры (не сцена).
  ipcMain.handle('orbit', (_e, dA: unknown, dH: unknown, dZ: unknown): void => {
    if (view) EngineAPI.orbit(view, Number(dA), Number(dH), Number(dZ));
  });
  // Прямой вывод (как Roblox/UE5): ОТДЕЛЬНЫЙ процесс direct_view, своё окно,
  // свой swapchain — GPU->GPU, ноль копий. In-process GLFW-окно в main виснет
  // десятки секунд (чужой event-loop Chromium), поэтому только spawn.
  // Только GL. Выключение чекбокса убивает процесс (fallback на canvas-трек).
  let directProc: ChildProcess | null = null;
  const killDirect = (): void => {
    if (directProc && !directProc.killed) directProc.kill();
    directProc = null;
  };
  // Спавн прямого вьювера под ТЕКУЩИЕ domain/backend (GL-only).
  const spawnDirect = (): boolean => {
    killDirect();
    if (backend !== Backend.GL) return false;
    const dn = { 1: 'voxel', 2: 'sdf', 4: 'vector', 8: 'pixel', 16: 'mesh' }[domain as number] || 'voxel';
    try {
      directProc = spawn(path.join(ROOT, 'engine', 'build', 'direct_view'),
        ['gl', dn], { cwd: ROOT, stdio: 'ignore', detached: true });
      directProc.unref();
      directProc.on('error', killDirect);
      return true;
    } catch {
      directProc = null;
      return false;
    }
  };
  app.on('will-quit', killDirect);
  ipcMain.handle('native', (_e, on: unknown): boolean => {
    if (!on) {
      killDirect();
      return true;
    }
    return spawnDirect();
  });
  ipcMain.handle('ping', () => ({ domain, backend, playing }));
  // Пикинг: клик по канвасу -> id сущности (0 = мимо). Только MESH-домен v1.
  ipcMain.handle('pick', (_e, x: unknown, y: unknown): number => {
    if (!view || typeof x !== 'number' || typeof y !== 'number') return 0;
    return EngineAPI.pick(view, x, y);
  });
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
