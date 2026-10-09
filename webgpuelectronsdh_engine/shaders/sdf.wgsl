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
fn fbm4(p: vec2f) -> f32 {
  var f = 0.0;
  var a = 0.5;
  var pp = p;
  for (var i = 0; i < 4; i++) {
    f += a * vnoise(pp);
    pp = pp * 2.03 + vec2f(1.7, 9.2);
    a *= 0.5;
  }
  return f;
}
fn hash33(p: vec3f) -> vec3f {
  var q = fract(p * vec3f(0.1031, 0.1030, 0.0973));
  q += dot(q, q.yxz + vec3f(33.33));
  return fract((q.xxy + q.yxx) * q.zyx);
}
fn skyGrad(rd: vec3f, sunDir: vec3f) -> vec3f {
  // Градиент без облаков/звёзд: дешёвый фон для тумана.
  let dayF = clamp(sunDir.y, -1.0, 1.0);
  let sunset = pow(clamp(1.0 - abs(dayF), 0.0, 1.0), 3.0);
  let night = smoothstep(0.02, -0.12, dayF);
  let zen = mix(vec3f(0.05, 0.10, 0.22), vec3f(0.16, 0.32, 0.58), clamp(dayF * 1.4, 0.0, 1.0));
  var hor = mix(vec3f(0.30, 0.45, 0.65), vec3f(0.55, 0.60, 0.68), clamp(dayF, 0.0, 1.0));
  hor = mix(hor, vec3f(1.0, 0.45, 0.20), sunset * clamp(rd.y * 3.0 + 0.6, 0.0, 1.0));
  var sk = mix(hor, zen, pow(clamp(rd.y, 0.0, 1.0), 0.6));
  sk *= (1.0 - night * 0.85);
  return mix(sk, sk * 0.35, clamp(-rd.y * 4.0, 0.0, 1.0));
}

fn sunTerms(rd: vec3f, sunDir: vec3f, sunCol: vec3f, night: f32) -> vec3f {
  let sunAmt = max(dot(rd, sunDir), 0.0);
  let disk = smoothstep(0.9993, 0.9997, sunAmt) * 4.0;
  let hg = (1.0 - 0.36) / (12.56637 * pow(max(1.0 + 0.36 - 1.2 * sunAmt, 1e-4), 1.5));
  let halo = pow(sunAmt, 350.0) * 0.5 + hg * 0.25 * (1.0 - night);
  return sunCol * (disk + halo) * (1.0 - night);
}

fn ign(p: vec2f) -> f32 {
  return fract(52.9829189 * fract(dot(p, vec2f(0.06711056, 0.00583715))));
}

