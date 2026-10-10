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

  // Мир — окно стриминга 176x64x176: analytic вход + DDA внутри.
  let hit = voxMarch(u.camPos, rd, 300.0);
  var outc = vec3f(0.0);
  if (hit.t < 0.0) {
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
  // Тень — тем же DDA к солнцу (жёсткая). На весь чанк в поле зрения:
  // дальность 120, фейд 100-120. Луч рвётся первым вокселем.
  var sh = 1.0;
  if (view.y > 0.5 && dot(n, sunDir) > 0.0 && hit.t < 120.0) {
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
  // Верх травы — grayscale-оверлей: красим биомным тинтом (как MC),
  // иначе серый x синий ambient = голубой. Низ/бока уже цветные в тайле.
  var base = textureSampleLevel(tileTex, tileSmp, tileUV(pos, n), tileLayer(hit.id, n), 0.0).rgb;
  if (hit.id == 1u && n.y > 0.5) { base *= vec3f(0.55, 0.85, 0.35); }
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
    // Маска дебага: ТОЧКА хита внутри пирамиды главной (а не луч).
    // Луч изнутри конуса всегда его «пересекает» — так маска никогда не срабатывала.
    {
      var fp = u.camPos + rd * 500.0;
      if (hit.t >= 0.0) { fp = u.camPos + rd * hit.t; }
      let rel = fp - MP;
      let fz = dot(rel, MF);
      var inside = fz >= 0.0 && fz <= 60.0;
      if (inside) {
        let fx = dot(rel, MR);
        let fy = dot(rel, MU);
        let ex = 0.3 + (fz / f) * ax;
        let ey = 0.3 + (fz / f) * 0.5;
        inside = abs(fx) <= ex && abs(fy) <= ey;
      }
      if (!inside) {
        outc = vec3f(0.015);
      } else {
        outc = mix(outc, vec3f(1.0, 0.55, 0.1), 0.10);
      }
    }
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
