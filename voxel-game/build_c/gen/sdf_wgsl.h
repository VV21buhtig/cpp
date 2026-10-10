#pragma once
// Собрано из shaders/sdf/*.wgsl. Не править руками.
static const char *SDF_WGSL __attribute__((unused)) = R"SDF(
// UBO 64Б = SdfUBO в csrc/sdf_ubo.h. Layout frozen: vec3+f32 x4.
struct UBO {
  camPos: vec3f,
  time: f32,
  camTarget: vec3f,
  resX: f32,
  sunDir: vec3f,
  maxSteps: f32,
  resY: f32,
  mode: f32,
  pad0: f32,
  pad1: f32,
};

@group(0) @binding(0) var<uniform> u: UBO;
// Грейд из settings.cfg (как их gammaU): x=gamma, y=exposure.
@group(0) @binding(2) var<uniform> grade: vec4f;
// Вид: x=fov_scale (1.6), y=тени вкл/выкл.
@group(0) @binding(3) var<uniform> view: vec4f;
// Фрустум главной камеры (дебаг): pos,fwd,right,up. Рисуем только при view.z>0.5.
@group(0) @binding(4) var<uniform> frustum: array<vec4f, 4>;

fn segDist(p: vec3f, a: vec3f, b: vec3f) -> f32 {
  let pa = p - a;
  let ba = b - a;
  let h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-9), 0.0, 1.0);
  return length(pa - ba * h);
}

// Сцена: куб + шар стоят на полу. Зеркало — csrc/sdf_scene.h (править парой).
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

// Воксели лучом: analytic вход в окно стриминга + DDA Аманатидеса-Ву внутри.
// Окно: origin из UBO (pad0/pad1 = мировая клетка texel 0,0), размер 176x64x176.
// Вне окна — воздух. Тороид на заливке (C), в шейдере прямое смещение.
@group(1) @binding(3) var voxTex: texture_3d<u32>;
// Атлас 16x16x7 (их тайлы): nearest, без фильтра-мыла.
@group(1) @binding(4) var tileTex: texture_2d_array<f32>;
@group(1) @binding(5) var tileSmp: sampler;

fn tileUV(pos: vec3f, n: vec3f) -> vec2f {
  // Атлас уже отфлипан при загрузке (vox_tex, как у них): v=0 = низ.
  if (abs(n.y) > 0.5) { return fract(pos.xz); }
  if (abs(n.x) > 0.5) { return vec2f(fract(pos.z), fract(pos.y)); }
  return vec2f(fract(pos.x), fract(pos.y));
}

fn tileLayer(id: u32, n: vec3f) -> i32 {
  if (id == 1u) { // трава: верх/низ/бок
    if (n.y > 0.5) { return 0; }
    if (n.y < -0.5) { return 2; } // снизу — земля, не трава
    return 1;
  }
  if (id == 2u) { return 2; } // земля
  if (id == 4u) { return select(7, 8, abs(n.y) > 0.5); } // бревно: бок/торец (их индексы)
  if (id == 5u) { return 6; } // листва
  return 3; // камень, бедрок, остальное
}
// Теги слотов: какой мировой чанк РЕАЛЬНО залит (121 пара). Сентинел = воздух,
// иначе видны миражи со старого конца карты при отставании заливки.
@group(0) @binding(1) var<uniform> voxTags: array<vec4i, 121>;

struct VoxHit {
  t: f32,
  n: vec3f,
  id: u32,
  steps: f32, // цена: итераций DDA (для heatmap, режим 3)
};

// Сегмент луча в окне: x=tEnter, y=tExit, (-1,-1) мимо.
fn boxSeg(ro: vec3f, rd: vec3f) -> vec2f {
  let omin = vec3f(u.pad0, 0.0, u.pad1);
  let omax = omin + vec3f(176.0, 64.0, 176.0);
  let ax = abs(rd.x);
  let ay = abs(rd.y);
  let az = abs(rd.z);
  let ix = select(1e9, 1.0 / rd.x, ax > 1e-9);
  let iy = select(1e9, 1.0 / rd.y, ay > 1e-9);
  let iz = select(1e9, 1.0 / rd.z, az > 1e-9);
  let t0 = (omin - ro) * vec3f(ix, iy, iz);
  let t1 = (omax - ro) * vec3f(ix, iy, iz);
  let tmin = min(t0, t1);
  let tmax = max(t0, t1);
  let enter = max(tmin.x, max(tmin.y, tmin.z));
  let exit = min(tmax.x, min(tmax.y, tmax.z));
  if (enter > exit || exit < 0.0) { return vec2f(-1.0); }
  return vec2f(max(enter, 0.0), exit);
}

fn voxMarch(ro: vec3f, rd: vec3f, maxT: f32) -> VoxHit {  var h: VoxHit;
  h.t = -1.0;
  h.n = vec3f(0.0);
  h.id = 0u;
  h.steps = 0.0;
  let seg = boxSeg(ro, rd);
  if (seg.x < 0.0) { return h; }
  let tEnd = min(seg.y, maxT);
  var p = vec3i(floor(ro + rd * seg.x));
  let srd = sign(rd);
  let step = vec3i(i32(srd.x), i32(srd.y), i32(srd.z));
  let ax = abs(rd.x);
  let ay = abs(rd.y);
  let az = abs(rd.z);
  let tdx = select(1e9, abs(1.0 / rd.x), ax > 1e-9);
  let tdy = select(1e9, abs(1.0 / rd.y), ay > 1e-9);
  let tdz = select(1e9, abs(1.0 / rd.z), az > 1e-9);
  // Инит — знаком: (p-ro) отрицательно при луче назад, abs давал tm<0
  // и порядок обхода ломался (стоя — стабильно-криво, в движении — гармошка).
  let idx = select(1e9, 1.0 / rd.x, ax > 1e-9);
  let idy = select(1e9, 1.0 / rd.y, ay > 1e-9);
  let idz = select(1e9, 1.0 / rd.z, az > 1e-9);
  let bx = select(select(0.5, 1.0, step.x > 0), 0.0, step.x < 0);
  let by = select(select(0.5, 1.0, step.y > 0), 0.0, step.y < 0);
  let bz = select(select(0.5, 1.0, step.z > 0), 0.0, step.z < 0);
  var tm = (vec3f(p) + vec3f(bx, by, bz) - ro) * vec3f(idx, idy, idz);
  var n = vec3f(0.0);
  var t = seg.x;
  for (var i = 0; i < 320; i++) {
    h.steps += 1.0;
    if (tm.x < tm.y && tm.x < tm.z) {
      p.x += step.x; t = tm.x; tm.x += tdx; n = vec3f(-srd.x, 0.0, 0.0);
    } else if (tm.y < tm.z) {
      p.y += step.y; t = tm.y; tm.y += tdy; n = vec3f(0.0, -srd.y, 0.0);
    } else {
      p.z += step.z; t = tm.z; tm.z += tdz; n = vec3f(0.0, 0.0, -srd.z);
    }
    if (t > tEnd) { break; }
    // Вне окна — воздух (и дальше не вернётся: окно выпуклое, DDA монотонен).
    let ox = i32(u.pad0);
    let oz = i32(u.pad1);
    if (p.x < ox || p.y < 0 || p.z < oz || p.x >= ox + 176 || p.y >= 64 || p.z >= oz + 176) { continue; }
    // Тороид чтением: слот = wrap(чанк), чанк = floor(клетка/16).
    // Совпадает с заливкой при любом origin (та — wrap(чанк) тоже).
    let cx = select(p.x / 16, (p.x - 15) / 16, p.x < 0);
    let cz = select(p.z / 16, (p.z - 15) / 16, p.z < 0);
    let lx = p.x - cx * 16;
    let lz = p.z - cz * 16;
    let rx = cx % 11;
    let rz = cz % 11;
    let sx = select(rx, rx + 11, rx < 0);
    let sz = select(rz, rz + 11, rz < 0);
    // Слот без свежих данных — воздух (луч идёт дальше, а не в мираж).
    let tag = voxTags[sz * 11 + sx].xy;
    if (tag.x != cx || tag.y != cz) { continue; }
    let id = textureLoad(voxTex, vec3i(sx * 16 + lx, p.y, sz * 16 + lz), 0).r;
    if (id != 0u) {
      h.t = t;
      h.n = n;
      h.id = id;
      break;
    }
  }
  return h;
}

