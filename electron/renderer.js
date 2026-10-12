// Renderer: рисует то, что дал main. Состояния сцены тут нет.
const cv = document.getElementById('cv');
const ctx = cv.getContext('2d');
const tree = document.getElementById('tree');
const stat = document.getElementById('stat');
const fpsEl = document.getElementById('fps');
const playBtn = document.getElementById('play');
const domSel = document.getElementById('dom');
const beSel = document.getElementById('be');
let playing = false;
let lastT = performance.now(), nFrames = 0;

async function refreshTree() {
  try {
    const sc = JSON.parse(await window.eng.scene());
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
    stat.textContent = 'scene err: ' + err;
  }
}

async function loop() {
  const buf = await window.eng.frame();
  // Buffer -> ImageData (RGBA, верх первый — как отдаёт бекенд).
  const u8 = new Uint8ClampedArray(buf.buffer, buf.byteOffset, buf.byteLength);
  ctx.putImageData(new ImageData(u8, 1280, 720), 0, 0);
  nFrames++;
  const now = performance.now();
  if (now - lastT > 1000) {
    const n = await window.eng.frames();
    fpsEl.textContent = `fps ${Math.round(nFrames * 1000 / (now - lastT))} / eng ${n}`;
    nFrames = 0;
    lastT = now;
  }
  requestAnimationFrame(loop);
}

playBtn.onclick = async () => {
  playing = !playing;
  await window.eng.play(playing);
  playBtn.textContent = playing ? 'Stop' : 'Play';
  playBtn.classList.toggle('on', playing);
};

async function reswitch() {
  const ok = await window.eng.switch(domSel.value, beSel.value);
  stat.textContent = ok ? `core: ${domSel.value} / ${beSel.value}` : 'NO CORE for domain x backend';
  refreshTree();
}
domSel.onchange = reswitch;
beSel.onchange = reswitch;

refreshTree();
loop();
