#ifndef SDF_SETTINGS_H
#define SDF_SETTINGS_H
// Настройки как их CVarSys, но без консоли: файл + хоткеи.
// settings.cfg: строки "имя значение". Нет файла — дефолты.
typedef struct {
    float gamma;    // 2.2: финал pow(col*exposure, 1/gamma)
    float exposure; // 1.0
    float fog;      // 1.0: множитель плотности тумана
} SdfSettings;

void sdf_settings_load(SdfSettings *s, const char *path);
void sdf_settings_save(const SdfSettings *s, const char *path);
#endif
