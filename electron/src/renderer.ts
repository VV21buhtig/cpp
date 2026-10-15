// Renderer: меню/дерево/шейдеры. Сцены тут нет, КАДРОВ тут нет.
// Вьюпорт — отдельное окно direct_view (GPU->GPU). Пиксели через IPC запрещены
// (антипаттерн удалён: frameBuf/push/pull/putImageData/createImageBitmap/ack).
const tree = document.getElementById('tree') as HTMLElement;
const stat = document.getElementById('stat') as HTMLElement;
const vpEl = document.getElementById('vp') as HTMLElement;
const viewBtn = document.getElementById('viewbtn') as HTMLButtonElement;
const nativeBox = document.getElementById('native') as HTMLInputElement;
const domSel = document.getElementById('dom') as HTMLSelectElement;
const beSel = document.getElementById('be') as HTMLSelectElement;

interface Ent {
  id: number;
  name: string;
}

let selectedId = 0;

function paintSel(): void {
  const rows = tree.querySelectorAll<HTMLElement>('.ent');
  rows.forEach((r) => {
    r.classList.toggle('sel', r.dataset['eid'] === String(selectedId));
  });
}

async function refreshTree(): Promise<void> {
  try {
    const sc = JSON.parse(await window.eng.scene()) as { entities: Ent[] };
    tree.innerHTML = '';
    for (const e of sc.entities) {
      if (e.name === 'Scene') continue;
      const d = document.createElement('div');
      d.className = 'ent';
      d.textContent = e.name;
      d.dataset['eid'] = String(e.id);
      d.onclick = () => {
        selectedId = e.id;
        paintSel();
      };
      tree.appendChild(d);
    }
    paintSel();
    stat.textContent = `entities: ${sc.entities.length}`;
  } catch (err) {
    stat.textContent = 'scene err: ' + String(err);
  }
}

function setVp(open: boolean): void {
  vpEl.textContent = open ? 'viewport: direct (no copy)' : 'viewport: closed';
  viewBtn.textContent = open ? 'Hide viewport' : 'Show viewport';
  viewBtn.classList.toggle('on', open);
}

async function setDirect(on: boolean): Promise<void> {
  nativeBox.checked = on;
  stat.textContent = on ? 'viewport: opening…' : 'viewport: closing…';
  const ok = await window.eng.native(on);
  await refreshTree();
  if (on && !ok) {
    nativeBox.checked = false;
    stat.textContent += ' | NATIVE FAILED (GL only?)';
  }
  setVp(on && ok);
}

viewBtn.onclick = (): void => {
  void setDirect(!nativeBox.checked);
};

async function reswitch(): Promise<void> {
  const statEl = stat;
  statEl.textContent = 'loading…';
  domSel.disabled = true;
  beSel.disabled = true;
  try {
    const r = await window.eng.switch(domSel.value, beSel.value);
    selectedId = 0;
    // refreshTree затирает stat своим счётчиком — поэтому сначала дерево,
    // потом поверх дописываем итог свитча (было: индикация жила 0мс).
    await refreshTree();
    const tail = statEl.textContent ? ` | ${statEl.textContent}` : '';
    statEl.textContent = r.ok
      ? `core: ${domSel.value} / ${beSel.value}${tail}`
      : `SWITCH FAILED: ${r.error}${tail}`;
  } catch (err) {
    statEl.textContent = 'SWITCH ERROR: ' + String(err);
  } finally {
    domSel.disabled = false;
    beSel.disabled = false;
  }
}
domSel.onchange = () => void reswitch();
beSel.onchange = () => void reswitch();
nativeBox.onchange = (ev: Event): void => {
  void setDirect((ev.target as HTMLInputElement).checked);
};
// Процесс умер снаружи (крестик): снять чекбокс, статус закрыть.
window.eng.onDirect((on: boolean) => {
  nativeBox.checked = on;
  setVp(on);
  if (!on) {
    stat.textContent += ' | direct closed';
    void refreshTree();
  }
});

void refreshTree();

// --- Monaco: панель шейдеров (VFS + eng_shader_reload) ---
// Monaco грузится AMD-лоадером из node_modules (без бандла);
// типизируем только 3 используемые точки (фасад, не any по всему коду).
interface MonacoEditor {
  getValue(): string;
  setValue(v: string): void;
  updateOptions(o: object): void;
}
interface MonacoNS {
  editor: {
    create(el: HTMLElement, opts: object): MonacoEditor;
    setTheme(t: string): void;
  };
}
interface AmdRequire {
  (mods: string[], cb: () => void): void;
  config(opts: object): void;
}
function getAmd(): AmdRequire {
  return (window as unknown as { require: AmdRequire }).require;
}

function langOf(id: string): string {
  if (id.endsWith('.wgsl')) return 'rust';
  if (id.endsWith('.vert')) return 'cpp';
  return 'cpp';
}

let med: MonacoEditor | null = null;
let monacoReady: ((m: MonacoNS) => void)[] = [];

function withMonaco(cb: (m: MonacoNS) => void): void {
  const g = window as unknown as { monaco?: MonacoNS };
  if (g.monaco) {
    cb(g.monaco);
    return;
  }
  monacoReady.push(cb);
}

function bootMonaco(): void {
  (globalThis as unknown as { MonacoEnvironment?: object }).MonacoEnvironment = {
    getWorkerUrl: () => '',
  };
  const amd = getAmd();
  amd.config({
    paths: { vs: '../node_modules/monaco-editor/min/vs' },
  });
  amd(['vs/editor/editor.main'], () => {
    const g = window as unknown as { monaco: MonacoNS };
    g.monaco.editor.setTheme('vs-dark');
    for (const cb of monacoReady) cb(g.monaco);
    monacoReady = [];
  });
}

async function bootShaders(): Promise<void> {
  const sel = document.getElementById('shsel') as HTMLSelectElement;
  const log = document.getElementById('shlog') as HTMLElement;
  const st = document.getElementById('shstat') as HTMLElement;
  const ids = await window.eng.shaders();
  sel.innerHTML = '';
  for (const id of ids) {
    const o = document.createElement('option');
    o.value = id;
    o.textContent = id;
    sel.appendChild(o);
  }
  const load = async (): Promise<void> => {
    const text = await window.eng.shaderRead(sel.value);
    withMonaco((m) => {
      if (!med) {
        med = m.editor.create(document.getElementById('med') as HTMLElement, {
          value: text,
          language: langOf(sel.value),
          theme: 'vs-dark',
          minimap: { enabled: false },
          fontSize: 12,
        });
      } else {
        med.setValue(text);
      }
    });
    log.textContent = '';
    log.classList.remove('err');
    st.textContent = sel.value;
  };
  sel.onchange = () => void load();
  (document.getElementById('shreload') as HTMLButtonElement).onclick = async (): Promise<void> => {
    if (!med) return;
    const r = await window.eng.shader(sel.value, med.getValue());
    log.textContent = (r.ok ? 'OK ' : 'FAIL ') + sel.value + '\n' + r.log;
    log.classList.toggle('err', !r.ok);
  };
  const panel = document.getElementById('shaders') as HTMLElement;
  (document.getElementById('shtoggle') as HTMLButtonElement).onclick = (): void => {
    const open = panel.style.display === 'none';
    panel.style.display = open ? 'block' : 'none';
    if (open) void load();
  };
  await load();
}

bootMonaco();
void bootShaders();