fn hg_phase(c: f32, g: f32) -> f32 {
  let g2 = g * g;
  return (1.0 - g2) / (12.56637 * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

fn slabClouds(ro: vec3f, rd: vec3f, sunCol: vec3f, night: f32) -> vec4f {
  // Их wc_clouds целиком: сферичность опущена (мир плоский), 56 проб -> 8 (Vega),
  // 3D-шумы -> наш warp-fbm (текстур нет), LUT неба -> наши sky-функции.
  // Остальное 1:1 — марш к солнцу 4, Wrenninge x4, powder, horizon-fade.
  var t0 = (12.0 - ro.y) / rd.y;
  var t1 = (20.0 - ro.y) / rd.y;
  if (t0 > t1) { let tt = t0; t0 = t1; t1 = tt; }
  t0 = max(t0, 0.0);
  t1 = min(t1, t0 + 120.0);
  if (t1 <= t0 || t0 > 400.0) { return vec4f(0.0); }
  let sunDir = normalize(u.sunDir);
  let moonUp = 1.0 - night;
  let L = select(-sunDir, sunDir, moonUp > 0.5);
  let light_col = select(vec3f(0.10, 0.11, 0.14), sunCol, moonUp > 0.5);
  let cos_t = dot(rd, L);
  let jitter = fract(ign(rd.xy * 913.0) * 7.0 + u.time * 0.13);
  let span = t1 - t0;
  let dt = span / 8.0;
  var t = t0 + dt * jitter;
  var T = 1.0;
  var scat = vec3f(0.0);
  let sigma = 0.30;
  let wind = vec2f(u.time * 0.020, u.time * 0.007);
  let amb_top = skyGrad(vec3f(0.0, 1.0, 0.0), sunDir) * 1.15;
  let amb_bot = vec3f(0.16, 0.15, 0.13) * 0.7 + amb_top * 0.15;
  var tsum = 0.0;
  var wsum = 0.0;
  for (var i = 0; i < 8; i++) {
    let p = ro + rd * t;
    let h = (p.y - 12.0) / 8.0;
    if (h >= 0.0 && h <= 1.0) {
      let grad = smoothstep(0.0, 0.08, h) * (1.0 - smoothstep(0.55, 1.0, h));
      var d = 0.0;
      if (grad > 0.0) {
        let q = p.xz * 0.05 + wind;
        let wv = vec2f(vnoise(q * 2.1), vnoise(q * 2.1 + vec2f(7.3, 3.1))) - 0.5;
        d = smoothstep(0.52, 0.72, fbm4(q + 0.45 * wv)) * grad;
      }
      if (d > 0.002) {
        // марш к свету: шаги растут, как у них ls*(j*0.6+1)
        var od = 0.0;
        var lp = p;
        for (var j = 0; j < 4; j++) {
          let stepl = 0.72 * (f32(j) * 0.6 + 1.0);
          lp += L * stepl;
          let lh = (lp.y - 12.0) / 8.0;
          if (lh > 1.0) { break; }
          if (lh >= 0.0) {
            let lq = lp.xz * 0.05 + wind;
            od += fbm4(lq) * stepl;
          }
        }
        // Wrenninge x4: вперёд 0.75c + назад -0.25c, бленд 0.3
        var sun = vec3f(0.0);
        var a = 1.0;
        var b = 1.0;
        var c = 1.0;
        for (var o = 0; o < 4; o++) {
          let ph = mix(hg_phase(cos_t, 0.75 * c), hg_phase(cos_t, -0.25 * c), 0.3);
          sun += vec3f(a * exp(-od * sigma * b) * ph);
          a *= 0.55;
          b *= 0.35;
          c *= 0.5;
        }
        let powder = 1.0 - exp(-d * sigma * 120.0);
        sun *= mix(1.0, powder * 2.0, 0.6);
        let amb = mix(amb_bot, amb_top, h * h) * (0.4 + 0.6 * h);
        let S = (light_col * sun + amb) * sigma * d;
        let st = exp(-d * sigma * dt);
        scat += T * (S - S * st) / max(sigma * d, 1e-4);
        tsum += t * T * (1.0 - st);
        wsum += T * (1.0 - st);
        T *= st;
        if (T < 0.01) { break; }
      }
    }
    t += dt;
  }
  // Воздушная перспектива + горизонт-фэйд как у них (душит полосы сбоку).
  let dist = select(t1, tsum / max(wsum, 1e-4), wsum > 0.0);
  let fade = exp(-dist / 80.0);
  let sky_col = skyGrad(rd, sunDir);
  scat = mix(sky_col * (1.0 - T), scat, fade);
  let horizon = smoothstep(0.0, 0.06, rd.y + 0.02);
  scat *= horizon;
  T = mix(1.0, T, horizon);
  return vec4f(scat, 1.0 - T);
}

fn sky(rd: vec3f, sunDir: vec3f) -> vec3f {
  let dayF = clamp(sunDir.y, -1.0, 1.0);
  let night = smoothstep(0.02, -0.12, dayF);
  let sunCol = mix(vec3f(1.0, 0.45, 0.20), vec3f(1.25, 1.21, 1.12), clamp(dayF * 2.0, 0.0, 1.0));
  var sk = skyGrad(rd, sunDir);
  let sl = slabClouds(u.camPos, rd, sunCol, night);
  sk = mix(sk, sl.rgb, sl.a);
  // Звёзды и луна из K (упрощены: 1 слой сетки, диск без кратеров).
  // Луна opposite солнца — видна ночью. Всё гаснет днём и за облаками.
  let mdir = -sunDir;
  let mdot = dot(rd, mdir);
  var star = vec3f(0.0);
  if (night > 0.01 && rd.y > 0.0) {
    let p = rd * 170.0;
    let cell = floor(p);
    let f = fract(p) - 0.5;
    let h = hash33(cell);
    let present = step(0.972, h.x);
    let dd = length(f - (hash33(cell * 1.7 + vec3f(3.0)) - 0.5) * 0.5);
    let tw = 0.75 + 0.25 * sin(u.time * (2.0 + h.z * 5.0) + h.y * 40.0);
    let b = present * smoothstep(0.32, 0.0, dd) * 2.8 * (0.15 + h.y * h.y * h.y) * tw;
    star = mix(vec3f(1.0, 0.8, 0.6), vec3f(0.72, 0.84, 1.0), h.z) * b;
  }
  let mr = sqrt(max(0.0, 2.0 * (1.0 - mdot)));
  let mdisk = smoothstep(0.0155, 0.0143, mr);
  let mlum = (0.75 + 0.25 * sqrt(max(0.0, 1.0 - (mr / 0.0155) * (mr / 0.0155))));
  let moon = vec3f(0.95, 0.96, 1.0) * mlum * mdisk * 0.9;
  sk += (star + moon) * night * (1.0 - sl.a);
  return sk + sunTerms(rd, sunDir, sunCol, night) * (1.0 - sl.a);
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
  // Тень облака — полем слоя 1 (тем же, что небо): соответствие пятно-облако.
  // Рычаг st=(H-y)/sunY при низком солнце взрывается — гейт 0.05..0.35.
  let shFade = smoothstep(0.05, 0.35, sunDir.y);
  if (shFade > 0.0) {
    let st = (15.0 - pos.y) / max(sunDir.y, 0.001);
    var shf = 0.0;
    if (st > 0.0) {
      let sq = (pos.xz + sunDir.xz * st) * 0.05 + vec2f(u.time * 0.020, u.time * 0.007);
      let swv = vec2f(vnoise(sq * 2.1), vnoise(sq * 2.1 + vec2f(7.3, 3.1))) - 0.5;
      shf = fbm4(sq + 0.45 * swv); // тот же warp, что небо: пятно = облако
    }
    col *= mix(1.0, mix(1.0, 0.25, smoothstep(0.45, 0.75, shf)), shFade);
  }
  // Воздушная перспектива: туман греется к солнцу (дальняк в рыжее).
  // Высотный туман из K (fog_od): плотность падает с высотой, аналитика.
  let fogDen = 0.0006 * exp(-max(pos.y, 0.0) / 6.0);
  let fog = 1.0 - exp(-fogDen * m.x * m.x);
  var fogCol = skyGrad(rd, sunDir) + sunTerms(rd, sunDir, lightCol, 1.0 - dayL);
  fogCol = mix(fogCol, vec3f(1.0, 0.45, 0.20) * (0.4 + 0.6 * dayL), pow(sunAmt, 3.0) * 0.55 * sunset);
  return vec4f(mix(col, fogCol, fog), 1.0);
}
