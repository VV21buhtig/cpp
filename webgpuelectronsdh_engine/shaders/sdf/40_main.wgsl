// Главный проход: фулскрин-треугольник, сферотрассировка, туман.
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
  // Аналитический пол: выигрывает ближний.
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
  // Воздушная перспектива: туман греется к солнцу. Плотность падает с высотой.
  let fogDen = 0.0006 * exp(-max(pos.y, 0.0) / 6.0);
  let fog = 1.0 - exp(-fogDen * m.x * m.x);
  var fogCol = skyGrad(rd, sunDir) + sunTerms(rd, sunDir, lightCol, 1.0 - dayL);
  fogCol = mix(fogCol, vec3f(1.0, 0.45, 0.20) * (0.4 + 0.6 * dayL), pow(sunAmt, 3.0) * 0.55 * sunset);
  return vec4f(mix(col, fogCol, fog), 1.0);
}
