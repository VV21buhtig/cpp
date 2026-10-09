// Небо: градиент день/закат/ночь + солнце (HG-гало) + звёзды/луна. Вид из K.
fn hash33(p: vec3f) -> vec3f {
  var q = fract(p * vec3f(0.1031, 0.1030, 0.0973));
  q += dot(q, q.yxz + vec3f(33.33));
  return fract((q.xxy + q.yxx) * q.zyx);
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
