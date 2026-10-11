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
