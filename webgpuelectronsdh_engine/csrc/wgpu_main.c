// App: механика поверх RenderCore. Ядро (GPU/текстуры/сабмит) — core/render_api.h.
// Здесь: ввод, камеры, меню, настройки, часы, губернатор, мир, цикл, лог.
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "core/render_api.h"
#include "sdf_math.h"
#include "sdf_ubo.h"
#include "sdf_scene.h"
#include "sdf_settings.h"
#include "vox/vox_world.h"
#include "vox/vox_gen.h"

typedef struct {
    GLFWwindow *win;
    RenderCore *rc;
    VoxWorld world;
    SdfSettings settings;
    time_t cfgMtime;
    Vec3 camPos;
    double yaw, pitch, speed;
    Vec3 mainPos;
    double mainYaw, mainPitch;
    int debugCam;
    int ctrlHeld;
    int mode;
    int menuOpen, menuSel;
    double t0, prevT, lastLog;
    double dayT, cloudT, timeScale;
    float fpsEma, maxSteps;
    float dtMax;
    int frame, maxFrames;
} App;

static double g_lx, g_ly;
static double g_sens = 0.0012;
static int g_locked = 0;
static App *g_app = 0;

static void set_locked(GLFWwindow *w, int locked) {
    g_locked = locked;
    glfwSetInputMode(w, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    int ww, hh;
    glfwGetWindowSize(w, &ww, &hh);
    g_lx = ww * 0.5; g_ly = hh * 0.5;
}

#define CAM_RADIUS 0.25f
static int check_aabb(float x, float z, float y) {
    return vox_floor(x - CAM_RADIUS, z - CAM_RADIUS, y) &&
           vox_floor(x + CAM_RADIUS, z - CAM_RADIUS, y) &&
           vox_floor(x - CAM_RADIUS, z + CAM_RADIUS, y) &&
           vox_floor(x + CAM_RADIUS, z + CAM_RADIUS, y);
}

static void on_mouse(GLFWwindow *w, double x, double y) {
    (void)w;
    if (!g_locked || !g_app) { g_lx = x; g_ly = y; return; }
    double *YW = &g_app->yaw, *PT = &g_app->pitch;
    if (g_app->debugCam && !g_app->ctrlHeld) { YW = &g_app->mainYaw; PT = &g_app->mainPitch; }
    *YW += (x - g_lx) * g_sens;
    *PT -= (y - g_ly) * g_sens;
    if (*PT > 1.45) *PT = 1.45;
    if (*PT < -1.45) *PT = -1.45;
    g_lx = x; g_ly = y;
}

static void on_btn(GLFWwindow *w, int b, int act, int m) {
    (void)m;
    if (b != GLFW_MOUSE_BUTTON_LEFT || act != GLFW_PRESS) return;
    if (!g_locked && g_app && !g_app->menuOpen) { set_locked(w, 1); return; }
    if (g_app && g_app->menuOpen) {
        double cx, cy;
        glfwGetCursorPos(w, &cx, &cy);
        int ww, hh, fw, fh;
        glfwGetWindowSize(w, &ww, &hh);
        glfwGetFramebufferSize(w, &fw, &fh);
        double px = ww > 0 ? cx * fw / ww : cx;
        double py = hh > 0 ? cy * fh / hh : cy;
        if (px >= 24 && px < 560 && py >= 24 && py < 308) {
            int row = (int)((py - 24) / 56);
            if (row < 0) row = 0;
            if (row > 4) row = 4;
            g_app->menuSel = row;
            if (row == 4) {
                g_app->settings.shadow = g_app->settings.shadow >= 0.5f ? 0.0f : 1.0f;
            } else if (px >= 190 && px < 350) {
                double f = (px - 190) / 160;
                if (f < 0) f = 0;
                if (f > 1) f = 1;
                if (row == 0) g_app->settings.gamma = (float)(0.5 + f * 3.5);
                else if (row == 1) g_app->settings.exposure = (float)(0.1 + f * 3.9);
                else if (row == 2) g_app->settings.fog = (float)(f * 3.0);
                else g_app->settings.fov = (float)(0.5 + f * 3.5);
            }
        }
    }
}

static void on_scroll(GLFWwindow *w, double dx, double dy) {
    (void)w; (void)dx;
    if (!g_app) return;
    g_app->speed *= (1.0 + (dy > 0 ? 0.15 : dy < 0 ? -0.15 : 0.0));
    if (g_app->speed < 1.0) g_app->speed = 1.0;
    if (g_app->speed > 12.0) g_app->speed = 12.0;
}

static void app_clamp_settings(SdfSettings *s) {
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

static int app_frame(App *app) {
    glfwPollEvents();
    if (glfwGetKey(app->win, GLFW_KEY_ESCAPE) == GLFW_PRESS && g_locked) set_locked(app->win, 0);

    double now = glfwGetTime();
    double frameStart = now;
    double t = now - app->t0;
    float dt = (float)(now - app->prevT);
    app->prevT = now;

    static int nPrev = 0, f1Prev = 0;
    int nDown = glfwGetKey(app->win, GLFW_KEY_N) == GLFW_PRESS;
    if (nDown && !nPrev) { app->mode = (app->mode + 1) % 4; printf("view mode=%d\n", app->mode); }
    nPrev = nDown;
    int ctrlHeld = glfwGetKey(app->win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                   glfwGetKey(app->win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    app->ctrlHeld = ctrlHeld;
    int f1D = glfwGetKey(app->win, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1D && !f1Prev) {
        app->debugCam = !app->debugCam;
        if (app->debugCam) {
            app->mainPos = app->camPos;
            app->mainYaw = app->yaw;
            app->mainPitch = app->pitch;
            printf("debug cam: лети, главная заморожена (F1 назад)\n");
        } else {
            app->camPos = app->mainPos;
            app->yaw = app->mainYaw;
            app->pitch = app->mainPitch;
            printf("debug cam: выкл, возврат\n");
        }
    }
    f1Prev = f1D;

    // Меню (Tab): стрелки + клик. Открыто — курсор свободен.
    static int tabPrev = 0;
    int tabDown = glfwGetKey(app->win, GLFW_KEY_TAB) == GLFW_PRESS;
    if (tabDown && !tabPrev) {
        app->menuOpen = !app->menuOpen;
        if (app->menuOpen) { set_locked(app->win, 0); }
        else {
            set_locked(app->win, 1);
            sdf_settings_save(&app->settings, "settings.cfg");
            printf("grade: saved gamma=%.2f exposure=%.2f fog=%.2f fov=%.2f shadow=%.0f\n",
                app->settings.gamma, app->settings.exposure, app->settings.fog,
                app->settings.fov, app->settings.shadow);
        }
    }
    tabPrev = tabDown;
    if (app->menuOpen) {
        static int upPrev = 0, dnPrev = 0;
        int upD = glfwGetKey(app->win, GLFW_KEY_UP) == GLFW_PRESS;
        int dnD = glfwGetKey(app->win, GLFW_KEY_DOWN) == GLFW_PRESS;
        if (upD && !upPrev) { app->menuSel = (app->menuSel + 4) % 5; }
        if (dnD && !dnPrev) { app->menuSel = (app->menuSel + 1) % 5; }
        upPrev = upD; dnPrev = dnD;
        float rate = dt * 1.0f;
        static int lfPrev = 0, rtPrev = 0;
        int lfD = glfwGetKey(app->win, GLFW_KEY_LEFT) == GLFW_PRESS;
        int rtD = glfwGetKey(app->win, GLFW_KEY_RIGHT) == GLFW_PRESS;
        int lfE = lfD && !lfPrev, rtE = rtD && !rtPrev;
        lfPrev = lfD; rtPrev = rtD;
        if (app->menuSel == 4) {
            if (lfE || rtE) app->settings.shadow = app->settings.shadow >= 0.5f ? 0.0f : 1.0f;
        } else if (lfD || rtD) {
            float d = ((lfD ? -1.0f : 0.0f) + (rtD ? 1.0f : 0.0f)) * rate;
            if (app->menuSel == 0) app->settings.gamma += d;
            else if (app->menuSel == 1) app->settings.exposure += d;
            else if (app->menuSel == 2) app->settings.fog += d;
            else app->settings.fov += d;
        }
        app_clamp_settings(&app->settings);
    }
    if (dt > 0.5f) dt = 0.5f;
    if (dt > app->dtMax) app->dtMax = dt;
    float logic_dt = dt > 0.033f ? 0.033f : dt;
    GLFWwindow *win = app->win;
    int tDown = glfwGetKey(win, GLFW_KEY_T) == GLFW_PRESS;
    int shDown = glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                 glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    app->timeScale = tDown ? (shDown ? -36.0 : 36.0) : 1.0;
    app->dayT += logic_dt * app->timeScale;
    app->cloudT += logic_dt;
    if (dt > 0.0f) {
        float fps = 1.0f / dt;
        app->fpsEma = app->fpsEma > 0.0f ? app->fpsEma * 0.95f + fps * 0.05f : fps;
        if (app->fpsEma < 45.0f && app->maxSteps > 25.0f) app->maxSteps -= 5.0f;
        else if (app->fpsEma > 57.0f && app->maxSteps < 100.0f) app->maxSteps += 1.0f;
    }

    // Цель ввода: в дебаге без Ctrl едет главная, иначе активная.
    Vec3 *CP = &app->camPos;
    double *YW = &app->yaw, *PT = &app->pitch;
    if (app->debugCam && !app->ctrlHeld) { CP = &app->mainPos; YW = &app->mainYaw; PT = &app->mainPitch; }
    Vec3 fwd, right, upv;
    cam_basis((float)*YW, (float)*PT, &fwd, &right, &upv);
    Vec3 fh = v3_norm(v3(fwd.x, 0.0f, fwd.z));
    Vec3 rh = v3_norm(v3(right.x, 0.0f, right.z));
    float sp = (float)app->speed * logic_dt;
    Vec3 wish = v3(0.0f, 0.0f, 0.0f);
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) wish = v3_add(wish, v3_mul(fh, sp));
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) wish = v3_sub(wish, v3_mul(fh, sp));
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) wish = v3_add(wish, v3_mul(rh, sp));
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) wish = v3_sub(wish, v3_mul(rh, sp));
    float cy0 = CP->y;
    float nx = CP->x + wish.x, nz = CP->z + wish.z;
    if (check_aabb(nx, nz, cy0)) {
        CP->x = nx;
        CP->z = nz;
    }
    else if (check_aabb(CP->x + wish.x, CP->z, cy0)) { CP->x += wish.x; }
    else if (check_aabb(CP->x, CP->z + wish.z, cy0)) { CP->z += wish.z; }
    if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) CP->y += sp;
    if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) CP->y -= sp;
    float cx = CP->x, cy = CP->y, cz = CP->z;
    float fl = vox_floor_y(cx, cz);
    if (cy < fl) cy = fl;
    CP->x = cx; CP->y = cy; CP->z = cz;
    // Рендер всегда от свободной/активной камеры вида.
    float rcx = app->camPos.x, rcy = app->camPos.y, rcz = app->camPos.z;

    // Стриминг-решения: кольцо + заливка грязных (бюджет).
    int pcx = (int)floorf(rcx / 16.0f), pcz = (int)floorf(rcz / 16.0f);
    vox_world_ensure(&app->world, pcx, pcz);
    app->rc->set_origin(app->rc, pcx - 5, pcz - 5);
    {
        int up = 0;
        for (int i = 0; i < VOX_POOL && up < 12; i++) {
            VoxSlot *s = &app->world.slots[i];
            if (!s->used || !s->dirty) continue;
            const VoxChunk *nb[6] = {
                vox_world_find(&app->world, s->cx - 1, s->cz),
                vox_world_find(&app->world, s->cx + 1, s->cz),
                0, 0, // вертикальных чанков нет (y всегда 0..64)
                vox_world_find(&app->world, s->cx, s->cz - 1),
                vox_world_find(&app->world, s->cx, s->cz + 1),
            };
            if (app->rc->upload_chunk(app->rc, s->cx, s->cz, s->data.id, nb))
                s->dirty = 0;
            up++;
        }
    }

    RcView v;
    memset(&v, 0, sizeof v);
    v.camPos = v3(rcx, rcy, rcz);
    v.yaw = (float)app->yaw; v.pitch = (float)app->pitch; v.fov = app->settings.fov;
    // Скриншот-прицел: VOX_CAM="yaw,pitch" фиксирует взгляд (иначе демо крутит).
    {
        const char *ce = getenv("VOX_CAM");
        if (ce && ce[0] && sscanf(ce, "%lf,%lf", &app->yaw, &app->pitch) == 2) {
            v.yaw = (float)app->yaw;
            v.pitch = (float)app->pitch;
        }
    }
    int ww, hh;
    glfwGetFramebufferSize(win, &ww, &hh);
    v.resW = ww; v.resH = hh;
    v.time = (float)app->cloudT;
    v.dayT = (float)app->dayT;
    Vec3 sunDir = sdf_sun(v.dayT);
    v.sunDir = sunDir;
    v.moonDir = v3(-sunDir.x, -sunDir.y, -sunDir.z);
    v.maxSteps = app->maxSteps;
    v.gamma = app->settings.gamma;
    v.exposure = app->settings.exposure;
    v.fog = app->settings.fog;
    v.shadowOn = app->settings.shadow;
    v.viewMode = app->mode;
    v.menuOpen = app->menuOpen;
    v.menuSel = app->menuSel;
    v.frustumOn = app->debugCam;
    v.mainPos = app->mainPos;
    v.mainYaw = (float)app->mainYaw;
    v.mainPitch = (float)app->mainPitch;
    app->rc->frame(app->rc, &v);

    if ((app->frame % 30) == 0) {
        struct stat st;
        if (stat("settings.cfg", &st) == 0 && st.st_mtime != app->cfgMtime) {
            app->cfgMtime = st.st_mtime;
            SdfSettings r;
            sdf_settings_load(&r, "settings.cfg");
            app->settings = r;
            printf("grade: reload gamma=%.2f exposure=%.2f fog=%.2f\n", r.gamma, r.exposure, r.fog);
        }
    }
    if (t - app->lastLog >= 4.0) {
        app->lastLog = t;
        printf("f=%d pos=(%.2f,%.2f,%.2f) yaw=%.2f pitch=%.2f spd=%.1f fps=%.0f steps=%.0f x%.0f dtmax=%.0fms%s\n",
            app->frame, rcx, rcy, rcz, app->yaw, app->pitch, app->speed, app->fpsEma, app->maxSteps, app->timeScale,
            app->dtMax * 1000.0f, app->debugCam ? (app->ctrlHeld ? " DBG+ctrl" : " DBG") : "");
        app->dtMax = 0.0f;
    }
    app->frame++;
    if (app->maxFrames > 0 && app->frame >= app->maxFrames) return 1;
