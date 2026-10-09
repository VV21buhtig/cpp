// Sky-view LUT под одно тело (их wc_skyview.glsl 1:1).
// Печётся заново при смене высоты тела >1e-3 или высоты камеры >1.0.
struct ViewParams {
  body_dir: vec4f,
  cam: vec4f, // x: высота камеры, метры
};

@group(0) @binding(0) var transTex: texture_2d<f32>;
@group(0) @binding(1) var msTex: texture_2d<f32>;
@group(0) @binding(2) var smp: sampler;
@group(0) @binding(3) var<uniform> P: ViewParams;

fn atmoViewPos() -> vec3f {
  let hFrac = clamp((P.cam.x - 62.0) / 64.0, 0.0, 1.0);
  return vec3f(0.0, GROUND_RADIUS_MM + 0.0003 + hFrac * 0.002, 0.0);
}

fn skyLutUv(ray_dir: vec3f, sun_dir: vec3f) -> vec2f {
  let view_pos = atmoViewPos();
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

@fragment
fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
  let azimuth = (uv.x - 0.5) * 2.0 * PI;
  var adj_v: f32;
  if (uv.y < 0.5) {
    let coord = 1.0 - 2.0 * uv.y;
    adj_v = -coord * coord;
  } else {
    let coord = uv.y * 2.0 - 1.0;
    adj_v = coord * coord;
  }
  let view_pos = atmoViewPos();
  let height = length(view_pos);
  let up = view_pos / height;
  let horizon_angle = safeacos(sqrt(height * height - GROUND_RADIUS_MM * GROUND_RADIUS_MM) / height) - 0.5 * PI;
  let altitude_angle = adj_v * 0.5 * PI - horizon_angle;
  let cos_alt = cos(altitude_angle);
  let ray_dir = vec3f(cos_alt * sin(azimuth), sin(altitude_angle), -cos_alt * cos(azimuth));
  let sun_altitude = 0.5 * PI - acos(clamp(dot(P.body_dir.xyz, up), -1.0, 1.0));
  let sun_dir = vec3f(0.0, sin(sun_altitude), -cos(sun_altitude));
  let atmo_dist = raySphere(view_pos, ray_dir, ATMO_RADIUS_MM);
  let ground_dist = raySphere(view_pos, ray_dir, GROUND_RADIUS_MM);
  let t_max = select(atmo_dist, ground_dist, ground_dist >= 0.0);
  let cos_theta = dot(ray_dir, sun_dir);
  let mie = miePhase(cos_theta);
  let rayleigh = rayleighPhase(-cos_theta);
  var lum = vec3f(0.0);
  var transmittance = vec3f(1.0);
  var t = 0.0;
  for (var i = 0; i < 30; i++) {
    let sq = (f32(i) + 0.3) / 30.0;
    let nt = t_max * sq * sq;
    let dt = nt - t;
    t = nt;
    let new_pos = view_pos + t * ray_dir;
    var rs: vec3f;
    var msc: f32;
    var extinction: vec3f;
    scatVals(new_pos, &rs, &msc, &extinction);
    let sample_t = exp(-dt * extinction);
    let luv = lutUv(new_pos, sun_dir);
    let sun_t = textureSampleLevel(transTex, smp, luv, 0.0).rgb;
    let psi_ms = textureSampleLevel(msTex, smp, luv, 0.0).rgb;
    let in_scattering = rs * (rayleigh * sun_t + psi_ms) + vec3f(msc) * (mie * sun_t + psi_ms);
    let scattering_integral = (in_scattering - in_scattering * sample_t) / extinction;
    lum += scattering_integral * transmittance;
    transmittance *= sample_t;
  }
  return vec4f(lum, 1.0);
}
