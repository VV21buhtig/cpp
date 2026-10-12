// Main process: грузит libengine.so через koffi, качает кадры и JSON.
// Никакой математики сцены здесь нет — только труба C-ABI -> IPC.
const path = require('path');
const { app, BrowserWindow, ipcMain } = require('electron');
const koffi = require('koffi');

const ROOT = path.join(__dirname, '..');
const LIB = path.join(ROOT, 'engine', 'build', 'libengine.so.0');
const FW = 1280, FH = 720;
// Движок ищет ассеты/шейдеры от корня репо — работаем оттуда.
process.chdir(ROOT);
// Headless-проверка тракта: EL_CDP=порт -> DevTools для скриншота/оценок.
if (process.env.EL_CDP) app.commandLine.appendSwitch('remote-debugging-port', process.env.EL_CDP);

const lib = koffi.load(LIB);
const eng_version = lib.func('eng_version', 'str', []);
const eng_view_open = lib.func('eng_view_open', 'void *', ['uint', 'int']);
const eng_view_frame = lib.func('eng_view_frame', 'int', ['void *', 'uint8_t *', 'int', 'int']);
const eng_view_scene = lib.func('eng_view_scene', 'str', ['void *']);
const eng_view_frames = lib.func('eng_view_frames', 'int', ['void *']);
const eng_view_close = lib.func('eng_view_close', 'void', ['void *']);
const eng_view_resize = lib.func('eng_view_resize', 'int', ['void *', 'int', 'int']);

const DOMAINS = { voxel: 1, sdf: 2, vector: 4, pixel: 8, mesh: 16 };
const BACKENDS = { webgpu: 1, gl: 2, vk: 3 };

let view = null;
let domain = DOMAINS.voxel;
let backend = BACKENDS.gl;
let playing = false;
let vw = FW, vh = FH; // текущий размер кадра (динамика через resize)
let frameBuf = Buffer.alloc(vw * vh * 4);

function boot() {
  if (view) eng_view_close(view);
  view = eng_view_open(domain, backend);
  if (view === null || koffi.address(view) === 0n) {
    view = null;
    return false;
  }
  return true;
}

function createWindow() {
  const win = new BrowserWindow({
    width: 1400, height: 900,
    backgroundColor: '#1e1e1e',
    webPreferences: { preload: path.join(__dirname, 'preload.js') },
  });
  win.loadFile(path.join(__dirname, 'index.html'));
}

app.whenReady().then(() => {
  console.log('engine:', eng_version());
  console.log('boot:', boot() ? 'OK' : 'FAIL');
  // Кадр тикает тут (main), renderer только показывает последний.
  setInterval(() => {
    if (view && playing) eng_view_frame(view, frameBuf, vw, vh);
  }, 33);
  ipcMain.handle('scene', () => (view ? eng_view_scene(view) : '{"entities":[]}'));
  ipcMain.handle('frame', () => {
    if (view && !playing) eng_view_frame(view, frameBuf, vw, vh);
    return frameBuf;
  });
  ipcMain.handle('frames', () => (view ? eng_view_frames(view) : 0));
  ipcMain.handle('play', (_e, on) => { playing = !!on; });
  ipcMain.handle('switch', (_e, d, b) => {
    if (DOMAINS[d]) domain = DOMAINS[d];
    if (BACKENDS[b]) backend = BACKENDS[b];
    const ok = boot();
    return ok;
  });
  // Динамика вьюпорта: ResizeObserver сцены -> бекенд -> новый буфер.
  // Возвращает [w, h] после clamp (VW_MIN..VW_MAX) — renderer ставит в canvas.
  ipcMain.handle('resize', (_e, w, h) => {
    if (!view) return [vw, vh];
    w = Math.round(w);
    h = Math.round(h);
    if (w === vw && h === vh) return [vw, vh];
    if (eng_view_resize(view, w, h) !== 1) return [vw, vh];
    vw = Math.min(2048, Math.max(64, w));
    vh = Math.min(2048, Math.max(64, h));
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
  if (view) eng_view_close(view);
  view = null;
  if (process.platform !== 'darwin') app.quit();
});
