// Пропускание 256x64, печётся 1 раз.
@fragment
fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
  let sunCos = 2.0 * uv.x - 1.0;
  let sunTheta = safeacos(sunCos);
  let height = mix(GROUND_RADIUS_MM, ATMO_RADIUS_MM, uv.y);
  let pos = vec3f(0.0, height, 0.0);
  let sunDir = normalize(vec3f(0.0, sunCos, -sin(sunTheta)));
  if (raySphere(pos, sunDir, GROUND_RADIUS_MM) > 0.0) { return vec4f(0.0); }
  let atmoDist = raySphere(pos, sunDir, ATMO_RADIUS_MM);
  var t = 0.0;
  var tr = vec3f(1.0);
  for (var i = 0; i < 40; i++) {
    let nt = ((f32(i) + 0.3) / 40.0) * atmoDist;
    let dt = nt - t;
    t = nt;
    var rs: vec3f;
    var ms: f32;
    var ext: vec3f;
    scatVals(pos + t * sunDir, &rs, &ms, &ext);
    tr *= exp(-dt * ext);
  }
  return vec4f(tr, 1.0);
}
