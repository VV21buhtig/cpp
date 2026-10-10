// Многократка 32x32, печётся 1 раз
@group(0) @binding(0) var transTex: texture_2d<f32>;
@group(0) @binding(1) var smp: sampler;

fn transAt(pos: vec3f, sunDir: vec3f) -> vec3f {
  return textureSampleLevel(transTex, smp, lutUv(pos, sunDir), 0.0).rgb;
}

fn sphDir(theta: f32, phi: f32) -> vec3f {
  let cp = cos(phi);
  let sp = sin(phi);
  return vec3f(sp * sin(theta), cp, sp * cos(theta));
}

@fragment
fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
  let sunCos = 2.0 * uv.x - 1.0;
  let sunTheta = safeacos(sunCos);
  let height = mix(GROUND_RADIUS_MM, ATMO_RADIUS_MM, uv.y);
  let pos = vec3f(0.0, height, 0.0);
  let sunDir = normalize(vec3f(0.0, sunCos, -sin(sunTheta)));
  var lum_total = vec3f(0.0);
  var fms = vec3f(0.0);
  for (var i = 0; i < 8; i++) {
    for (var j = 0; j < 8; j++) {
      let theta = 2.0 * PI * (f32(i) + 0.5) / 8.0;
      let phi = safeacos(1.0 - 2.0 * (f32(j) + 0.5) / 8.0);
      let ray_dir = sphDir(theta, phi);
      let atmo_dist = raySphere(pos, ray_dir, ATMO_RADIUS_MM);
      let ground_dist = raySphere(pos, ray_dir, GROUND_RADIUS_MM);
      let t_max = select(atmo_dist, ground_dist, ground_dist > 0.0);
      let cos_theta = dot(ray_dir, sunDir);
      let mie = miePhase(cos_theta);
      let rayleigh = rayleighPhase(-cos_theta);
      var lum = vec3f(0.0);
      var lum_factor = vec3f(0.0);
      var transmittance = vec3f(1.0);
      var t = 0.0;
      for (var s = 0; s < 20; s++) {
        let nt = ((f32(s) + 0.3) / 20.0) * t_max;
        let dt = nt - t;
        t = nt;
        let new_pos = pos + t * ray_dir;
        var rs: vec3f;
        var msc: f32;
        var extinction: vec3f;
        scatVals(new_pos, &rs, &msc, &extinction);
        let sample_t = exp(-dt * extinction);
        let sno_phase = rs + vec3f(msc);
        let scattering_f = (sno_phase - sno_phase * sample_t) / extinction;
        lum_factor += transmittance * scattering_f;
        let sun_t = transAt(new_pos, sunDir);
        let in_scattering = (rs * rayleigh + vec3f(msc) * mie) * sun_t;
        let scattering_integral = (in_scattering - in_scattering * sample_t) / extinction;
        lum += scattering_integral * transmittance;
        transmittance *= sample_t;
      }
      if (ground_dist > 0.0) {
        var hit_pos = pos + ground_dist * ray_dir;
        if (dot(pos, sunDir) > 0.0) {
          hit_pos = normalize(hit_pos) * GROUND_RADIUS_MM;
          lum += transmittance * GROUND_ALBEDO * transAt(hit_pos, sunDir);
        }
      }
      fms += lum_factor / 64.0;
      lum_total += lum / 64.0;
    }
  }
  return vec4f(lum_total / (1.0 - fms), 1.0);
}
