// Renderer: рисует то, что дал main. Состояния сцены тут нет.
// Push-кадры складываем в последний; rAF рисует без IPC round-trip,
// загрузка в GPU через createImageBitmap (не putImageData).
const cv = document.getElementById('cv') as HTMLCanvasElement;
const ctx = cv.getContext('2d') as CanvasRenderingContext2D;
const tree = document.getElementById('tree') as HTMLElement;
const stat = document.getElementById('stat') as HTMLElement;
const fpsEl = document.getElementById('fps') as HTMLElement;
const playBtn = document.getElementById('play') as HTMLButtonElement;
const domSel = document.getElementById('dom') as HTMLSelectElement;
const beSel = document.getElementById('be') as HTMLSelectElement;

let playing = false;
let lastT = performance.now(), nFrames = 0;
// Путь вывода: true = createImageBitmap+drawImage, false = putImageData.
// Выбирается замером на железе (в софтверном Chromium bitmap медленнее).
const USE_BITMAP = true;
let pending: Uint8ClampedArray<ArrayBuffer> | null = null;
let pendingW = 0, pendingH = 0;

interface Ent {
  name: string;
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
      tree.appendChild(d);
    }
    stat.textContent = `entities: ${sc.entities.length}`;
  } catch (err) {
    stat.textContent = 'scene err: ' + String(err);
  }
}

function snap(buf: Buffer): void {
  // Вид прямо на IPC-буфер БЕЗ копии: Electron уже склонировал его
  // при передаче (свой ArrayBuffer), reuse main-буфера нас не рвёт.
  // Типизация: только свежий ArrayBuffer ест ImageData (см. выше).
  const view = new Uint8ClampedArray(buf.buffer, buf.byteOffset, buf.byteLength);
  const ab = view.buffer;
  if (ab instanceof ArrayBuffer) {
    pending = new Uint8ClampedArray(ab, view.byteOffset, view.byteLength);
  } else {
    const fresh = new Uint8ClampedArray(view.length);
    fresh.set(view);
    pending = fresh;
  }
  pendingW = cv.width;
  pendingH = cv.height;
}

window.eng.onFrame((buf: Buffer) => {
  snap(buf);
});

async function pullOnce(): Promise<void> {
  // Пауза: кадр по требованию (pull), в play — только push.
  if (playing) return;
  const buf = await window.eng.frame();
  snap(buf);
}

async function loop(): Promise<void> {
  if (!playing) await pullOnce();
  if (pending && pendingW === cv.width && pendingH === cv.height) {
    // Путь вывода выбирается замером (см. доку): bitmap vs putImage.
    if (USE_BITMAP) {
      try {
        const bmp = await createImageBitmap(new ImageData(pending, cv.width, cv.height));
        ctx.drawImage(bmp, 0, 0);
        bmp.close();
        nFrames++;
      } catch {
        // кадр в пути после ресайза — пропуск
      }
    } else {
      ctx.putImageData(new ImageData(pending, cv.width, cv.height), 0, 0);
      nFrames++;
    }
    pending = null;
    window.eng.ack(); // следующий кадр можно слать (самопейсинг линка)
  }
  const now = performance.now();
  if (now - lastT > 1000) {
    const n = await window.eng.frames();
    fpsEl.textContent = `fps ${Math.round((nFrames * 1000) / (now - lastT))} / eng ${n}`;
    nFrames = 0;
    lastT = now;
  }
  requestAnimationFrame(() => void loop());
}

playBtn.onclick = async (): Promise<void> => {
  playing = !playing;
  await window.eng.play(playing);
  playBtn.textContent = playing ? 'Stop' : 'Play';
  playBtn.classList.toggle('on', playing);
};

async function reswitch(): Promise<void> {
  const ok = await window.eng.switch(domSel.value, beSel.value);
  stat.textContent = ok ? `core: ${domSel.value} / ${beSel.value}` : 'NO CORE for domain x backend';
  void refreshTree();
}
domSel.onchange = () => void reswitch();
beSel.onchange = () => void reswitch();

// Динамика: панель потянули — шлём размер в бекенд, canvas под ответ.
// Троттлим (не чаще раза в 300мс): ресайз окна/swapchain — тяжёлый.
let rzT = 0;
let rzQueued: [number, number] | null = null;
async function doResize(w: number, h: number): Promise<void> {
  const [bw, bh] = await window.eng.resize(w, h);
  cv.width = bw;
  cv.height = bh;
  stat.textContent = `viewport: ${bw}x${bh}`;
}
new ResizeObserver(() => {
  const r = (document.getElementById('stage') as HTMLElement).getBoundingClientRect();
  const w = Math.floor(r.width), h = Math.floor(r.height);
  if (w < 64 || h < 64 || (w === cv.width && h === cv.height)) return;
  const now = performance.now();
  if (now - rzT > 300) {
    rzT = now;
    void doResize(w, h);
  } else {
    rzQueued = [w, h];
    setTimeout(() => {
      if (rzQueued) {
        const [qw, qh] = rzQueued;
        rzQueued = null;
        rzT = performance.now();
        void doResize(qw, qh);
      }
    }, 320);
  }
}).observe(document.getElementById('stage') as HTMLElement);

void refreshTree();
void loop();