// Свет: мягкие тени вторым маршем + SDF-AO вместо SSAO (iq).
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

fn sdfAO(pos: vec3f, n: vec3f) -> f32 {
  var occ = 0.0;
  var sca = 1.0;
  for (var i = 0; i < 5; i++) {
    let h = 0.01 + 0.12 * f32(i) / 4.0;
    occ += (h - map(pos + n * h).x) * sca;
    sca *= 0.95;
  }
  return clamp(1.0 - 3.0 * occ, 0.0, 1.0);
}

// Небо: градиент день/закат/ночь + солнце (HG-гало) + звёзды/луна. Вид из K.
fn hash33(p: vec3f) -> vec3f {
  var q = fract(p * vec3f(0.1031, 0.1030, 0.0973));
  q += dot(q, q.yxz + vec3f(33.33));
  return fract((q.xxy + q.yxx) * q.zyx);
}

// LUT неба из печки (см. shaders/sky/): сэмплятся тут, пекутся в sky_lut.c.
@group(1) @binding(0) var skySunTex: texture_2d<f32>;
@group(1) @binding(1) var skyMoonTex: texture_2d<f32>;
@group(1) @binding(2) var skySmp: sampler;

fn safeacosM(x: f32) -> f32 { return acos(clamp(x, -1.0, 1.0)); }

fn skyLutUv(ray_dir: vec3f, sun_dir: vec3f) -> vec2f {
  let hFrac = clamp((u.camPos.y - 62.0) / 64.0, 0.0, 1.0);
  let vp = vec3f(0.0, 6.360 + 0.0003 + hFrac * 0.002, 0.0);
  let height = length(vp);
  let up = vp / height;
  let horizon_angle = safeacosM(sqrt(height * height - 6.360 * 6.360) / height);
  let altitude_angle = horizon_angle - acos(clamp(dot(ray_dir, up), -1.0, 1.0));
  var azimuth_angle: f32;
  if (abs(altitude_angle) > (0.5 * 3.14159265 - 0.0001)) {
    azimuth_angle = 0.0;
  } else {
    let right = cross(sun_dir, up);
    let forward = cross(up, right);
    let projected = normalize(ray_dir - up * dot(ray_dir, up) + vec3f(1e-6, 0.0, 0.0));
    azimuth_angle = atan2(dot(projected, right), dot(projected, forward)) + 3.14159265;
  }
  let v = 0.5 + 0.5 * sign(altitude_angle) * sqrt(abs(altitude_angle) * 2.0 / 3.14159265);
  return vec2f(azimuth_angle / (2.0 * 3.14159265), v);
}

fn lum3(c: vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }

// Сырая физика из LUT. Уровни нормируются формой (см. sky): точные SUN_I/MOON_I
// их illuminance нам неизвестны, в отношении почти сокращаются.
fn skyPhys(dir: vec3f, sunDir: vec3f, moonDir: vec3f) -> vec3f {
  return textureSampleLevel(skySunTex, skySmp, skyLutUv(dir, sunDir), 0.0).rgb * 3.0
       + textureSampleLevel(skyMoonTex, skySmp, skyLutUv(dir, moonDir), 0.0).rgb * 0.02;
}

