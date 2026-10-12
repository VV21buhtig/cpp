// Строгие типы C-ABI движка. Фронт не хранит сцену сам:
// дерево — из JSON бекенда, кадр — из буфера бекенда.
import path from 'path';
import koffi from 'koffi';

export const FW = 1280;
export const FH = 720;
export const VW_MIN = 64;
export const VW_MAX = 2048;

export const enum Domain {
  Voxel = 1,
  Sdf = 2,
  Vector = 4,
  Pixel = 8,
  Mesh = 16,
}

export const enum Backend {
  WebGPU = 1,
  GL = 2,
  VK = 3,
}

export interface SceneEntity {
  id: number;
  name: string;
  parent: number;
  visible: number;
  pos: [number, number, number];
  rot: [number, number, number];
  scale: [number, number, number];
}

export interface SceneTree {
  entities: SceneEntity[];
}

export type ViewHandle = object;

const ROOT = path.join(__dirname, '..', '..');
const LIB = path.join(ROOT, 'engine', 'build', 'libengine.so.0');

const lib = koffi.load(LIB);

// Явные сигнатуры каждой функции (ошибка типов — на этапе tsc, не segfault).
const c_version = lib.func('const char *eng_version()');
const c_open = lib.func('void *eng_view_open(uint, int)');
const c_frame = lib.func('int eng_view_frame(void *, uint8_t *, int, int)');
const c_scene = lib.func('const char *eng_view_scene(void *)');
const c_frames = lib.func('int eng_view_frames(void *)');
const c_close = lib.func('void eng_view_close(void *)');
const c_resize = lib.func('int eng_view_resize(void *, int, int)');

export const EngineAPI = {
  version(): string {
    return c_version() as string;
  },
  open(domain: Domain, backend: Backend): ViewHandle | null {
    const h = c_open(domain, backend) as unknown as ViewHandle | null;
    if (!h || koffi.address(h as object) === 0n) return null;
    return h;
  },
  frame(view: ViewHandle, buf: Buffer, w: number, h: number): boolean {
    return (c_frame(view, buf, w, h) as unknown as number) === 1;
  },
  scene(view: ViewHandle): SceneTree {
    const s = c_scene(view) as unknown as string;
    return JSON.parse(s.length ? s : '{"entities":[]}') as SceneTree;
  },
  frames(view: ViewHandle): number {
    return c_frames(view) as unknown as number;
  },
  close(view: ViewHandle): void {
    c_close(view);
  },
  resize(view: ViewHandle, w: number, h: number): boolean {
    return (c_resize(view, Math.round(w), Math.round(h)) as unknown as number) === 1;
  },
};
