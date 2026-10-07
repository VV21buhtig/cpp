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

fn calcNormal(p: vec3f, t: f32) -> vec3f {
  // Эпсилон растёт с дистанцией (см. renderer.js): иначе шум нормали у горизонта.
  let ee = max(0.002 * t, 0.0008);
  let e = vec2f(ee, -ee);
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
    // Relaxed step ×0.8 как в главном марше (smin ломает bound).
    t += clamp(h, 0.01, 0.5) * 0.8;
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
  let uv = vec2f((frag.x - 0.5 * res.x) / res.y, -((frag.y - 0.5 * res.y) / res.y));
  let fw = normalize(u.camTarget - u.camPos);
  let rt = normalize(cross(fw, vec3f(0.0, 1.0, 0.0)));
  let up = cross(rt, fw);
  let rd = normalize(uv.x * rt + uv.y * up + 1.6 * fw);
  // dummyTex белая 1x1 (*1.0): держит binding 1 в auto-layout (см. renderer.js).
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
  let n = calcNormal(pos, m.x);
  let sunDir = normalize(u.sunDir);
  let ndl = max(dot(n, sunDir), 0.0);
  let sh = softShadow(pos + n * 0.04, sunDir, 0.05, 12.0, 8.0); // 0.04: bias против полос акне
  let base = select(select(vec3f(0.55, 0.60, 0.45), vec3f(0.75, 0.30, 0.25), m.y > 0.5),
                    vec3f(0.35, 0.45, 0.60), m.y > 1.5);
  let amb = mix(vec3f(0.27, 0.24, 0.21), vec3f(0.54, 0.60, 0.69), n.y * 0.5 + 0.5);
  let col = base * (amb + vec3f(1.25, 1.21, 1.12) * ndl * sh);
  let fog = 1.0 - exp(-0.0006 * m.x * m.x);
  return vec4f(mix(col, sky(rd, u.sunDir), fog) * dummyKeep, 1.0);
}