fn skyGrad(rd: vec3f, sunDir: vec3f) -> vec3f {
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

fn sky(rd: vec3f, sunDir: vec3f) -> vec3f {
  let dayF = clamp(sunDir.y, -1.0, 1.0);
  let night = smoothstep(0.02, -0.12, dayF);
  let sunCol = mix(vec3f(1.0, 0.45, 0.20), vec3f(1.25, 1.21, 1.12), clamp(dayF * 2.0, 0.0, 1.0));
  var sk = skyGrad(rd, sunDir);
  // Форма из физики: отношение к зениту модулирует градиент (уровни наши).
  let moonDir0 = -sunDir;
  let up0 = vec3f(0.0, 1.0, 0.0);
  let zen = skyPhys(up0, sunDir, moonDir0);
  let raw = skyPhys(rd, sunDir, moonDir0);
  let ratio = clamp(raw / max(lum3(zen), 1e-3), vec3f(0.0), vec3f(3.0));
  sk *= (0.35 + 0.65 * ratio);
  // Звёзды и луна из K (упрощены: 1 слой сетки, диск без кратеров).
  // Луна opposite солнца — видна ночью. Всё гаснет днём.
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
  sk += (star + moon) * night;
  return sk + sunTerms(rd, sunDir, sunCol, night);
}

// Главный проход: фулскрин-треугольник, воксельный мир DDA, туман.
// Шрифт 5x7 для меню (G,A,M,E,X,P,O,F,R,0-9,.,-,space), по 7 строк на глиф.
const FONT: array<u32, 196> = array<u32, 196>(
  14u, 17u, 16u, 23u, 17u, 17u, 14u, // G
  14u, 17u, 17u, 31u, 17u, 17u, 17u, // A
  17u, 27u, 21u, 21u, 17u, 17u, 17u, // M
  31u, 16u, 16u, 30u, 16u, 16u, 31u, // E
  17u, 17u, 10u, 4u, 10u, 17u, 17u, // X
  30u, 17u, 17u, 30u, 16u, 16u, 16u, // P
  14u, 17u, 17u, 17u, 17u, 17u, 14u, // O
  31u, 16u, 16u, 30u, 16u, 16u, 16u, // F
  30u, 17u, 17u, 30u, 20u, 18u, 17u, // R
  14u, 17u, 17u, 17u, 17u, 17u, 14u, // 0
  4u, 12u, 4u, 4u, 4u, 4u, 14u, // 1
  14u, 17u, 1u, 2u, 4u, 8u, 31u, // 2
  30u, 1u, 1u, 6u, 1u, 1u, 30u, // 3
  2u, 6u, 10u, 17u, 31u, 2u, 2u, // 4
  31u, 16u, 30u, 1u, 1u, 17u, 14u, // 5
  14u, 16u, 16u, 30u, 17u, 17u, 14u, // 6
  31u, 1u, 2u, 4u, 8u, 8u, 8u, // 7
  14u, 17u, 17u, 14u, 17u, 17u, 14u, // 8
  14u, 17u, 17u, 15u, 1u, 1u, 14u, // 9
  0u, 0u, 0u, 0u, 0u, 6u, 6u, // .
  0u, 0u, 0u, 31u, 0u, 0u, 0u, // -
  0u, 0u, 0u, 0u, 0u, 0u, 0u, // space
  14u, 16u, 16u, 14u, 1u, 1u, 14u, // S
  17u, 17u, 17u, 31u, 17u, 17u, 17u, // H
  30u, 17u, 17u, 17u, 17u, 17u, 30u, // D
  17u, 17u, 17u, 21u, 21u, 27u, 17u, // W
  17u, 17u, 17u, 17u, 10u, 4u, 4u, // V
  17u, 25u, 21u, 19u, 17u, 17u, 17u, // N
);

fn textOn(px: vec2f, x0: f32, y0: f32, sc: f32, gi: u32) -> bool {
  let c = i32(floor((px.x - x0) / sc));
  let r = i32(floor((px.y - y0) / sc));
  if (c < 0 || c > 4 || r < 0 || r > 6) { return false; }
  return (FONT[gi * 7u + u32(r)] & (1u << u32(4 - c))) != 0u;
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
  // Базис без NaN: на питче ±90 cross вырождается и весь кадр рвёт.
  let up0 = select(vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, -1.0), abs(fw.y) > 0.999);
  let rt = normalize(cross(fw, up0));
  let up = cross(rt, fw);
  let rd = normalize(uv.x * rt + uv.y * up + view.x * fw);

  // Фрустум-куллинг лучей (дебаг F1): вне конуса главной марша нет вообще.
  // В обычном режиме свои лучи всегда внутри своего конуса — тест пропускаем.
  let MP = frustum[0].xyz;
  let MF = frustum[1].xyz;
  let MR = frustum[2].xyz;
  let MU = frustum[3].xyz;
  var dbgOut = false;
  var roS = u.camPos;
  var tMaxF = 300.0;
  var tShift = 0.0;
  if (view.z > 0.5) {
    let ax0 = (u.resX / max(u.resY, 1.0)) * 0.5;
    let f0 = max(view.x, 0.2);
    let rel = u.camPos - MP;
    let rof = vec3f(dot(rel, MR), dot(rel, MU), dot(rel, MF));
    let rdf = vec3f(dot(rd, MR), dot(rd, MU), dot(rd, MF));
    var tN = 0.0;
    var tF = 1e9;
    var ok = true;
    if (abs(rdf.z) < 1e-9) {
      if (rof.z < 1.0 || rof.z > 60.0) { ok = false; }
    } else {
      let ta = (1.0 - rof.z) / rdf.z;
      let tb = (60.0 - rof.z) / rdf.z;
      tN = max(tN, min(ta, tb));
      tF = min(tF, max(ta, tb));
    }
    let kx = ax0 / f0;
    let ky = 0.5 / f0;
    for (var s = 0; s < 4; s++) {
      var A = 0.0;
      var B = 0.0;
      if (s == 0) { A = rdf.x - kx * rdf.z; B = -(rof.x - kx * rof.z); }
      else if (s == 1) { A = -rdf.x - kx * rdf.z; B = -(-rof.x - kx * rof.z); }
      else if (s == 2) { A = rdf.y - ky * rdf.z; B = -(rof.y - ky * rof.z); }
      else { A = -rdf.y - ky * rdf.z; B = -(-rof.y - ky * rof.z); }
      if (A > 1e-9) { tF = min(tF, B / A); }
      else if (A < -1e-9) { tN = max(tN, B / A); }
      else if (B < 0.0) { ok = false; }
    }
    if (!(ok && tN < tF && tF > 0.0)) { dbgOut = true; }
    else { tShift = max(tN, 0.0); roS = u.camPos + rd * tShift; tMaxF = min(300.0, tF - tShift); }
  }
  // Мир — окно стриминга 176x64x176: analytic вход + DDA внутри.
  var hit = voxMarch(roS, rd, tMaxF);
  if (hit.t >= 0.0) { hit.t = hit.t + tShift; } // t всегда от камеры
  var outc = vec3f(0.0);
  if (dbgOut) {
    outc = vec3f(0.01); // вне конуса: марша не было, красить нечего
  } else if (hit.t < 0.0) {
    var miss = sky(rd, normalize(u.sunDir));
    miss = pow(max(miss * grade.y, vec3f(0.0)), vec3f(1.0 / max(grade.x, 0.5)));
    outc = miss;
  } else {
  let pos = u.camPos + rd * hit.t;
  let n = hit.n;
  if (u.mode > 0.5 && u.mode < 1.5) { outc = n * 0.5 + 0.5; }
  else if (u.mode > 1.5 && u.mode < 2.5) { let g = clamp(hit.t / 120.0, 0.0, 1.0); outc = vec3f(g); }
  else if (u.mode > 2.5) { let c = clamp(hit.steps / 320.0, 0.0, 1.0); outc = vec3f(c, c * 0.3, 0.1); }
  else {
  let sunDir = normalize(u.sunDir);
  // Тень — тем же DDA к солнцу (жёсткая). Дальность от губернатора:
  // steps 25..100 -> 30..120 (раньше maxSteps никто не читал, губернатор был плацебо).
  var sh = 1.0;
  let shRange = u.maxSteps * 1.2;
  if (view.y > 0.5 && dot(n, sunDir) > 0.0 && hit.t < shRange) {
    let shHit = voxMarch(pos + n * 0.05, sunDir, shRange);
    let shRaw = select(0.0, 1.0, shHit.t < 0.0);
    let shFade = 1.0 - smoothstep(shRange - 20.0, shRange, hit.t);
    sh = mix(1.0, shRaw, shFade);
  }
  // Живой свет из K: тёплый низко, белый высоко, ночью гаснет.
  let dayF = clamp(sunDir.y, -1.0, 1.0);
  let dayL = clamp(dayF * 2.0 + 0.25, 0.04, 1.0);
  let lightCol = mix(vec3f(1.0, 0.50, 0.25), vec3f(1.25, 1.21, 1.12), clamp(dayF * 2.0, 0.0, 1.0));
  let skyAmb = sky(vec3f(0.0, 1.0, 0.0), sunDir);
  // Верх травы — grayscale-оверлей: красим биомным тинтом (как MC),
  // иначе серый x синий ambient = голубой. Низ/бока уже цветные в тайле.
  var base = textureSampleLevel(tileTex, tileSmp, tileUV(pos, n), tileLayer(hit.id, n), 0.0).rgb;
  if (hit.id == 1u && n.y > 0.5) { base *= vec3f(0.569, 0.741, 0.349); } // их тинт #91BD59
  // Анти-муар: дальше 20-80 тексель тает в плоский цвет (мипов нет).
  {
    var flat = vec3f(0.5, 0.5, 0.52);
    if (hit.id == 1u) { flat = select(vec3f(0.45, 0.32, 0.20), vec3f(0.35, 0.62, 0.25), n.y > 0.5); }
    else if (hit.id == 2u) { flat = vec3f(0.45, 0.32, 0.20); }
    else if (hit.id == 8u) { flat = vec3f(0.12, 0.12, 0.13); }
    base = mix(base, flat, smoothstep(20.0, 80.0, hit.t));
  }
  let amb = mix(vec3f(0.27, 0.24, 0.21), skyAmb, n.y * 0.5 + 0.5) * (0.35 + 0.65 * dayL);
  let ndl = max(dot(n, sunDir), 0.0);
  var col = base * (amb + lightCol * ndl * sh * dayL);
  // Контровой рим из K: край против солнца подсвечен — силуэты не плоские.
  let sunAmt = max(dot(rd, sunDir), 0.0);
  let sunset = pow(clamp(1.0 - abs(clamp(sunDir.y, -1.0, 1.0)), 0.0, 1.0), 3.0);
  let rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
  col += base * lightCol * rim * (0.15 + 0.85 * sunset);
  // Воздушная перспектива: туман греется к солнцу.
  // Высота ОТНОСИТЕЛЬНО камеры: было exp(-y/6) от абсолютной — мир на y~30,
  // туман всегда выходил ~0. Вниз — густо, вверх — разрежено.
  let fogDen = 0.0006 * grade.z * exp(-max(pos.y - u.camPos.y, 0.0) / 8.0);
  let fog = 1.0 - exp(-fogDen * hit.t * hit.t);
  var fogCol = skyGrad(rd, sunDir) + sunTerms(rd, sunDir, lightCol, 1.0 - dayL);
  fogCol = mix(fogCol, vec3f(1.0, 0.45, 0.20) * (0.4 + 0.6 * dayL), pow(sunAmt, 3.0) * 0.55 * sunset);
  outc = mix(col, fogCol, fog);
  outc = pow(max(outc * grade.y, vec3f(0.0)), vec3f(1.0 / max(grade.x, 0.5)));
  } // mode 0 (шейдинг)
  } // есть хит
  // Фрустум главной камеры (дебаг F1): 12 рёбер + точка камеры.
  if (view.z > 0.5) {
    let MP = frustum[0].xyz;
    let MF = frustum[1].xyz;
    let MR = frustum[2].xyz;
    let MU = frustum[3].xyz;
    let ax = (u.resX / max(u.resY, 1.0)) * 0.5;
    let f = max(view.x, 0.2);
    var cn: array<vec3f, 8>;
    for (var k = 0; k < 8; k++) {
      let sx = select(-1.0, 1.0, (k & 1) != 0);
      let sy = select(-1.0, 1.0, (k & 2) != 0);
      let dd = select(1.0, 60.0, k >= 4);
      cn[k] = MP + (sx * ax * MR + sy * 0.5 * MU + f * MF) * (dd / f);
    }
    var md = 1e9;
    // Гейт выше уже выкинул лучи вне конуса (марша не было) — красить нечего.
    // Рёбра и точка рисуются поверх всегда.
    // ближний/дальний прямоугольники
    for (var k = 0; k < 4; k++) {
      md = min(md, segDist(u.camPos + rd * 0.5, cn[k], cn[(k + 1) & 3]));
      md = min(md, segDist(u.camPos + rd * 0.5, cn[4 + k], cn[4 + ((k + 1) & 3)]));
      md = min(md, segDist(u.camPos + rd * 0.5, cn[k], cn[4 + k]));
    }
    // точка камеры
    let oc = u.camPos + rd * 0.5 - MP;
    let bq = dot(oc, rd);
    let hq = bq * bq - dot(oc, oc) + 0.16;
    if (hq > 0.0) {
      let tq = -bq - sqrt(hq);
      if (tq > 0.0) { md = 0.0; }
    }
    let wdt = 0.03 + length(u.camPos - MP) * 0.002;
    if (md < wdt) { outc = vec3f(1.0, 0.55, 0.1); }
  }
  // Меню настроек (Tab): подписи + значения + полосы.
  if (grade.w > -0.5) {
    let mp = vec2f(frag.x, frag.y);
    if (mp.x >= 24.0 && mp.x < 560.0 && mp.y >= 24.0 && mp.y < 308.0) {
      var mcol = vec3f(0.05, 0.06, 0.08);
      // Рамка 2px: панель видна и на белом небе.
      if (mp.x < 26.0 || mp.x >= 558.0 || mp.y < 26.0 || mp.y >= 306.0) {
        mcol = vec3f(0.48, 0.63, 1.0);
      } else {
      let row = min(i32((mp.y - 24.0) / 56.0), 4);
      let sel = i32(grade.w + 0.5);
      if (row == sel) { mcol = vec3f(0.09, 0.12, 0.17); }
      var gl = array<u32, 6>(21u, 21u, 21u, 21u, 21u, 21u);
      var vv = grade.x;
      var vmin = 0.5;
      var vspan = 3.5;
      if (row == 0) { gl = array<u32, 6>(0u, 1u, 2u, 2u, 1u, 21u); }
      else if (row == 1) { gl = array<u32, 6>(3u, 4u, 5u, 6u, 21u, 21u); vv = grade.y; vmin = 0.1; vspan = 3.9; }
      else if (row == 2) { gl = array<u32, 6>(7u, 6u, 0u, 21u, 21u, 21u); vv = grade.z; vmin = 0.0; vspan = 3.0; }
      else if (row == 3) { gl = array<u32, 6>(7u, 6u, 25u, 21u, 21u, 21u); vv = view.x; vmin = 0.5; vspan = 3.5; }
      else { gl = array<u32, 6>(22u, 23u, 1u, 24u, 6u, 25u); vv = view.y; vmin = 0.0; vspan = 1.0; }
      let rowY = 34.0 + f32(row) * 56.0;
      for (var k = 0; k < 6; k++) {
        if (textOn(mp, 36.0 + f32(k) * 21.0, rowY, 3.0, gl[k])) { mcol = vec3f(0.85); }
      }
      if (row == 4) {
        // Тумблер текстом: ON / OFF.
        var on = view.y > 0.5;
        var wa = array<u32, 4>(6u, 27u, 21u, 21u);
        if (!on) { wa = array<u32, 4>(6u, 7u, 7u, 21u); }
        for (var k = 0; k < 4; k++) {
          if (textOn(mp, 420.0 + f32(k) * 21.0, rowY, 3.0, wa[k])) { mcol = vec3f(0.85); }
        }
        if (mp.x >= 190.0 && mp.x < 350.0 && on) { mcol = vec3f(0.48, 0.63, 1.0); }
      } else {
        let cc = vv * 100.0 + 0.5;
        let va = array<u32, 4>(u32(9u + u32(min(i32(vv), 9))), 19u, u32(9u + u32((i32(cc) / 10) % 10)), u32(9u + u32(i32(cc) % 10)));
        for (var k = 0; k < 4; k++) {
          if (textOn(mp, 420.0 + f32(k) * 21.0, rowY, 3.0, va[k])) { mcol = vec3f(0.85); }
        }
        if (mp.x >= 190.0 && mp.x < 350.0) {
          if ((mp.x - 190.0) / 160.0 <= (vv - vmin) / vspan) { mcol = vec3f(0.48, 0.63, 1.0); }
        }
      } // toggle value/ON-OFF
    } // не рамка (border-else)
      outc = mcol;
    }
  }
  return vec4f(outc, 1.0);
}

)SDF";
static const char *SKY_TRANS_WGSL __attribute__((unused)) = R"SDF(
// Ядро Hillaire из K . Единицы — мегаметры.
// Подставляется префиксом к 10/20/30/40 (см. cmake/embed_wgsl.cmake).
const GROUND_RADIUS_MM = 6.360;
const ATMO_RADIUS_MM = 6.460;
const GROUND_ALBEDO = vec3f(0.3);
const RAYLEIGH_BASE = vec3f(5.802, 13.558, 33.1);
const MIE_SCAT_BASE = 3.996;
const MIE_ABS_BASE = 4.4;
const OZONE_BASE = vec3f(0.650, 1.881, 0.085);
const PI = 3.14159265;

