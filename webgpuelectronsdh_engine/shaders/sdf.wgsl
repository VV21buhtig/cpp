// Шаг 2: +ПОЛ. Куб и шар стоят на плоскости (не парят, не утоплены).
// Шаг 3: +ТЕНИ (второй марш к солнцу, bias от акне).
// Шаг 4: +НЕБО (градиент + диск) + ТУМАН (дальний пол тает в небо, шва нет).
// Лестница: 0) куб -> 1) +шар -> 2) +пол -> 3) +тени -> 4) +небо/туман.
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

fn sdBoxF(p: vec3f, b: vec3f) -> f32 {
  let d = abs(p) - b;
  return length(max(d, vec3f(0.0))) + min(max(d.x, max(d.y, d.z)), 0.0);
}
fn sdSphere(p: vec3f, r: f32) -> f32 { return length(p) - r; }

fn map(p: vec3f) -> vec2f {
  var m = vec2f(p.y, 2.0); // пол: id 2
  let bx = sdBoxF(p - vec3f(0.0, 0.8, 0.0), vec3f(0.8)); // стоит на полу
  if (bx < m.x) { m = vec2f(bx, 0.0); }
  let s = sdSphere(p - vec3f(1.8, 0.7, 0.0), 0.7); // стоит на полу
  if (s < m.x) { m = vec2f(s, 1.0); }
  return m;
}

fn calcNormal(p: vec3f, t: f32) -> vec3f {
  let ee = max(0.001 * t, 0.0005);
  let e = vec2f(ee, -ee);
  return normalize(
    e.xyy * map(p + e.xyy).x + e.yyx * map(p + e.yyx).x +
    e.yxy * map(p + e.yxy).x + e.xxx * map(p + e.xxx).x);
}

fn softShadow(ro: vec3f, rd: vec3f) -> f32 {
  var res = 1.0;
  var t = 0.05;
  for (var i = 0; i < 24; i++) {
    let h = map(ro + rd * t).x;
    if (h < 0.001) { return 0.0; }
    res = min(res, 8.0 * h / t);
    t += clamp(h, 0.01, 0.5);
    if (t > 12.0) { break; }
  }
  return clamp(res, 0.0, 1.0);
}

fn hash12(p: vec2f) -> f32 {
  // murmur из K (wc_noise_common): ровнее fract-sin. Обёртка в [0,1024):
  // u32() от отрицательного в WGSL недетерминирован, заодно тайлится.
  let q = p - floor(p / 1024.0) * 1024.0;
  var h = 0x5bd1e995u;
  h ^= u32(q.x) * 0x27d4eb2du;
  h ^= u32(q.y) * 0x9e3779b1u;
  h ^= (u32(q.x) + u32(q.y)) * 0x165667b1u;
  h *= 0x85ebca6bu;
  h ^= h >> 16u;
  return f32(h & 1023u) / 1024.0;
}
fn vnoise(p: vec2f) -> f32 {
  let i = floor(p);
  let u = fract(p) * fract(p) * (3.0 - 2.0 * fract(p));
  return mix(mix(hash12(i), hash12(i + vec2f(1.0, 0.0)), u.x),
             mix(hash12(i + vec2f(0.0, 1.0)), hash12(i + vec2f(1.0, 1.0)), u.x), u.y);
}
fn sky(rd: vec3f, sunDir: vec3f) -> vec3f {
  // Вид из K: цвет неба живёт от высоты солнца — день/закат/ночь.
  let sunAmt = max(dot(rd, sunDir), 0.0);
  let dayF = clamp(sunDir.y, -1.0, 1.0);
  let sunset = pow(clamp(1.0 - abs(dayF), 0.0, 1.0), 3.0);
  let night = smoothstep(0.02, -0.12, dayF);
  let zen = mix(vec3f(0.05, 0.10, 0.22), vec3f(0.16, 0.32, 0.58), clamp(dayF * 1.4, 0.0, 1.0));
  var hor = mix(vec3f(0.30, 0.45, 0.65), vec3f(0.55, 0.60, 0.68), clamp(dayF, 0.0, 1.0));
  hor = mix(hor, vec3f(1.0, 0.45, 0.20), sunset * clamp(rd.y * 3.0 + 0.6, 0.0, 1.0));
  var sk = mix(hor, zen, pow(clamp(rd.y, 0.0, 1.0), 0.6));
  sk *= (1.0 - night * 0.85);
  let sunCol = mix(vec3f(1.0, 0.45, 0.20), vec3f(1.25, 1.21, 1.12), clamp(dayF * 2.0, 0.0, 1.0));
  let disk = smoothstep(0.9993, 0.9997, sunAmt) * 4.0;
  let hg = (1.0 - 0.36) / (12.56637 * pow(max(1.0 + 0.36 - 1.2 * sunAmt, 1e-4), 1.5)); // Henyey-Greenstein g=0.6 из K
  let halo = pow(sunAmt, 350.0) * 0.5 + hg * 0.25 * (1.0 - night);
  // Дешёвые облака вместо волюметрики K: 2D fbm-купол, тёмные сверху,
  // рыжая подсветка снизу у солнца. Только небо (rd.y>0), 4 октавы.
  var cloudCov = 0.0;
  var cloudCol = vec3f(0.0);
  if (rd.y > 0.015) {
    let cuv = rd.xz / (rd.y + 0.08) * 1.2 + vec2f(u.time * 0.008, u.time * 0.003);
    var f = 0.0;
    var a = 0.5;
    var pp = cuv;
    for (var i = 0; i < 4; i++) {
      f += a * vnoise(pp);
      pp = pp * 2.03 + vec2f(1.7, 9.2);
      a *= 0.5;
    }
    let edge = smoothstep(0.45, 0.60, f) - smoothstep(0.52, 0.72, f);
    cloudCov = smoothstep(0.52, 0.72, f) * smoothstep(0.015, 0.12, rd.y);
    let dark = mix(vec3f(0.10, 0.09, 0.12), vec3f(0.02, 0.02, 0.04), night);
    let lit = sunCol * 1.3 * clamp(edge * 1.5 + pow(sunAmt, 3.0), 0.0, 1.0) * (1.0 - night);
    cloudCol = mix(dark, lit + dark, clamp(edge * 2.0 + pow(sunAmt, 3.0), 0.0, 1.0));
  }
  sk = mix(sk, cloudCol, cloudCov);
  return mix(sk, sk * 0.35, clamp(-rd.y * 4.0, 0.0, 1.0)) + sunCol * (disk + halo) * (1.0 - night) * (1.0 - cloudCov);
}

