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