#ifndef __EMSCRIPTEN__
    if (app->maxFrames <= 0) {
        double elapsed = glfwGetTime() - frameStart;
        double want = 1.0 / 60.0;
        if (elapsed < want) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = (long)((want - elapsed) * 1e9);
            nanosleep(&ts, 0);
        }
    }
#endif
    return glfwWindowShouldClose(win) ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
static App *g_frame_app = 0;
static void frame_trampoline(void *arg) { (void)arg; app_frame(g_frame_app); }
#endif

int main(int argc, char **argv) {
    static App app;
    memset(&app, 0, sizeof app);
    app.maxFrames = -1;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) app.maxFrames = atoi(argv[++i]);

    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    // Ядро выбирается до окна: GL нужен контекст 4.5, webgpu — NO_API.
    const char *coreName = "webgpu";
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--core") && i + 1 < argc) coreName = argv[++i];
    int wantGL = !strcmp(coreName, "gl");
    if (wantGL) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    app.win = glfwCreateWindow(1280, 720, "voxels — freecam WASD", 0, 0);
    if (!app.win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    g_app = &app;
    glfwSetCursorPosCallback(app.win, on_mouse);
    glfwSetMouseButtonCallback(app.win, on_btn);
    glfwSetScrollCallback(app.win, on_scroll);
    set_locked(app.win, 1);

    app.rc = wantGL ? rc_gl_create() : rc_webgpu_create();
    if (!app.rc || !app.rc->init(app.rc, app.win)) { fprintf(stderr, "core init fail\n"); return 1; }
    printf("core: %s\n", wantGL ? "gl" : "webgpu");
    vox_world_init(&app.world, 1337);
    sdf_settings_load(&app.settings, "settings.cfg");
    printf("settings: gamma=%.2f exposure=%.2f fog=%.2f fov=%.2f shadow=%.0f\n",
        app.settings.gamma, app.settings.exposure, app.settings.fog,
        app.settings.fov, app.settings.shadow);

    app.camPos = v3(32.0f, 42.0f, 12.0f);
    app.yaw = 2.16; app.pitch = -0.69; app.speed = 4.0;
    app.mode = 0; app.menuOpen = 0; app.menuSel = 0; app.debugCam = 0;
    app.dayT = 0.0; app.cloudT = 0.0; app.timeScale = 1.0;
    app.fpsEma = 0.0f; app.maxSteps = 100.0f;
    app.t0 = app.prevT = glfwGetTime();
    app.lastLog = -10.0;

#ifdef __EMSCRIPTEN__
    g_frame_app = &app;
    emscripten_set_main_loop_arg(frame_trampoline, &app, 0, 1);
#else
    while (!app_frame(&app)) { }
    printf("done: %d frames\n", app.frame);
    app.rc->shutdown(app.rc);
    rc_destroy(app.rc);
    glfwDestroyWindow(app.win);
    glfwTerminate();
#endif
    return 0;
}