fn sdfAO(pos: vec3f, n: vec3f) -> f32 {
  // Дешёвый SSAO из K для реймарша: 5 проб вдоль нормали (iq).
  var occ = 0.0;
  var sca = 1.0;
  for (var i = 0; i < 5; i++) {
    let h = 0.01 + 0.12 * f32(i) / 4.0;
    occ += (h - map(pos + n * h).x) * sca;
    sca *= 0.95;
  }
  return clamp(1.0 - 3.0 * occ, 0.0, 1.0);
}

@vertex
fn vs(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
  let v = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
  return vec4f(v * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn fs(@builtin(position) frag: vec4f) -> @location(0) vec4f {
  let res = vec2f(u.resX, u.resY);
  let uv = vec2f((frag.x - 0.5 * res.x) / res.y, -((frag.y - 0.5 * res.y) / res.y));
  let fw = normalize(u.camTarget - u.camPos);
  let rt = normalize(cross(fw, vec3f(0.0, 1.0, 0.0)));
  let up = cross(rt, fw);
  let rd = normalize(uv.x * rt + uv.y * up + 1.6 * fw);

  var t = 0.0;
  var m = vec2f(-1.0, -1.0);
  for (var i = 0; i < 100; i++) {
    if (f32(i) >= u.maxSteps) { break; }
    let h = map(u.camPos + rd * t);
    if (h.x < max(0.001 * t, 0.0002)) { m = vec2f(t, h.y); break; }
    t += h.x;
    if (t > 60.0) { break; }
  }
  // Аналитический пол: выигрывает ближний, туман на дальнем даст небо (шаг 4).
  let tP = -u.camPos.y / rd.y;
  if (rd.y < -0.0005 && tP > 0.0 && (m.x < 0.0 || tP < m.x)) { m = vec2f(tP, 2.0); }

  if (m.x < 0.0) {
    return vec4f(sky(rd, normalize(u.sunDir)), 1.0);
  }
  let pos = u.camPos + rd * m.x;
  let n = calcNormal(pos, m.x);
  let sunDir = normalize(u.sunDir);
  let sh = softShadow(pos + n * 0.02, sunDir); // bias: без него полосы акне
  // Живой свет из K: тёплый низко, белый высоко, ночью гаснет.
  let dayF = clamp(sunDir.y, -1.0, 1.0);
  let dayL = clamp(dayF * 2.0 + 0.25, 0.04, 1.0);
  let lightCol = mix(vec3f(1.0, 0.50, 0.25), vec3f(1.25, 1.21, 1.12), clamp(dayF * 2.0, 0.0, 1.0));
  let skyAmb = sky(vec3f(0.0, 1.0, 0.0), sunDir);
  var base = vec3f(0.60, 0.65, 0.75); // куб
  if (m.y > 0.5 && m.y < 1.5) { base = vec3f(0.75, 0.45, 0.35); } // шар
  else if (m.y > 1.5) { base = vec3f(0.55, 0.60, 0.45); } // пол
  let amb = mix(vec3f(0.27, 0.24, 0.21), skyAmb, n.y * 0.5 + 0.5) * (0.35 + 0.65 * dayL) * sdfAO(pos, n);
  let ndl = max(dot(n, sunDir), 0.0);
  var col = base * (amb + lightCol * ndl * sh * dayL);
  // Контровой рим из K: край против солнца подсвечен — силуэты не плоские.
  let sunAmt = max(dot(rd, sunDir), 0.0);
  let sunset = pow(clamp(1.0 - abs(clamp(sunDir.y, -1.0, 1.0)), 0.0, 1.0), 3.0);
  let rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
  col += base * lightCol * rim * (0.15 + 0.85 * sunset);
  // Воздушная перспектива: туман греется к солнцу (дальняк в рыжее).
  let fog = 1.0 - exp(-0.0006 * m.x * m.x);
  var fogCol = sky(rd, sunDir);
  fogCol = mix(fogCol, vec3f(1.0, 0.45, 0.20) * (0.4 + 0.6 * dayL), pow(sunAmt, 3.0) * 0.55 * sunset);
  return vec4f(mix(col, fogCol, fog), 1.0);
}
