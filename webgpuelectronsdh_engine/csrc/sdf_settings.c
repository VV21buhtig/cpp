#include "sdf_settings.h"
#include <stdio.h>
#include <string.h>

#define SDF_GAMMA_DEF 2.2f
#define SDF_EXPOSURE_DEF 1.0f
#define SDF_FOG_DEF 1.0f
#define SDF_FOV_DEF 1.6f

void sdf_settings_load(SdfSettings *s, const char *path) {
    s->gamma = SDF_GAMMA_DEF;
    s->exposure = SDF_EXPOSURE_DEF;
    s->fog = SDF_FOG_DEF;
    s->fov = SDF_FOV_DEF;
    s->shadow = 1.0f;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char name[64];
    float v = 0;
    while (fscanf(f, "%63s %f", name, &v) == 2) {
        if (!strcmp(name, "gamma")) s->gamma = v;
        else if (!strcmp(name, "exposure")) s->exposure = v;
        else if (!strcmp(name, "fog")) s->fog = v;
        else if (!strcmp(name, "fov")) s->fov = v;
        else if (!strcmp(name, "shadow")) s->shadow = v;
    }
    fclose(f);
    if (s->gamma < 0.5f) s->gamma = 0.5f;
    if (s->gamma > 4.0f) s->gamma = 4.0f;
    if (s->exposure < 0.1f) s->exposure = 0.1f;
    if (s->exposure > 4.0f) s->exposure = 4.0f;
    if (s->fog < 0.0f) s->fog = 0.0f;
    if (s->fog > 3.0f) s->fog = 3.0f;
    if (s->fov < 0.5f) s->fov = 0.5f;
    if (s->fov > 4.0f) s->fov = 4.0f;
    s->shadow = s->shadow >= 0.5f ? 1.0f : 0.0f;
}

void sdf_settings_save(const SdfSettings *s, const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "gamma %.3f\nexposure %.3f\nfog %.3f\nfov %.3f\nshadow %.0f\n",
            s->gamma, s->exposure, s->fog, s->fov, s->shadow);
    fclose(f);
    rename(tmp, path); // атомарно как у них
}
