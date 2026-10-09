// Главный проход: фулскрин-треугольник, воксельный мир DDA, туман.
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
  let rd = normalize(uv.x * rt + uv.y * up + 1.6 * fw);

  // Мир — окно стриминга 176x64x176: analytic вход + DDA внутри.
  let hit = voxMarch(u.camPos, rd, 300.0);
  if (hit.t < 0.0) {
    return vec4f(sky(rd, normalize(u.sunDir)), 1.0);
  }
  let pos = u.camPos + rd * hit.t;
  let n = hit.n;
  if (u.mode > 0.5 && u.mode < 1.5) { return vec4f(n * 0.5 + 0.5, 1.0); }
  if (u.mode > 1.5) { let g = clamp(hit.t / 120.0, 0.0, 1.0); return vec4f(g, g, g, 1.0); }
  let sunDir = normalize(u.sunDir);
  // Тень — тем же DDA к солнцу (жёсткая). На весь чанк в поле зрения:
  // дальность 120, фейд 100-120. Луч рвётся первым вокселем.
  var sh = 1.0;
  if (dot(n, sunDir) > 0.0 && hit.t < 120.0) {
    let shHit = voxMarch(pos + n * 0.05, sunDir, 120.0);
    let shRaw = select(0.0, 1.0, shHit.t < 0.0);
    let shFade = 1.0 - smoothstep(100.0, 120.0, hit.t);
    sh = mix(1.0, shRaw, shFade);
  }
  // Живой свет из K: тёплый низко, белый высоко, ночью гаснет.
  let dayF = clamp(sunDir.y, -1.0, 1.0);
  let dayL = clamp(dayF * 2.0 + 0.25, 0.04, 1.0);
  let lightCol = mix(vec3f(1.0, 0.50, 0.25), vec3f(1.25, 1.21, 1.12), clamp(dayF * 2.0, 0.0, 1.0));
  let skyAmb = sky(vec3f(0.0, 1.0, 0.0), sunDir);
  var base = vec3f(0.5, 0.5, 0.52); // камень
  if (hit.id == 1u) { // трава: верх зелёный, бока земля
    base = select(vec3f(0.45, 0.32, 0.20), vec3f(0.35, 0.62, 0.25), n.y > 0.5);
  } else if (hit.id == 2u) { base = vec3f(0.45, 0.32, 0.20); } // земля
  else if (hit.id == 8u) { base = vec3f(0.12, 0.12, 0.13); } // бедрок
  let amb = mix(vec3f(0.27, 0.24, 0.21), skyAmb, n.y * 0.5 + 0.5) * (0.35 + 0.65 * dayL);
  let ndl = max(dot(n, sunDir), 0.0);
  var col = base * (amb + lightCol * ndl * sh * dayL);
  // Контровой рим из K: край против солнца подсвечен — силуэты не плоские.
  let sunAmt = max(dot(rd, sunDir), 0.0);
  let sunset = pow(clamp(1.0 - abs(clamp(sunDir.y, -1.0, 1.0)), 0.0, 1.0), 3.0);
  let rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
  col += base * lightCol * rim * (0.15 + 0.85 * sunset);
  // Воздушная перспектива: туман греется к солнцу. Плотность падает с высотой.
  let fogDen = 0.0006 * exp(-max(pos.y, 0.0) / 6.0);
  let fog = 1.0 - exp(-fogDen * hit.t * hit.t);
  var fogCol = skyGrad(rd, sunDir) + sunTerms(rd, sunDir, lightCol, 1.0 - dayL);
  fogCol = mix(fogCol, vec3f(1.0, 0.45, 0.20) * (0.4 + 0.6 * dayL), pow(sunAmt, 3.0) * 0.55 * sunset);
  return vec4f(mix(col, fogCol, fog), 1.0);
}
