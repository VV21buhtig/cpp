// Свет: мягкие тени вторым маршем + SDF-AO вместо SSAO (iq).
fn softShadow(ro: vec3f, rd: vec3f) -> f32 {
  var res = 1.0;
  var t = 0.05;
  for (var i = 0; i < 24; i++) {
    let h = map(ro + rd * t).x;
    if (h < 0.001) { return 0.0; }
    res = min(res, 8.0 * h / t);
    t += clamp(h, 0.01, 0.5);
    if (t > 12.0) { break; }
  }
  return clamp(res, 0.0, 1.0);
}

fn sdfAO(pos: vec3f, n: vec3f) -> f32 {
  var occ = 0.0;
  var sca = 1.0;
  for (var i = 0; i < 5; i++) {
    let h = 0.01 + 0.12 * f32(i) / 4.0;
    occ += (h - map(pos + n * h).x) * sca;
    sca *= 0.95;
  }
  return clamp(1.0 - 3.0 * occ, 0.0, 1.0);
}

// RT AO для вокселей: 5 проб вдоль нормали прямо по сетке (без обхода).
// Дешевле SSAO (нет второго прохода и буфера глубины), честнее костылей.
fn voxOcc(cell: vec3i) -> f32 {
  let ox = i32(u.pad0);
  let oz = i32(u.pad1);
  if (cell.x < ox || cell.y < 0 || cell.z < oz || cell.x >= ox + 176 || cell.y >= 64 || cell.z >= oz + 176) { return 0.0; }
  let cx = select(cell.x / 16, (cell.x - 15) / 16, cell.x < 0);
  let cz = select(cell.z / 16, (cell.z - 15) / 16, cell.z < 0);
  let lx = cell.x - cx * 16;
  let lz = cell.z - cz * 16;
  let rx = cx % 11;
  let rz = cz % 11;
  let sx = select(rx, rx + 11, rx < 0);
  let sz = select(rz, rz + 11, rz < 0);
  let tag = voxTags[sz * 11 + sx].xy;
  if (tag.x != cx || tag.y != cz) { return 0.0; }
  let id = textureLoad(voxTex, vec3i(sx * 16 + lx, cell.y, sz * 16 + lz), 0).r;
  return select(0.0, 1.0, id != 0u);
}

fn voxAO(pos: vec3f, n: vec3f, t: f32) -> f32 {
  var occ = 0.0;
  var sca = 1.0;
  for (var i = 1; i <= 5; i++) {
    let c = vec3i(floor(pos + n * (f32(i) - 0.5)));
    occ += voxOcc(c) * sca;
    sca *= 0.7;
  }
  let ao = clamp(1.0 - occ * 0.1, 0.0, 1.0);
  // Усечение по дальности: дальше 60 AO не видно — не считаем тьму.
  return mix(1.0, ao, clamp(1.0 - t / 60.0, 0.0, 1.0));
}
