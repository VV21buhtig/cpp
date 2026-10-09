// Воксели лучом: analytic вход в окно стриминга + DDA Аманатидеса-Ву внутри.
// Окно: origin из UBO (pad0/pad1 = мировая клетка texel 0,0), размер 176x64x176.
// Вне окна — воздух. Тороид на заливке (C), в шейдере прямое смещение.
@group(1) @binding(3) var voxTex: texture_3d<u32>;

struct VoxHit {
  t: f32,
  n: vec3f,
  id: u32,
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

fn voxMarch(ro: vec3f, rd: vec3f, maxT: f32) -> VoxHit {
  var h: VoxHit;
  h.t = -1.0;
  h.n = vec3f(0.0);
  h.id = 0u;
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
  for (var i = 0; i < 320; i++) { // диагональ окна ~257 клеток
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
    let id = textureLoad(voxTex, p - vec3i(ox, 0, oz), 0).r;
    if (id != 0u) {
      h.t = t;
      h.n = n;
      h.id = id;
      break;
    }
  }
  return h;
}
