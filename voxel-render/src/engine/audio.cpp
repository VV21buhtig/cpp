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
std::string gSndDir = "sounds";

// Один голос на логический звук: рестарт seek0+start. Утекать нечему по построению.
struct Voice {
    std::vector<ma_int16> data;
    ma_audio_buffer rb;
    ma_sound snd;
    bool has = false; // файл загружен и звук создан
};

bool loadWav(Voice& v, const char* path) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 1, 22050);
    ma_decoder dec;
    if (ma_decoder_init_file(path, &cfg, &dec) != MA_SUCCESS) return false;
    std::vector<ma_int16> pcm;
    pcm.reserve(22050);
    ma_int16 tmp[4096];
    ma_uint64 n;
    while (ma_decoder_read_pcm_frames(&dec, tmp, 4096, &n) == MA_SUCCESS && n > 0)
        pcm.insert(pcm.end(), tmp, tmp + n);
    ma_decoder_uninit(&dec);
    if (pcm.empty()) return false;
    v.data = std::move(pcm);
    ma_audio_buffer_config bc = ma_audio_buffer_config_init(ma_format_s16, 1, (ma_uint64)v.data.size(), v.data.data(), nullptr);
    if (ma_audio_buffer_init(&bc, &v.rb) != MA_SUCCESS) return false;
    return true;
}

Voice gBreak, gBreakWood, gBreakStone, gPlace, gStepG1, gStepG2, gStepStone, gStepWet, gSplash, gSwim, gUI, gAmbient, gLavaLoop, gUnderLoop;
ma_sound gAmbSnd, gLavaSnd, gUnderSnd;
bool gAmbOn = false, gLavaOn = false, gUnderOn = false;

void dropVoice(Voice& v) {
    if (v.has) { ma_sound_uninit(&v.snd); v.has = false; }
    // PCM живёт в v.data (вектор), rb только ссылается
}

bool makeVoice(Voice& v, const char* name) {
    // пак -> дефолт; wav раньше ogg
    std::string bases[2] = {gSndDir == "sounds" ? std::string("") : gSndDir + "/", "sounds/"};
    for (int k = 0; k < 2; k++) {
        if (bases[k].empty()) continue;
        for (const char* ext : {".wav", ".ogg"}) {
            std::string p = bases[k] + name + ext;
            FILE* probe = fopen(p.c_str(), "rb");
            if (!probe) continue;
            fclose(probe);
            if (loadWav(v, p.c_str())) {
                printf("audio: loaded %s\n", p.c_str());
                ma_uint32 flags = 0;
                if (ma_sound_init_from_data_source(&g_eng, &v.rb, flags, nullptr, &v.snd) != MA_SUCCESS) return false;
                v.has = true;
                return true;
            }
            printf("audio: bad %s (silent)\n", p.c_str());
            return false;
        }
    }
    printf("audio: missing %s.{wav,ogg} (silent)\n", name);
    return false;
}

