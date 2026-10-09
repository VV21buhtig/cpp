// Ядро Hillaire из K (wc_atmo.glsl). Единицы — мегаметры.
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