fn safeacos(x: f32) -> f32 { return acos(clamp(x, -1.0, 1.0)); }

fn scatVals(pos: vec3f, rs: ptr<function, vec3f>, ms: ptr<function, f32>, ext: ptr<function, vec3f>) {
  let altKm = (length(pos) - GROUND_RADIUS_MM) * 1000.0;
  let rd = exp(-altKm / 8.0);
  let md = exp(-altKm / 1.2);
  *rs = RAYLEIGH_BASE * rd;
  *ms = MIE_SCAT_BASE * md;
  let ma = MIE_ABS_BASE * md;
  let oz = OZONE_BASE * max(0.0, 1.0 - abs(altKm - 25.0) / 15.0);
  *ext = *rs + vec3f(*ms + ma) + oz;
}

fn raySphere(ro: vec3f, rd: vec3f, rad: f32) -> f32 {
  let b = dot(ro, rd);
  let c = dot(ro, ro) - rad * rad;
  if (c > 0.0 && b > 0.0) { return -1.0; }
  let disc = b * b - c;
  if (disc < 0.0) { return -1.0; }
  if (disc > b * b) { return -b + sqrt(disc); }
  return -b - sqrt(disc);
}

fn miePhase(c: f32) -> f32 {
  let g = 0.8;
  return 3.0 / (8.0 * PI) * (1.0 - g * g) * (1.0 + c * c)
       / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * c, 1.5));
}
fn rayleighPhase(c: f32) -> f32 { return 3.0 / (16.0 * PI) * (1.0 + c * c); }

