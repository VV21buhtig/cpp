// Строгие типы C-ABI движка. Фронт не хранит сцену сам: дерево — из JSON.
// Кадров тут нет (антипаттерн удалён): вьюпорт — direct_view, GPU->GPU.
// CPU-путь кадра (frame/download) живёт только в C-тестах и VOX_SHOT.
import path from 'path';
import koffi from 'koffi';

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
const c_scene = lib.func('const char *eng_view_scene(void *)');
const c_close = lib.func('void eng_view_close(void *)');
const c_shader = lib.func('int eng_view_shader(void *, str, str, char *, int)');

export const EngineAPI = {
  version(): string {
    return c_version() as string;
  },
  open(domain: Domain, backend: Backend): ViewHandle | null {
    const h = c_open(domain, backend) as unknown as ViewHandle | null;
    if (!h || koffi.address(h as object) === 0n) return null;
    return h;
  },
  scene(view: ViewHandle): SceneTree {
    const s = c_scene(view) as unknown as string;
    return JSON.parse(s.length ? s : '{"entities":[]}') as SceneTree;
  },
  close(view: ViewHandle): void {
    c_close(view);
  },
  shader(view: ViewHandle, id: string, code: string): { ok: boolean; log: string } {
    const lb = Buffer.alloc(2048);
    const r = c_shader(view, id, code, lb, lb.length) as unknown as number;
    const log = lb.toString('utf8').split('\0')[0] || '';
    return { ok: r === 1, log };
  },
};
