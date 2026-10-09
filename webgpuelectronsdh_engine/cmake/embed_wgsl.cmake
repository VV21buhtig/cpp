# читает shaders/sdf.wgsl, кладёт в raw-строку R"SDF(...)SDF"
file(READ "${SRC}" _content)
if(_content MATCHES "\\)SDF\"")
  message(FATAL_ERROR "sdf.wgsl содержит )SDF\" — смени разделитель")
endif()
file(WRITE "${DST}" "#pragma once\n// Сгенерировано из shaders/sdf.wgsl. Не править руками.\nstatic const char *SDF_WGSL = R\"SDF(\n${_content})SDF\";\n")