fn lutUv(pos: vec3f, sunDir: vec3f) -> vec2f {
  let h = length(pos);
  let up = pos / h;
  return vec2f(clamp(0.5 + 0.5 * dot(sunDir, up), 0.0, 1.0),
               clamp((h - GROUND_RADIUS_MM) / (ATMO_RADIUS_MM - GROUND_RADIUS_MM), 0.0, 1.0));
}

struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
};
@vertex
fn vs(@builtin(vertex_index) vi: u32) -> VOut {
  var o: VOut;
  let v = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
  o.pos = vec4f(v * 2.0 - 1.0, 0.0, 1.0);
  o.uv = v;
  return o;
}

// Пропускание 256x64, печётся 1 раз.
@fragment
fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
  let sunCos = 2.0 * uv.x - 1.0;
  let sunTheta = safeacos(sunCos);
  let height = mix(GROUND_RADIUS_MM, ATMO_RADIUS_MM, uv.y);
  let pos = vec3f(0.0, height, 0.0);
  let sunDir = normalize(vec3f(0.0, sunCos, -sin(sunTheta)));
  if (raySphere(pos, sunDir, GROUND_RADIUS_MM) > 0.0) { return vec4f(0.0); }
  let atmoDist = raySphere(pos, sunDir, ATMO_RADIUS_MM);
  var t = 0.0;
  var tr = vec3f(1.0);
  for (var i = 0; i < 40; i++) {
    let nt = ((f32(i) + 0.3) / 40.0) * atmoDist;
    let dt = nt - t;
    t = nt;
    var rs: vec3f;
    var ms: f32;
    var ext: vec3f;
    scatVals(pos + t * sunDir, &rs, &ms, &ext);
    tr *= exp(-dt * ext);
  }
  return vec4f(tr, 1.0);
}
)SDF";
static const char *SKY_MS_WGSL __attribute__((unused)) = R"SDF(
// Ядро Hillaire из K . Единицы — мегаметры.
// Подставляется префиксом к 10/20/30/40 (см. cmake/embed_wgsl.cmake).
const GROUND_RADIUS_MM = 6.360;
const ATMO_RADIUS_MM = 6.460;
const GROUND_ALBEDO = vec3f(0.3);
const RAYLEIGH_BASE = vec3f(5.802, 13.558, 33.1);
const MIE_SCAT_BASE = 3.996;
const MIE_ABS_BASE = 4.4;
const OZONE_BASE = vec3f(0.650, 1.881, 0.085);
const PI = 3.14159265;

