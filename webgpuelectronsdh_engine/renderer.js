// SDF raymarcher frontend: WebGPU init + orbit camera + uniforms.
// WGSL (sdf.wgsl) инлайнится строкой — без сборщиков, читается как есть.
// Все ошибки — текстом на страницу (немой чёрный экран запрещён).
window.addEventListener("error", (e) => {
  document.body.innerHTML = "<pre style='color:#f88'>JS: " + (e.message || e.error) + "</pre>";
});
window.addEventListener("unhandledrejection", (e) => {
  const r = e.reason || "unknown";
  document.body.innerHTML = "<pre style='color:#f88'>PROMISE: " + (r.message || r) + "</pre>";
});
const SDF_WGSL = /* wgsl */`
// Канон SDF-сцены. Один источник: сюда правим, renderer.js-прототип сверяется с ним.
// UBO 64Б = SdfUBO в csrc/sdf_ubo.h (std140: vec3+f32 = 16Б x4).
// resX/resY вместо textureDimensions(dummy) — баг прототипа закрыт здесь.
struct UBO {
  camPos: vec3f,
  time: f32,
  camTarget: vec3f,
  resX: f32,
  sunDir: vec3f,
  maxSteps: f32,
  resY: f32,
  pad0: f32,
  pad1: f32,
  pad2: f32,
};

@group(0) @binding(0) var<uniform> u: UBO;
@group(0) @binding(1) var dummyTex: texture_2d<f32>; // заглушка биндинга, в математике не участвует

fn sdSphere(p: vec3f, r: f32) -> f32 { return length(p) - r; }
fn sdBoxF(p: vec3f, b: vec3f) -> f32 {
  let d = abs(p) - b;
  return length(max(d, vec3f(0.0))) + min(max(d.x, max(d.y, d.z)), 0.0);
}
fn sdPlane(p: vec3f, h: f32) -> f32 { return p.y - h; }
fn opS(a: f32, b: f32) -> f32 { return max(a, -b); }
fn smin(a: f32, b: f32, k: f32) -> f32 {
  let h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}

fn map(p: vec3f) -> vec2f {
  var m = vec2f(sdPlane(p, 0.0), 0.0);
  let s1 = sdSphere(p - vec3f(-1.2, 1.0, 0.0), 1.0);
  let s2 = sdSphere(p - vec3f(1.2, 0.8, 0.5), 0.7);
  let blob = smin(s1, s2, 0.6);
  if (blob < m.x) { m = vec2f(blob, 1.0); }
  let bx = sdBoxF(p - vec3f(0.0, 0.6, -2.0), vec3f(0.8, 0.6, 0.8));
  if (bx < m.x) { m = vec2f(bx, 2.0); }
  let hole = sdSphere(p - vec3f(0.0, 1.4, 1.8), 0.5);
  m.x = opS(m.x, hole);
  return m;
}

fn calcNormal(p: vec3f) -> vec3f {
  let e = vec2f(0.0015, -0.0015);
  return normalize(
    e.xyy * map(p + e.xyy).x + e.yyx * map(p + e.yyx).x +
    e.yxy * map(p + e.yxy).x + e.xxx * map(p + e.xxx).x);
}

fn softShadow(ro: vec3f, rd: vec3f, mint: f32, maxt: f32, k: f32) -> f32 {
  var res = 1.0;
  var t = mint;
  for (var i = 0; i < 24; i++) {
    let h = map(ro + rd * t).x;
    if (h < 0.001) { return 0.0; }
    res = min(res, k * h / t);
    t += clamp(h, 0.01, 0.5);
    if (t > maxt) { break; }
  }
  return clamp(res, 0.0, 1.0);
}

fn sky(rd: vec3f, sunDir: vec3f) -> vec3f {
  let sunAmt = max(dot(rd, sunDir), 0.0);
  let sk = mix(vec3f(0.30, 0.45, 0.65), vec3f(0.05, 0.10, 0.22), pow(clamp(rd.y, 0.0, 1.0), 0.6));
  let sun = vec3f(1.25, 1.21, 1.12) * (smoothstep(0.9993, 0.9997, sunAmt) * 4.0 + pow(sunAmt, 350.0) * 0.5);
  return mix(sk, sk * 0.35, clamp(-rd.y * 4.0, 0.0, 1.0)) + sun;
}

@vertex
fn vs(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
  let v = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
  return vec4f(v * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn fs(@builtin(position) frag: vec4f) -> @location(0) vec4f {
  let res = vec2f(u.resX, u.resY); // фикс: разрешение из UBO, не из dummy 1x1
  // WebGPU: frag.y растёт ВНИЗ (origin top-left), а сцена в y-up — флипаем.
  // Без этого небо под ногами, земля над головой.
  let uv = vec2f((frag.x - 0.5 * res.x) / res.y, -((frag.y - 0.5 * res.y) / res.y));
  let fw = normalize(u.camTarget - u.camPos);
  let rt = normalize(cross(fw, vec3f(0.0, 1.0, 0.0)));
  let up = cross(rt, fw);
  let rd = normalize(uv.x * rt + uv.y * up + 1.6 * fw);
  // dummyTex белая 1x1 (*1.0): держит binding 1 в auto-layout, на картинку не влияет.
  // Без этого auto-layout выкидывает неиспользуемый биндинг и createBindGroup кидает.
  let dummyKeep = textureLoad(dummyTex, vec2u(0u, 0u), 0).x;

  var t = 0.0;
  var m = vec2f(-1.0, -1.0);
  let maxT = 60.0;
  for (var i = 0; i < 100; i++) {
    if (f32(i) >= u.maxSteps) { break; }
    let h = map(u.camPos + rd * t);
    if (h.x < max(0.0015 * t, 0.0005)) { m = vec2f(t, h.y); break; }
    t += h.x * 0.8; // 0.8: smin ломает bound, полный шаг проскакивает
    if (t > maxT) { break; }
  }

  if (m.x < 0.0) {
    return vec4f(sky(rd, u.sunDir) * dummyKeep, 1.0);
  }
  let pos = u.camPos + rd * m.x;
  let n = calcNormal(pos);
  let sunDir = normalize(u.sunDir);
  let ndl = max(dot(n, sunDir), 0.0);
  let sh = softShadow(pos + n * 0.04, sunDir, 0.05, 12.0, 8.0); // 0.04: bias против полос акне на скользящих углах
  let base = select(select(vec3f(0.55, 0.60, 0.45), vec3f(0.75, 0.30, 0.25), m.y > 0.5),
                    vec3f(0.35, 0.45, 0.60), m.y > 1.5);
  let amb = mix(vec3f(0.27, 0.24, 0.21), vec3f(0.54, 0.60, 0.69), n.y * 0.5 + 0.5);
  let col = base * (amb + vec3f(1.25, 1.21, 1.12) * ndl * sh);
  let fog = 1.0 - exp(-0.0006 * m.x * m.x);
  return vec4f(mix(col, sky(rd, u.sunDir), fog) * dummyKeep, 1.0);
}
`;

