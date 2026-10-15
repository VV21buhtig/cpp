// Main process: труба C-ABI -> IPC. Никакой математики сцены тут нет.
//
// АНТИПАТТЕРН (запрещено возвращать): передача пикселей кадра через CPU/IPC
// в canvas (frameBuf, webContents.send('frame'), putImageData/createImageBitmap).
// Вьюпорт — ТОЛЬКО отдельный процесс direct_view (GPU->GPU, ноль копий).
// Фронт: дерево сцены, свитч домена, шейдеры. Сцены тут нет.
import path from 'path';
import fs from 'fs';
import { spawn, ChildProcess } from 'child_process';
import { app, BrowserWindow, ipcMain } from 'electron';
import { EngineAPI, Domain, Backend, ViewHandle } from './engine_abi';

const ROOT = path.join(__dirname, '..', '..');
// Движок ищет ассеты/шейдеры от корня репо — работаем оттуда.
process.chdir(ROOT);
// Headless-проверка: EL_CDP=порт -> DevTools для скриншота/оценок.
if (process.env['EL_CDP']) app.commandLine.appendSwitch('remote-debugging-port', process.env['EL_CDP'] as string);

let view: ViewHandle | null = null;
let domain: Domain = Domain.Voxel;
let backend: Backend = Backend.GL;
let mainWin: BrowserWindow | null = null;

function boot(): boolean {
  if (view) EngineAPI.close(view);
  view = null;
  try {
    view = EngineAPI.open(domain, backend);
  } catch (err) {
    console.error('boot threw:', err);
    view = null;
  }
  return view !== null;
}

function createWindow(): void {
  // Окно меню/дерева/шейдеров. Вьюпорта внутри нет — он отдельным окном.
  const win = new BrowserWindow({
    width: 1600, height: 950,
    backgroundColor: '#1e1e1e',
    webPreferences: { preload: path.join(__dirname, 'preload.js') },
  });
  win.loadFile(path.join(__dirname, 'index.html'));
  mainWin = win;
}

app.whenReady().then(() => {
  console.log('engine:', EngineAPI.version());
  console.log('boot:', boot() ? 'OK' : 'FAIL');
  ipcMain.handle('scene', (): string => {
    if (!view) return '{"entities":[]}';
    try {
      return JSON.stringify(EngineAPI.scene(view));
    } catch {
      return '{"entities":[]}';
    }
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
  // Прямой вывод (как Roblox/UE5): ОТДЕЛЬНЫЙ процесс direct_view, своё окно,
  // свой swapchain — GPU->GPU, ноль копий. In-process GLFW-окно в main виснет
  // десятки секунд (чужой event-loop Chromium), поэтому только spawn.
  // Только GL (у VK/WebGPU своих окон пока нет — скажет NATIVE FAILED).
  let directProc: ChildProcess | null = null;
  const killDirect = (): void => {
    if (directProc && !directProc.killed) directProc.kill();
    directProc = null;
  };
  // Фронт держит чекбокс честно: закрытие окна снаружи его снимает.
  const sendDirect = (on: boolean): void => {
    if (mainWin && !mainWin.isDestroyed()) mainWin.webContents.send('direct', on);
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
      // Окно закрыли крестиком — процесс умер: отпустить флаг И сказать фронту.
      directProc.on('error', () => {
        killDirect();
        sendDirect(false);
      });
      directProc.on('exit', () => {
        killDirect();
        sendDirect(false);
      });
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
  ipcMain.handle('ping', () => ({ domain, backend }));
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