fn safeacos(x: f32) -> f32 { return acos(clamp(x, -1.0, 1.0)); }

fn scatVals(pos: vec3f, rs: ptr<function, vec3f>, ms: ptr<function, f32>, ext: ptr<function, vec3f>) {
  let altKm = (length(pos) - GROUND_RADIUS_MM) * 1000.0;
  let rd = exp(-altKm / 8.0);
  let md = exp(-altKm / 1.2);
  *rs = RAYLEIGH_BASE * rd;
  *ms = MIE_SCAT_BASE * md;
  let ma = MIE_ABS_BASE * md;
  let oz = OZONE_BASE * max(0.0, 1.0 - abs(altKm - 25.0) / 15.0);
  *ext = *rs + vec3f(*ms + ma) + oz;
}

fn raySphere(ro: vec3f, rd: vec3f, rad: f32) -> f32 {
  let b = dot(ro, rd);
  let c = dot(ro, ro) - rad * rad;
  if (c > 0.0 && b > 0.0) { return -1.0; }
  let disc = b * b - c;
  if (disc < 0.0) { return -1.0; }
  if (disc > b * b) { return -b + sqrt(disc); }
  return -b - sqrt(disc);
}

fn miePhase(c: f32) -> f32 {
  let g = 0.8;
  return 3.0 / (8.0 * PI) * (1.0 - g * g) * (1.0 + c * c)
       / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * c, 1.5));
}
fn rayleighPhase(c: f32) -> f32 { return 3.0 / (16.0 * PI) * (1.0 + c * c); }

fn lutUv(pos: vec3f, sunDir: vec3f) -> vec2f {
  let h = length(pos);
  let up = pos / h;
  return vec2f(clamp(0.5 + 0.5 * dot(sunDir, up), 0.0, 1.0),
               clamp((h - GROUND_RADIUS_MM) / (ATMO_RADIUS_MM - GROUND_RADIUS_MM), 0.0, 1.0));
}

struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
};
@vertex
fn vs(@builtin(vertex_index) vi: u32) -> VOut {
  var o: VOut;
  let v = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
  o.pos = vec4f(v * 2.0 - 1.0, 0.0, 1.0);
  o.uv = v;
  return o;
}

// Многократка 32x32, печётся 1 раз
@group(0) @binding(0) var transTex: texture_2d<f32>;
@group(0) @binding(1) var smp: sampler;

fn transAt(pos: vec3f, sunDir: vec3f) -> vec3f {
  return textureSampleLevel(transTex, smp, lutUv(pos, sunDir), 0.0).rgb;
}

fn sphDir(theta: f32, phi: f32) -> vec3f {
  let cp = cos(phi);
  let sp = sin(phi);
  return vec3f(sp * sin(theta), cp, sp * cos(theta));
}

@fragment
fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
  let sunCos = 2.0 * uv.x - 1.0;
  let sunTheta = safeacos(sunCos);
  let height = mix(GROUND_RADIUS_MM, ATMO_RADIUS_MM, uv.y);
  let pos = vec3f(0.0, height, 0.0);
  let sunDir = normalize(vec3f(0.0, sunCos, -sin(sunTheta)));
  var lum_total = vec3f(0.0);
  var fms = vec3f(0.0);
  for (var i = 0; i < 8; i++) {
    for (var j = 0; j < 8; j++) {
      let theta = 2.0 * PI * (f32(i) + 0.5) / 8.0;
      let phi = safeacos(1.0 - 2.0 * (f32(j) + 0.5) / 8.0);
      let ray_dir = sphDir(theta, phi);
      let atmo_dist = raySphere(pos, ray_dir, ATMO_RADIUS_MM);
      let ground_dist = raySphere(pos, ray_dir, GROUND_RADIUS_MM);
      let t_max = select(atmo_dist, ground_dist, ground_dist > 0.0);
      let cos_theta = dot(ray_dir, sunDir);
      let mie = miePhase(cos_theta);
      let rayleigh = rayleighPhase(-cos_theta);
      var lum = vec3f(0.0);
      var lum_factor = vec3f(0.0);
      var transmittance = vec3f(1.0);
      var t = 0.0;
      for (var s = 0; s < 20; s++) {
        let nt = ((f32(s) + 0.3) / 20.0) * t_max;
        let dt = nt - t;
        t = nt;
        let new_pos = pos + t * ray_dir;
        var rs: vec3f;
        var msc: f32;
        var extinction: vec3f;
        scatVals(new_pos, &rs, &msc, &extinction);
        let sample_t = exp(-dt * extinction);
        let sno_phase = rs + vec3f(msc);
        let scattering_f = (sno_phase - sno_phase * sample_t) / extinction;
        lum_factor += transmittance * scattering_f;
        let sun_t = transAt(new_pos, sunDir);
        let in_scattering = (rs * rayleigh + vec3f(msc) * mie) * sun_t;
        let scattering_integral = (in_scattering - in_scattering * sample_t) / extinction;
        lum += scattering_integral * transmittance;
        transmittance *= sample_t;
      }
      if (ground_dist > 0.0) {
        var hit_pos = pos + ground_dist * ray_dir;
        if (dot(pos, sunDir) > 0.0) {
          hit_pos = normalize(hit_pos) * GROUND_RADIUS_MM;
          lum += transmittance * GROUND_ALBEDO * transAt(hit_pos, sunDir);
        }
      }
      fms += lum_factor / 64.0;
      lum_total += lum / 64.0;
    }
  }
  return vec4f(lum_total / (1.0 - fms), 1.0);
}
)SDF";
static const char *SKY_VIEW_WGSL __attribute__((unused)) = R"SDF(
// Ядро Hillaire из K . Единицы — мегаметры.
// Подставляется префиксом к 10/20/30/40 (см. cmake/embed_wgsl.cmake).
const GROUND_RADIUS_MM = 6.360;
const ATMO_RADIUS_MM = 6.460;
const GROUND_ALBEDO = vec3f(0.3);
const RAYLEIGH_BASE = vec3f(5.802, 13.558, 33.1);
const MIE_SCAT_BASE = 3.996;
const MIE_ABS_BASE = 4.4;
const OZONE_BASE = vec3f(0.650, 1.881, 0.085);
const PI = 3.14159265;

fn safeacos(x: f32) -> f32 { return acos(clamp(x, -1.0, 1.0)); }