static int gStarts = 0, gStartFails = 0;
void play(Voice& v, glm::vec3 at, float vol, float rate, float maxDist, bool spatial) {
    if (!g_init || !v.has) return;
    ma_sound_seek_to_pcm_frame(&v.snd, 0);
    if (spatial) {
        ma_sound_set_position(&v.snd, at.x, at.y, at.z);
        ma_sound_set_attenuation_model(&v.snd, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(&v.snd, 2.0f);
        ma_sound_set_max_distance(&v.snd, maxDist);
    } else {
        ma_sound_set_position(&v.snd, 0, 0, 0);
    }
    ma_sound_set_spatialization_enabled(&v.snd, spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_pitch(&v.snd, rate);
    ma_sound_set_volume(&v.snd, vol);
    ma_result r = ma_sound_start(&v.snd);
    if (r != MA_SUCCESS) printf("audio: start failed %d\n", (int)r);
}

static void loadAll() {
    makeVoice(gBreak, "dig");
    makeVoice(gBreakWood, "dig_wood");
    makeVoice(gBreakStone, "dig_stone");
    makeVoice(gPlace, "place");
    makeVoice(gStepG1, "step_grass1");
    makeVoice(gStepG2, "step_grass2");
    makeVoice(gStepStone, "step_stone");
    makeVoice(gStepWet, "step_wet");
    makeVoice(gSplash, "splash");
    makeVoice(gSwim, "swim");
    makeVoice(gLavaLoop, "lava_loop");
    makeVoice(gUnderLoop, "underwater");
    makeVoice(gUI, "ui");
    makeVoice(gAmbient, "ambient");
}
} // namespace

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
        else printf("audio: WARNING sounds/ not found (CWD=?) — rebuild copies it\n");
    }
    return true;
}
void AudioSys::shutdown() {
    if (!g_init) return;
    if (gAmbOn) { ma_sound_uninit(&gAmbSnd); gAmbOn = false; }
    if (gLavaOn) { ma_sound_uninit(&gLavaSnd); gLavaOn = false; }
    if (gUnderOn) { ma_sound_uninit(&gUnderSnd); gUnderOn = false; }
    for (Voice* v : {&gBreak, &gBreakWood, &gBreakStone, &gPlace, &gStepG1, &gStepG2, &gStepStone, &gStepWet, &gSplash, &gSwim, &gUI, &gAmbient, &gLavaLoop, &gUnderLoop})
        dropVoice(*v);
    ma_engine_uninit(&g_eng);
    g_init = false;
    ok = false;
}
void AudioSys::setMaster(float v) {
    masterVol = v;
    if (g_init) ma_engine_set_volume(&g_eng, v);
}
void AudioSys::setSoundDir(const std::string& d) { gSndDir = d; }
void AudioSys::reload() {
    if (!g_init) return;
    if (gAmbOn) { ma_sound_uninit(&gAmbSnd); gAmbOn = false; }
    if (gLavaOn) { ma_sound_uninit(&gLavaSnd); gLavaOn = false; }
    if (gUnderOn) { ma_sound_uninit(&gUnderSnd); gUnderOn = false; }
    for (Voice* v : {&gBreak, &gBreakWood, &gBreakStone, &gPlace, &gStepG1, &gStepG2, &gStepStone, &gStepWet, &gSplash, &gSwim, &gUI, &gAmbient, &gLavaLoop, &gUnderLoop})
        dropVoice(*v);
    loadAll();
    if (gAmbOn) wind(true);
}
void AudioSys::listener(glm::vec3 pos, glm::vec3 front) {
    if (!g_init) return;
    ma_engine_listener_set_position(&g_eng, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(&g_eng, 0, front.x, front.y, front.z);
}
int AudioSys::voices() {
    if (!g_init) return -1;
    int n = 0;
    for (Voice* v : {&gBreak, &gBreakWood, &gBreakStone, &gPlace, &gStepG1, &gStepG2, &gStepStone, &gStepWet, &gSplash, &gUI})
        if (v->has && ma_sound_is_playing(&v->snd)) n++;
    return n;
}
void AudioSys::playBreak(glm::vec3 at) { play(gBreak, at, 0.9f, 0.9f + (rand() % 20) / 100.0f, 48.0f, true); }
void AudioSys::playBreakWood(glm::vec3 at) { play(gBreakWood, at, 0.9f, 0.9f + (rand() % 20) / 100.0f, 48.0f, true); }
// слом/ставка по типу блока: дерево, камень/руда, остальное земля
static void breakById(glm::vec3 at, int id) {
    Voice* dst = &gBreak;
    if (id == 5 || id == 4) dst = &gBreakWood;
    else if (id == 3 || (id >= 9 && id <= 12)) dst = &gBreakStone;
    play(*dst, at, 0.9f, 0.9f + (rand() % 20) / 100.0f, 48.0f, true);
}
void AudioSys::playBreakId(glm::vec3 at, int id) { breakById(at, id); }
void AudioSys::playPlaceId(glm::vec3 at, int id) {
    Voice* dst = &gPlace;
    if (id == 5 || id == 4) dst = &gBreakWood;
    else if (id == 3 || (id >= 9 && id <= 12)) dst = &gBreakStone;
    else if (id == 1 || id == 2) dst = &gBreak;
    play(*dst, at, 0.7f, 1.1f, 48.0f, true);
}
void AudioSys::playPlace(glm::vec3 at) { play(gPlace, at, 0.8f, 1.0f, 48.0f, true); }
void AudioSys::playStep(glm::vec3 at, int surf) {
    if (surf == 2) play(gStepWet, at, 0.6f, 1.0f, 24.0f, true);
    else if (surf == 1) play(gStepStone, at, 0.55f, 0.9f + (rand() % 20) / 100.0f, 24.0f, true);
    else if (rand() % 2) play(gStepG1, at, 0.55f, 0.9f + (rand() % 20) / 100.0f, 24.0f, true);
    else play(gStepG2, at, 0.55f, 0.9f + (rand() % 20) / 100.0f, 24.0f, true);
}
void AudioSys::playSplash(glm::vec3 at) { play(gSplash, at, 0.9f, 1.0f, 48.0f, true); }
void AudioSys::playSwim(glm::vec3 at, bool lava) {
    if (lava) play(gSwim, at, 0.8f, 0.6f, 32.0f, true);
    else play(gSwim, at, 0.7f, 0.9f + (rand() % 20) / 100.0f, 32.0f, true);
}
static void loopTo(ma_sound& s, bool& flag, Voice& v, float vol, bool want, glm::vec3 at) {
    if (!g_init) return;
    if (want && !flag) {
        if (!v.has) return;
        if (ma_sound_init_from_data_source(&g_eng, &v.rb, MA_SOUND_FLAG_LOOPING, nullptr, &s) == MA_SUCCESS) {
            ma_sound_set_volume(&s, vol);
            ma_sound_start(&s);
            flag = true;
        }
        return;
    }
    if (!want && flag) { ma_sound_stop(&s); ma_sound_uninit(&s); flag = false; return; }
    if (flag) ma_sound_set_position(&s, at.x, at.y, at.z);
}
void AudioSys::lavaLoop(glm::vec3 at, bool on) { loopTo(gLavaSnd, gLavaOn, gLavaLoop, 0.5f, on, at); }
void AudioSys::underLoop(bool on) { loopTo(gUnderSnd, gUnderOn, gUnderLoop, 0.6f, on, glm::vec3(0)); }
void AudioSys::playThunk(glm::vec3 at) { play(gBreak, at, 0.9f, 0.5f, 48.0f, true); }
void AudioSys::playUI() { play(gUI, glm::vec3(0), 0.5f, 1.0f, 0.0f, false); }
void AudioSys::wind(bool on) {
    if (!g_init) return;
    if (on && !gAmbOn && !gAmbient.has) return; // нечего лупить
    if (on && !gAmbOn) {
        // loop отдельным звуком поверх голосового банка
        if (ma_sound_init_from_data_source(&g_eng, &gAmbient.rb, MA_SOUND_FLAG_LOOPING, nullptr, &gAmbSnd) == MA_SUCCESS) {
            ma_sound_set_volume(&gAmbSnd, 0.35f);
            ma_sound_start(&gAmbSnd);
            gAmbOn = true;
        }
    } else if (!on && gAmbOn) { ma_sound_stop(&gAmbSnd); gAmbOn = false; }
}