async function main() {
  const canvas = document.getElementById("view");
  if (!(navigator.gpu)) {
    document.body.innerHTML = "<p style='color:#fff'>WebGPU not available (нужен Chrome/Edge 113+ или Electron 28+)</p>";
    return;
  }
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) {
    document.body.innerHTML = "<p style='color:#fff'>WebGPU adapter = null: браузер API отдал, а GPU нет. Открой chrome://gpu (ищи WebGPU), включи chrome://flags/#enable-unsafe-webgpu или запусти с --enable-unsafe-webgpu</p>";
    return;
  }
  const device = await adapter.requestDevice();
  const ctx = canvas.getContext("webgpu");
  const format = navigator.gpu.getPreferredCanvasFormat();
  ctx.configure({ device, format, alphaMode: "opaque" });

  const mod = device.createShaderModule({ code: SDF_WGSL });
  const info = await mod.getCompilationInfo();
  const errors = info.messages.filter((m) => m.type === "error");
  if (errors.length) {
    document.body.innerHTML = "<pre style='color:#f88'>" + errors.map((e) => e.message).join("\n") + "</pre>";
    return;
  }

  const ubo = device.createBuffer({ size: 64, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
  const dummy = device.createTexture({
    size: [1, 1], format: "rgba8unorm",
    usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
  });
  device.queue.writeTexture({ texture: dummy },
    new Uint8Array([255, 255, 255, 255]), { bytesPerRow: 4 }, [1, 1]);

  const pipe = device.createRenderPipeline({
    layout: "auto",
    vertex: { module: mod, entryPoint: "vs" },
    fragment: { module: mod, entryPoint: "fs", targets: [{ format }] },
  });
  const bind = device.createBindGroup({
    layout: pipe.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: ubo } },
      { binding: 1, resource: dummy.createView() },
    ],
  });

  // orbit camera
  let yaw = -0.6, pitch = 0.25, dist = 7.0, tx = 0.0, ty = 1.0;
  let dragging = false, lx = 0, ly = 0;
  canvas.addEventListener("mousedown", (e) => { dragging = true; lx = e.clientX; ly = e.clientY; });
  window.addEventListener("mouseup", () => { dragging = false; });
  window.addEventListener("mousemove", (e) => {
    if (!dragging) return;
    yaw -= (e.clientX - lx) * 0.005;
    // Низ -0.12: ниже камера уходит ПОД бесконечную плоскость (внутрь геометрии)
    // и весь экран — мусор поля. Это не баг рендера, это стена by design (v0.1 orbit).
    pitch = Math.min(1.45, Math.max(-0.12, pitch + (e.clientY - ly) * 0.005));
    lx = e.clientX; ly = e.clientY;
  });
  canvas.addEventListener("wheel", (e) => {
    dist = Math.min(30, Math.max(2.5, dist * (1 + Math.sign(e.deltaY) * 0.1)));
  }, { passive: true });

  const data = new Float32Array(16);
  let t0 = performance.now();
  // WASD здесь НЕТ by design (v0.1: только orbit; free-fly — v0.2).
  // preventDefault чтобы браузер не уводил страницу (пробел-скролл и т.п.).
  window.addEventListener("keydown", (e) => {
    if (["Space", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight"].includes(e.code)) e.preventDefault();
  });
  let frames = 0, fpsT = performance.now();
  function frame() {
    const w = canvas.clientWidth, h = canvas.clientHeight;
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    const t = (performance.now() - t0) / 1000;
    const cp = [
      tx + dist * Math.cos(pitch) * Math.cos(yaw),
      ty + dist * Math.sin(pitch),
      0 + dist * Math.cos(pitch) * Math.sin(yaw),
    ];
    // UBO 64Б = SdfUBO (csrc/sdf_ubo.h) = shaders/sdf.wgsl: camPos+time | camTarget+resX | sunDir+maxSteps | resY+pad
    data[0] = cp[0]; data[1] = cp[1]; data[2] = cp[2]; data[3] = t;
    data[4] = tx; data[5] = ty; data[6] = 0; data[7] = w;
    const sa = t * 0.05;
    data[8] = Math.cos(sa); data[9] = 0.55; data[10] = Math.sin(sa); data[11] = 100;
    data[12] = h; data[13] = 0; data[14] = 0; data[15] = 0;
    device.queue.writeBuffer(ubo, 0, data); // writeBuffer = когерентность за нас; на нативе с map будет flush (см. csrc/sdf_gpu.h)

    const enc = device.createCommandEncoder();
    const pass = enc.beginRenderPass({
      colorAttachments: [{
        view: ctx.getCurrentTexture().createView(),
        loadOp: "clear", storeOp: "store",
        clearValue: [0, 0, 0, 1],
      }],
    });
    pass.setPipeline(pipe);
    pass.setBindGroup(0, bind);
    pass.draw(3);
    pass.end();
    device.queue.submit([enc.finish()]);
    if (++frames % 60 === 0) {
      const now = performance.now();
      document.title = "SDF engine — " + Math.round(frames / ((now - fpsT) / 1000)) + " fps";
      frames = 0; fpsT = now;
    }
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}
main();
