#define MINIAUDIO_IMPLEMENTATION
#include "engine/audio.h"
#include "../../third-party/miniaudio/miniaudio.h"

#include <cmath>
#include <vector>
#include <cstdlib>
#include <cstdio>

namespace {
ma_engine g_eng;
bool g_init = false;

struct Buf {
    std::vector<ma_int16> data;
    ma_audio_buffer rb;
    bool valid = false;
};
Buf gBreak, gBreakWood, gPlace, gStepG1, gStepG2, gStepStone, gStepWet, gSplash, gUI, gAmbient, gWaterLoop;
ma_sound gAmbSnd, gWaterSnd;
bool gAmbOn = false, gWaterOn = false;
float gWaterVol = 0.0f;

unsigned int lcg(unsigned int& s) { s = s * 1103515245u + 12345u; return (s >> 8) & 0x7fff; }

void bufFromPCM(Buf& b, const std::vector<ma_int16>& pcm) {
    b.data = pcm;
    ma_audio_buffer_config cfg = ma_audio_buffer_config_init(ma_format_s16, 1, (ma_uint64)pcm.size(), b.data.data(), nullptr);
    b.valid = ma_audio_buffer_init(&cfg, &b.rb) == MA_SUCCESS;
}

// Файл (ogg/wav/mp3) -> s16 mono 22050. false = нет файла.
bool loadFile(Buf& b, const char* path) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 1, 22050);
    ma_decoder dec;
    if (ma_decoder_init_file(path, &cfg, &dec) != MA_SUCCESS) {
        printf("audio: missing %s (fallback)\n", path);
        return false;
    }
    std::vector<ma_int16> pcm;
    pcm.reserve(22050);
    ma_int16 tmp[4096];
    ma_uint64 n;
    while (ma_decoder_read_pcm_frames(&dec, tmp, 4096, &n) == MA_SUCCESS && n > 0)
        pcm.insert(pcm.end(), tmp, tmp + n);
    ma_decoder_uninit(&dec);
    if (pcm.empty()) return false;
    bufFromPCM(b, pcm);
    printf("audio: loaded %s (%zu frames)\n", path, pcm.size());
    return true;
}

// --- процедурные фолбэки (если файлов нет) ---
void makeNoise(Buf& b, int frames, float decay, float lp, unsigned int seed, float vol) {
    std::vector<ma_int16> pcm(frames);
    unsigned int s = seed;
    float y = 0;
    for (int i = 0; i < frames; i++) {
        float t = (float)i / frames;
        float n = ((float)lcg(s) / 32768.0f) * 2.0f - 1.0f;
        y += lp * (n - y);
        pcm[i] = (ma_int16)(y * exp(-decay * t) * vol * 32767.0f);
    }
    bufFromPCM(b, pcm);
}
void makeTone(Buf& b, int frames, float freq, float decay, float vol) {
    std::vector<ma_int16> pcm(frames);
    for (int i = 0; i < frames; i++) {
        float t = (float)i / frames;
        pcm[i] = (ma_int16)(sin(t * frames / 22050.0f * freq * 6.28318f) * exp(-decay * t) * vol * 32767.0f);
    }
    bufFromPCM(b, pcm);
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
void startLoop(Buf& b, ma_sound& s, bool& flag, float vol) {
    if (!g_init || !b.valid || flag) return;
    if (ma_sound_init_from_data_source(&g_eng, &b.rb, MA_SOUND_FLAG_LOOPING, nullptr, &s) == MA_SUCCESS) {
        ma_sound_set_volume(&s, vol);
        ma_sound_start(&s);
        flag = true;
    }
}
} // namespace

