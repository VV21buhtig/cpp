#define MINIAUDIO_IMPLEMENTATION
#include "engine/audio.h"
#include "../../third-party/miniaudio/miniaudio.h"

#include <cmath>
#include <vector>
#include <cstdlib>

namespace {
ma_engine g_eng;
bool g_init = false;

struct Buf {
    std::vector<ma_int16> data;
    ma_audio_buffer rb;
    bool valid = false;
};
Buf gBreak, gPlace, gStep, gSplash, gUI, gWind;
ma_sound gWindSnd;
bool gWindOn = false;

unsigned int lcg(unsigned int& s) { s = s * 1103515245u + 12345u; return (s >> 8) & 0x7fff; }

void makeNoise(Buf& b, int frames, float decay, float lp, unsigned int seed, float vol) {
    b.data.resize(frames);
    unsigned int s = seed;
    float y = 0;
    for (int i = 0; i < frames; i++) {
        float t = (float)i / frames;
        float n = ((float)lcg(s) / 32768.0f) * 2.0f - 1.0f;
        y += lp * (n - y);
        b.data[i] = (ma_int16)(y * exp(-decay * t) * vol * 32767.0f);
    }
    ma_audio_buffer_config cfg = ma_audio_buffer_config_init(ma_format_s16, 1, frames, b.data.data(), nullptr);
    b.valid = ma_audio_buffer_init(&cfg, &b.rb) == MA_SUCCESS;
}
void makeTone(Buf& b, int frames, float freq, float decay, float vol) {
    b.data.resize(frames);
    for (int i = 0; i < frames; i++) {
        float t = (float)i / frames;
        b.data[i] = (ma_int16)(sin(t * frames / 22050.0f * freq * 6.28318f) * exp(-decay * t) * vol * 32767.0f);
    }
    ma_audio_buffer_config cfg = ma_audio_buffer_config_init(ma_format_s16, 1, frames, b.data.data(), nullptr);
    b.valid = ma_audio_buffer_init(&cfg, &b.rb) == MA_SUCCESS;
}

// Пул one-shot: init in place, GC добирает отыгравшие.
#define MAXTRACK 32
ma_sound gTrack[MAXTRACK];
bool gTrackUsed[MAXTRACK] = {};
void audioGC() {
    if (!g_init) return;
    for (int i = 0; i < MAXTRACK; i++)
        if (gTrackUsed[i] && !ma_sound_is_playing(&gTrack[i])) {
            ma_sound_uninit(&gTrack[i]);
            gTrackUsed[i] = false;
        }
}
void fire(Buf& b, glm::vec3 at, float vol, float rate, float maxDist, bool spatial) {
    if (!g_init || !b.valid) return;
    audioGC();
    int slot = -1;
    for (int i = 0; i < MAXTRACK; i++)
        if (!gTrackUsed[i]) { slot = i; break; }
    if (slot < 0) return; // переполн — дропаем
    ma_uint32 flags = spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_data_source(&g_eng, &b.rb, flags, nullptr, &gTrack[slot]) != MA_SUCCESS) return;
    gTrackUsed[slot] = true;
    if (spatial) {
        ma_sound_set_position(&gTrack[slot], at.x, at.y, at.z);
        ma_sound_set_attenuation_model(&gTrack[slot], ma_attenuation_model_inverse);
        ma_sound_set_min_distance(&gTrack[slot], 2.0f);
        ma_sound_set_max_distance(&gTrack[slot], maxDist);
    }
    ma_sound_set_pitch(&gTrack[slot], rate);
    ma_sound_set_volume(&gTrack[slot], vol);
    ma_sound_start(&gTrack[slot]);
}
} // namespace

bool AudioSys::init() {
    ma_engine_config cfg = ma_engine_config_init();
    cfg.noAutoStart = MA_TRUE;
    if (ma_engine_init(&cfg, &g_eng) != MA_SUCCESS) return false;
    ma_engine_start(&g_eng);
    makeNoise(gBreak, 3969, 6.0f, 0.25f, 11, 0.5f);   // thump 0.18с
    makeTone(gPlace, 1543, 180.0f, 8.0f, 0.4f);        // click
    makeNoise(gStep, 1323, 9.0f, 0.35f, 77, 0.25f);    // шаг тихий
    makeNoise(gSplash, 7717, 3.0f, 0.12f, 55, 0.45f);  // всплеск 0.35с
    makeTone(gUI, 1323, 660.0f, 9.0f, 0.3f);           // блип
    makeNoise(gWind, 66150, 0.0f, 0.02f, 7, 0.10f);    // ветер 3с loop
    g_init = true;
    ok = true;
    return true;
}
void AudioSys::shutdown() {
    if (!g_init) return;
    audioGC();
    ma_sound_uninit(&gWindSnd);
    ma_engine_uninit(&g_eng);
    g_init = false;
    ok = false;
}
void AudioSys::setMaster(float v) {
    masterVol = v;
    if (g_init) ma_engine_set_volume(&g_eng, v);
}
void AudioSys::listener(glm::vec3 pos, glm::vec3 front) {
    if (!g_init) return;
    ma_engine_listener_set_position(&g_eng, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(&g_eng, 0, front.x, front.y, front.z);
}
void AudioSys::playBreak(glm::vec3 at) { fire(gBreak, at, 0.9f, 0.9f + (rand() % 20) / 100.0f, 48.0f, true); }
void AudioSys::playPlace(glm::vec3 at) { fire(gPlace, at, 0.8f, 1.0f, 48.0f, true); }
void AudioSys::playStep(glm::vec3 at) { fire(gStep, at, 0.55f, 0.9f + (rand() % 20) / 100.0f, 24.0f, true); }
void AudioSys::playSplash(glm::vec3 at) { fire(gSplash, at, 0.9f, 1.0f, 48.0f, true); }
void AudioSys::playThunk(glm::vec3 at) { fire(gBreak, at, 0.9f, 0.5f, 48.0f, true); }
void AudioSys::playUI() { fire(gUI, glm::vec3(0), 0.5f, 1.0f, 0.0f, false); }
void AudioSys::wind(bool on) {
    if (!g_init || on == gWindOn) return;
    gWindOn = on;
    if (on) {
        if (ma_sound_init_from_data_source(&g_eng, &gWind.rb, MA_SOUND_FLAG_LOOPING, nullptr, &gWindSnd) == MA_SUCCESS) {
            ma_sound_set_volume(&gWindSnd, 0.5f);
            ma_sound_start(&gWindSnd);
        }
    } else ma_sound_stop(&gWindSnd);
}
