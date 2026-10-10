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