fn scatVals(pos: vec3f, rs: ptr<function, vec3f>, ms: ptr<function, f32>, ext: ptr<function, vec3f>) {
  let altKm = (length(pos) - GROUND_RADIUS_MM) * 1000.0;
  let rd = exp(-altKm / 8.0);
  let md = exp(-altKm / 1.2);
  *rs = RAYLEIGH_BASE * rd;
  *ms = MIE_SCAT_BASE * md;
  let ma = MIE_ABS_BASE * md;
  let oz = OZONE_BASE * max(0.0, 1.0 - abs(altKm - 25.0) / 15.0);
  *ext = *rs + vec3f(*ms + ma) + oz;
}

fn raySphere(ro: vec3f, rd: vec3f, rad: f32) -> f32 {
  let b = dot(ro, rd);
  let c = dot(ro, ro) - rad * rad;
  if (c > 0.0 && b > 0.0) { return -1.0; }
  let disc = b * b - c;
  if (disc < 0.0) { return -1.0; }
  if (disc > b * b) { return -b + sqrt(disc); }
  return -b - sqrt(disc);
}

fn miePhase(c: f32) -> f32 {
  let g = 0.8;
  return 3.0 / (8.0 * PI) * (1.0 - g * g) * (1.0 + c * c)
       / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * c, 1.5));
}
fn rayleighPhase(c: f32) -> f32 { return 3.0 / (16.0 * PI) * (1.0 + c * c); }

fn lutUv(pos: vec3f, sunDir: vec3f) -> vec2f {
  let h = length(pos);
  let up = pos / h;
  return vec2f(clamp(0.5 + 0.5 * dot(sunDir, up), 0.0, 1.0),
               clamp((h - GROUND_RADIUS_MM) / (ATMO_RADIUS_MM - GROUND_RADIUS_MM), 0.0, 1.0));
}

struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
};
@vertex
fn vs(@builtin(vertex_index) vi: u32) -> VOut {
  var o: VOut;
  let v = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
  o.pos = vec4f(v * 2.0 - 1.0, 0.0, 1.0);
  o.uv = v;
  return o;
}

// Sky-view LUT под одно тело
// Печётся заново при смене высоты тела >1e-3 или высоты камеры >1.0.
struct ViewParams {
  body_dir: vec4f,
  cam: vec4f, // x: высота камеры, метры
};

@group(0) @binding(0) var transTex: texture_2d<f32>;
@group(0) @binding(1) var msTex: texture_2d<f32>;
@group(0) @binding(2) var smp: sampler;
@group(0) @binding(3) var<uniform> P: ViewParams;

fn atmoViewPos() -> vec3f {
  let hFrac = clamp((P.cam.x - 62.0) / 64.0, 0.0, 1.0);
  return vec3f(0.0, GROUND_RADIUS_MM + 0.0003 + hFrac * 0.002, 0.0);
}

fn skyLutUv(ray_dir: vec3f, sun_dir: vec3f) -> vec2f {
  let view_pos = atmoViewPos();
  let height = length(view_pos);
  let up = view_pos / height;
  let horizon_angle = safeacos(sqrt(height * height - GROUND_RADIUS_MM * GROUND_RADIUS_MM) / height);
  let altitude_angle = horizon_angle - acos(clamp(dot(ray_dir, up), -1.0, 1.0));
  var azimuth_angle: f32;
  if (abs(altitude_angle) > (0.5 * PI - 0.0001)) {
    azimuth_angle = 0.0;
  } else {
    let right = cross(sun_dir, up);
    let forward = cross(up, right);
    let projected = normalize(ray_dir - up * dot(ray_dir, up) + vec3f(1e-6, 0.0, 0.0));
    azimuth_angle = atan2(dot(projected, right), dot(projected, forward)) + PI;
  }
  let v = 0.5 + 0.5 * sign(altitude_angle) * sqrt(abs(altitude_angle) * 2.0 / PI);
  return vec2f(azimuth_angle / (2.0 * PI), v);
}

@fragment
fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
  let azimuth = (uv.x - 0.5) * 2.0 * PI;
  var adj_v: f32;
  if (uv.y < 0.5) {
    let coord = 1.0 - 2.0 * uv.y;
    adj_v = -coord * coord;
  } else {
    let coord = uv.y * 2.0 - 1.0;
    adj_v = coord * coord;
  }
  let view_pos = atmoViewPos();
  let height = length(view_pos);
  let up = view_pos / height;
  let horizon_angle = safeacos(sqrt(height * height - GROUND_RADIUS_MM * GROUND_RADIUS_MM) / height) - 0.5 * PI;
  let altitude_angle = adj_v * 0.5 * PI - horizon_angle;
  let cos_alt = cos(altitude_angle);
  let ray_dir = vec3f(cos_alt * sin(azimuth), sin(altitude_angle), -cos_alt * cos(azimuth));
  let sun_altitude = 0.5 * PI - acos(clamp(dot(P.body_dir.xyz, up), -1.0, 1.0));
  let sun_dir = vec3f(0.0, sin(sun_altitude), -cos(sun_altitude));
  let atmo_dist = raySphere(view_pos, ray_dir, ATMO_RADIUS_MM);
  let ground_dist = raySphere(view_pos, ray_dir, GROUND_RADIUS_MM);
  let t_max = select(atmo_dist, ground_dist, ground_dist >= 0.0);
  let cos_theta = dot(ray_dir, sun_dir);
  let mie = miePhase(cos_theta);
  let rayleigh = rayleighPhase(-cos_theta);
  var lum = vec3f(0.0);
  var transmittance = vec3f(1.0);
  var t = 0.0;
  for (var i = 0; i < 30; i++) {
    let sq = (f32(i) + 0.3) / 30.0;
    let nt = t_max * sq * sq;
    let dt = nt - t;
    t = nt;
    let new_pos = view_pos + t * ray_dir;
    var rs: vec3f;
    var msc: f32;
    var extinction: vec3f;
    scatVals(new_pos, &rs, &msc, &extinction);
    let sample_t = exp(-dt * extinction);
    let luv = lutUv(new_pos, sun_dir);
    let sun_t = textureSampleLevel(transTex, smp, luv, 0.0).rgb;
    let psi_ms = textureSampleLevel(msTex, smp, luv, 0.0).rgb;
    let in_scattering = rs * (rayleigh * sun_t + psi_ms) + vec3f(msc) * (mie * sun_t + psi_ms);
    let scattering_integral = (in_scattering - in_scattering * sample_t) / extinction;
    lum += scattering_integral * transmittance;
    transmittance *= sample_t;
  }
  return vec4f(lum, 1.0);
}
)SDF";
static const char *SKY_AMB_WGSL __attribute__((unused)) = R"SDF(
// Ядро Hillaire из K . Единицы — мегаметры.
// Подставляется префиксом к 10/20/30/40 (см. cmake/embed_wgsl.cmake).
const GROUND_RADIUS_MM = 6.360;
const ATMO_RADIUS_MM = 6.460;
const GROUND_ALBEDO = vec3f(0.3);
const RAYLEIGH_BASE = vec3f(5.802, 13.558, 33.1);
const MIE_SCAT_BASE = 3.996;
const MIE_ABS_BASE = 4.4;
const OZONE_BASE = vec3f(0.650, 1.881, 0.085);
const PI = 3.14159265;

