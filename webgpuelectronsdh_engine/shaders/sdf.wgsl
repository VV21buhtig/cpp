// Шаг 2: +ПОЛ. Куб и шар стоят на плоскости (не парят, не утоплены).
// Лестница: 0) куб -> 1) +шар -> 2) +пол -> 3) +тени -> 4) +небо/туман.
// Плоскость точная: добивка аналитикой (скользящий марш у горизонта дребезжит).
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
    return vec4f(vec3f(0.02, 0.03, 0.06), 1.0);
  }
  let n = calcNormal(u.camPos + rd * m.x, m.x);
  let l = normalize(vec3f(-0.5, 0.8, 0.35));
  var base = vec3f(0.60, 0.65, 0.75); // куб
  if (m.y > 0.5 && m.y < 1.5) { base = vec3f(0.75, 0.45, 0.35); } // шар
  else if (m.y > 1.5) { base = vec3f(0.55, 0.60, 0.45); } // пол
  let col = base * (0.25 + 0.90 * max(dot(n, l), 0.0));
  return vec4f(col, 1.0);
}