bool AudioSys::init() {
    ma_engine_config cfg = ma_engine_config_init();
    cfg.noAutoStart = MA_TRUE;
    if (ma_engine_init(&cfg, &g_eng) != MA_SUCCESS) return false;
    ma_engine_start(&g_eng);
    if (!loadFile(gBreak, "sounds/dig.ogg"))         makeNoise(gBreak, 3969, 6.0f, 0.25f, 11, 0.5f);
    if (!loadFile(gBreakWood, "sounds/dig_wood.ogg")) makeNoise(gBreakWood, 3000, 7.0f, 0.3f, 12, 0.45f);
    if (!loadFile(gPlace, "sounds/place.ogg"))        makeTone(gPlace, 1543, 180.0f, 8.0f, 0.4f);
    if (!loadFile(gStepG1, "sounds/step_grass1.ogg")) makeNoise(gStepG1, 1323, 9.0f, 0.35f, 71, 0.25f);
    if (!loadFile(gStepG2, "sounds/step_grass2.ogg")) makeNoise(gStepG2, 1323, 9.0f, 0.35f, 72, 0.25f);
    if (!loadFile(gStepStone, "sounds/step_stone.ogg")) makeNoise(gStepStone, 1323, 9.0f, 0.5f, 73, 0.3f);
    if (!loadFile(gStepWet, "sounds/step_wet.ogg"))   makeNoise(gStepWet, 1500, 5.0f, 0.2f, 74, 0.35f);
    if (!loadFile(gSplash, "sounds/step_wet.ogg"))    makeNoise(gSplash, 7717, 3.0f, 0.12f, 55, 0.45f);
    if (!loadFile(gUI, "sounds/ui.ogg"))              makeTone(gUI, 1323, 660.0f, 9.0f, 0.3f);
    if (!loadFile(gAmbient, "sounds/ambient.ogg"))    makeNoise(gAmbient, 66150, 0.0f, 0.02f, 7, 0.10f);
    if (!loadFile(gWaterLoop, "sounds/waterloop.ogg")) makeNoise(gWaterLoop, 22050, 0.0f, 0.05f, 9, 0.15f);
    g_init = true;
    ok = true;
    return true;
}
void AudioSys::shutdown() {
    if (!g_init) return;
    audioGC();
    if (gAmbOn) { ma_sound_uninit(&gAmbSnd); gAmbOn = false; }
    if (gWaterOn) { ma_sound_uninit(&gWaterSnd); gWaterOn = false; }
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
void AudioSys::playBreakWood(glm::vec3 at) { fire(gBreakWood, at, 0.9f, 0.9f + (rand() % 20) / 100.0f, 48.0f, true); }
void AudioSys::playPlace(glm::vec3 at) { fire(gPlace, at, 0.8f, 1.0f, 48.0f, true); }
void AudioSys::playStep(glm::vec3 at, int surf) {
    // surf: 0 трава/земля/дерево, 1 камень/руда, 2 мокро
    if (surf == 2) fire(gStepWet, at, 0.6f, 1.0f, 24.0f, true);
    else if (surf == 1) fire(gStepStone, at, 0.55f, 0.9f + (rand() % 20) / 100.0f, 24.0f, true);
    else if (rand() % 2) fire(gStepG1, at, 0.55f, 0.9f + (rand() % 20) / 100.0f, 24.0f, true);
    else fire(gStepG2, at, 0.55f, 0.9f + (rand() % 20) / 100.0f, 24.0f, true);
}
void AudioSys::playSplash(glm::vec3 at) { fire(gSplash, at, 0.9f, 1.0f, 48.0f, true); }
void AudioSys::playThunk(glm::vec3 at) { fire(gBreak, at, 0.9f, 0.5f, 48.0f, true); }
void AudioSys::playUI() { fire(gUI, glm::vec3(0), 0.5f, 1.0f, 0.0f, false); }
void AudioSys::wind(bool on) {
    if (!g_init) return;
    if (on && !gAmbOn) startLoop(gAmbient, gAmbSnd, gAmbOn, 0.35f);
    else if (!on && gAmbOn) { ma_sound_stop(&gAmbSnd); gAmbOn = false; }
}
void AudioSys::waterAt(glm::vec3 at, float level, float dt) {
    if (!g_init) return;
    float target = level > 0.01f ? 0.8f : 0.0f;
    gWaterVol += (target - gWaterVol) * (dt < 1.0f ? dt * 3.0f : 1.0f);
    if (gWaterVol > 0.02f && !gWaterOn) {
        if (ma_sound_init_from_data_source(&g_eng, &gWaterLoop.rb, MA_SOUND_FLAG_LOOPING, nullptr, &gWaterSnd) == MA_SUCCESS) {
            ma_sound_start(&gWaterSnd);
            gWaterOn = true;
        } else return;
    }
    if (!gWaterOn) return;
    ma_sound_set_position(&gWaterSnd, at.x, at.y, at.z);
    ma_sound_set_volume(&gWaterSnd, gWaterVol);
    if (gWaterVol <= 0.02f && target <= 0.02f) { ma_sound_stop(&gWaterSnd); ma_sound_uninit(&gWaterSnd); gWaterOn = false; }
}