fn safeacos(x: f32) -> f32 { return acos(clamp(x, -1.0, 1.0)); }

fn scatVals(pos: vec3f, rs: ptr<function, vec3f>, ms: ptr<function, f32>, ext: ptr<function, vec3f>) {
  let altKm = (length(pos) - GROUND_RADIUS_MM) * 1000.0;
  let rd = exp(-altKm / 8.0);
  let md = exp(-altKm / 1.2);
  *rs = RAYLEIGH_BASE * rd;
  *ms = MIE_SCAT_BASE * md;
  let ma = MIE_ABS_BASE * md;
  let oz = OZONE_BASE * max(0.0, 1.0 - abs(altKm - 25.0) / 15.0);
  *ext = *rs + vec3f(*ms + ma) + oz;
}

fn raySphere(ro: vec3f, rd: vec3f, rad: f32) -> f32 {
  let b = dot(ro, rd);
  let c = dot(ro, ro) - rad * rad;
  if (c > 0.0 && b > 0.0) { return -1.0; }
  let disc = b * b - c;
  if (disc < 0.0) { return -1.0; }
  if (disc > b * b) { return -b + sqrt(disc); }
  return -b - sqrt(disc);
}

fn miePhase(c: f32) -> f32 {
  let g = 0.8;
  return 3.0 / (8.0 * PI) * (1.0 - g * g) * (1.0 + c * c)
       / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * c, 1.5));
}
fn rayleighPhase(c: f32) -> f32 { return 3.0 / (16.0 * PI) * (1.0 + c * c); }

fn lutUv(pos: vec3f, sunDir: vec3f) -> vec2f {
  let h = length(pos);
  let up = pos / h;
  return vec2f(clamp(0.5 + 0.5 * dot(sunDir, up), 0.0, 1.0),
               clamp((h - GROUND_RADIUS_MM) / (ATMO_RADIUS_MM - GROUND_RADIUS_MM), 0.0, 1.0));
}

struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
};
@vertex
fn vs(@builtin(vertex_index) vi: u32) -> VOut {
  var o: VOut;
  let v = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
  o.pos = vec4f(v * 2.0 - 1.0, 0.0, 1.0);
  o.uv = v;
  return o;
}

// Ambient-куб 6 текселей (их wc_sky_ambient.glsl 1:1, без погоды/молнии).
struct AmbParams {
  sun_dir: vec4f,
  sun_col: vec4f,
  moon_dir: vec4f,
  moon_col: vec4f,
};

@group(0) @binding(0) var skySunTex: texture_2d<f32>;
@group(0) @binding(1) var skyMoonTex: texture_2d<f32>;
@group(0) @binding(2) var smp: sampler;
@group(0) @binding(3) var<uniform> P: AmbParams;

fn skyLutUvA(ray_dir: vec3f, sun_dir: vec3f) -> vec2f {
  let view_pos = vec3f(0.0, GROUND_RADIUS_MM + 0.0003, 0.0);
  let height = length(view_pos);
  let up = view_pos / height;
  let horizon_angle = safeacos(sqrt(height * height - GROUND_RADIUS_MM * GROUND_RADIUS_MM) / height);
  let altitude_angle = horizon_angle - acos(clamp(dot(ray_dir, up), -1.0, 1.0));
  var azimuth_angle: f32;
  if (abs(altitude_angle) > (0.5 * PI - 0.0001)) {
    azimuth_angle = 0.0;
  } else {
    let right = cross(sun_dir, up);
    let forward = cross(up, right);
    let projected = normalize(ray_dir - up * dot(ray_dir, up) + vec3f(1e-6, 0.0, 0.0));
    azimuth_angle = atan2(dot(projected, right), dot(projected, forward)) + PI;
  }
  let v = 0.5 + 0.5 * sign(altitude_angle) * sqrt(abs(altitude_angle) * 2.0 / PI);
  return vec2f(azimuth_angle / (2.0 * PI), v);
}

fn skyRaw(d: vec3f) -> vec3f {
  return textureSampleLevel(skySunTex, smp, skyLutUvA(d, P.sun_dir.xyz), 0.0).rgb * P.sun_col.rgb
       + textureSampleLevel(skyMoonTex, smp, skyLutUvA(d, P.moon_dir.xyz), 0.0).rgb * P.moon_col.rgb;
}

fn faceDir(face: i32) -> vec3f {
  if (face == 0) { return vec3f(1.0, 0.0, 0.0); }
  if (face == 1) { return vec3f(-1.0, 0.0, 0.0); }
  if (face == 2) { return vec3f(0.0, 1.0, 0.0); }
  if (face == 3) { return vec3f(0.0, -1.0, 0.0); }
  if (face == 4) { return vec3f(0.0, 0.0, 1.0); }
  return vec3f(0.0, 0.0, -1.0);
}

@fragment
fn fs(@builtin(position) frag: vec4f) -> @location(0) vec4f {
  let face = i32(frag.x);
  let D = faceDir(face);
  var sum = vec3f(0.0);
  let ground_e = P.sun_col.rgb * max(P.sun_dir.y, 0.0) + P.moon_col.rgb * max(P.moon_dir.y, 0.0);
  let ground = vec3f(0.16, 0.15, 0.13) * (ground_e / PI + skyRaw(vec3f(0.0, 1.0, 0.0)) * 0.6);
  for (var i = 0; i < 12; i++) {
    let ct = 1.0 - (f32(i) + 0.5) / 12.0 * 2.0;
    let st = sqrt(max(0.0, 1.0 - ct * ct));
    for (var j = 0; j < 24; j++) {
      let ph = (f32(j) + 0.5) / 24.0 * 2.0 * PI;
      let w = vec3f(st * cos(ph), ct, st * sin(ph));
      var L = ground;
      if (w.y > -0.02) {
        L = skyRaw(normalize(vec3f(w.x, max(w.y, 0.0), w.z)));
      }
      sum += L * max(dot(w, D), 0.0);
    }
  }
  return vec4f(sum * (4.0 * PI / 288.0) / PI, 1.0);
}
)SDF";
