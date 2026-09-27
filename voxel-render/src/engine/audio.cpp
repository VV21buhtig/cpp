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
Buf gBreak, gBreakWood, gPlace, gStepG1, gStepG2, gStepStone, gStepWet, gSplash, gUI, gAmbient;
ma_sound gAmbSnd;
bool gAmbOn = false;

static std::string gSndDir = "sounds";

void bufFromPCM(Buf& b, const std::vector<ma_int16>& pcm) {
    b.data = pcm;
    ma_audio_buffer_config cfg = ma_audio_buffer_config_init(ma_format_s16, 1, (ma_uint64)pcm.size(), b.data.data(), nullptr);
    b.valid = ma_audio_buffer_init(&cfg, &b.rb) == MA_SUCCESS;
}

// Файл (wav/ogg/mp3) -> s16 mono 22050. false = нет/битый (тишина).
bool loadFile(Buf& b, const char* path) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 1, 22050);
    ma_decoder dec;
    ma_result dr = ma_decoder_init_file(path, &cfg, &dec);
    if (dr != MA_SUCCESS) {
        printf("audio: bad %s (err %d)\n", path, (int)dr);
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
static int gSteals = 0, gFails = 0;
void fire(Buf& b, glm::vec3 at, float vol, float rate, float maxDist, bool spatial) {
    if (!g_init || !b.valid) return;
    audioGC();
    int slot = -1;
    for (int i = 0; i < MAXTRACK; i++)
        if (!gTrackUsed[i]) { slot = i; break; }
    if (slot < 0) { // переполн — крадём самый старый слот
        static int rr = 0;
        slot = rr; rr = (rr + 1) % MAXTRACK;
        ma_sound_uninit(&gTrack[slot]);
        gSteals++;
        printf("audio: steal slot (total %d)\n", gSteals);
    }
    ma_uint32 flags = spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    ma_result r = ma_sound_init_from_data_source(&g_eng, &b.rb, flags, nullptr, &gTrack[slot]);
    if (r != MA_SUCCESS) { gFails++; printf("audio: sound init failed %d (total %d)\n", (int)r, gFails); return; }
    gTrackUsed[slot] = true;
    if (spatial) {
        ma_sound_set_position(&gTrack[slot], at.x, at.y, at.z);
        ma_sound_set_attenuation_model(&gTrack[slot], ma_attenuation_model_inverse);
        ma_sound_set_min_distance(&gTrack[slot], 2.0f);
        ma_sound_set_max_distance(&gTrack[slot], maxDist);
    }
    ma_sound_set_pitch(&gTrack[slot], rate);
    ma_sound_set_volume(&gTrack[slot], vol);
    r = ma_sound_start(&gTrack[slot]);
    if (r != MA_SUCCESS) {
        gFails++;
        printf("audio: sound start failed %d (total %d)\n", (int)r, gFails);
        ma_sound_uninit(&gTrack[slot]);
        gTrackUsed[slot] = false;
    }
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

static void loadAll() {
    auto one = [&](Buf& b, const char* n) -> bool {
        // пак -> дефолт -> тишина с логом; wav раньше ogg (ogg парсер капризный)
        std::string bases[2] = {gSndDir == "sounds" ? std::string("") : gSndDir + "/", "sounds/"};
        for (int k = 0; k < 2; k++) {
            if (bases[k].empty()) continue;
            std::string pw = bases[k] + n + ".wav";
            FILE* probe = fopen(pw.c_str(), "rb");
            if (probe) { fclose(probe); loadFile(b, pw.c_str()); return b.valid; }
            std::string po = bases[k] + n + ".ogg";
            probe = fopen(po.c_str(), "rb");
            if (probe) { fclose(probe); loadFile(b, po.c_str()); return b.valid; }
        }
        printf("audio: missing %s.{wav,ogg} (silent)\n", n);
        return b.valid;
    };
    one(gBreak, "dig");
    one(gBreakWood, "dig_wood");
    one(gPlace, "place");
    one(gStepG1, "step_grass1");
    one(gStepG2, "step_grass2");
    one(gStepStone, "step_stone");
    one(gStepWet, "step_wet");
    if (!one(gSplash, "splash")) one(gSplash, "step_wet");
    one(gUI, "ui");
    one(gAmbient, "ambient");
}

bool AudioSys::init() {
    ma_engine_config cfg = ma_engine_config_init();
    cfg.noAutoStart = MA_TRUE;
    if (ma_engine_init(&cfg, &g_eng) != MA_SUCCESS) return false;
    ma_engine_start(&g_eng);
    loadAll();
    g_init = true;
    ok = true;
    {
        FILE* probe = fopen("sounds/dig.wav", "rb");
        if (!probe) probe = fopen("sounds/dig.ogg", "rb");
        if (probe) fclose(probe);
        else printf("audio: WARNING sounds/ not found (CWD=%s?) — rebuild copies it\n", ".");
    }
    return true;
}
void AudioSys::shutdown() {
    if (!g_init) return;
    audioGC();
    if (gAmbOn) { ma_sound_uninit(&gAmbSnd); gAmbOn = false; }
    ma_engine_uninit(&g_eng);
    g_init = false;
    ok = false;
}
void AudioSys::setSoundDir(const std::string& d) { gSndDir = d; }
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
int AudioSys::voices() {
    if (!g_init) return -1;
    audioGC();
    int n = 0;
    for (int i = 0; i < MAXTRACK; i++) if (gTrackUsed[i]) n++;
    return n;
}
void AudioSys::reload() {
    if (!g_init) return;
    audioGC();
    for (int i = 0; i < MAXTRACK; i++)
        if (gTrackUsed[i]) { ma_sound_uninit(&gTrack[i]); gTrackUsed[i] = false; }
    bool amb = gAmbOn;
    if (gAmbOn) { ma_sound_uninit(&gAmbSnd); gAmbOn = false; }
    for (Buf* b : {&gBreak, &gBreakWood, &gPlace, &gStepG1, &gStepG2, &gStepStone, &gStepWet, &gSplash, &gUI, &gAmbient})
        if (b->valid) { ma_audio_buffer_uninit(&b->rb); b->valid = false; b->data.clear(); }
    loadAll();
    if (amb) wind(true);
}
void AudioSys::wind(bool on) {
    if (!g_init) return;
    if (on && !gAmbOn) startLoop(gAmbient, gAmbSnd, gAmbOn, 0.35f);
    else if (!on && gAmbOn) { ma_sound_stop(&gAmbSnd); gAmbOn = false; }
}

