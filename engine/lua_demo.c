// Lua-демо: геймплей на скриптах, рендер в движке.
// Lua строит сцену (vec.lua: scene(t) -> фигуры), C скармливает vec-ядру.
// Первый скриптовый контур: логика снаружи, кадры внутри.
#include <GLFW/glfw3.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render_api.h"

RenderCore *rc_vec_create(void);

#define SHMAX 4096
static RcVecShape shbuf[SHMAX];
static int shn = 0;

static int l_shape(lua_State *L, int kind) {
    // rect/circle/line: (a,b,c,d, r,g,b,a)
    if (shn >= SHMAX) return 0;
    RcVecShape *s = &shbuf[shn++];
    s->kind = kind;
    s->a = (float)luaL_checknumber(L, 1);
    s->b = (float)luaL_checknumber(L, 2);
    s->c = (float)luaL_checknumber(L, 3);
    s->d = (float)luaL_checknumber(L, 4);
    s->rgba[0] = (float)luaL_optnumber(L, 5, 1.0);
    s->rgba[1] = (float)luaL_optnumber(L, 6, 1.0);
    s->rgba[2] = (float)luaL_optnumber(L, 7, 1.0);
    s->rgba[3] = (float)luaL_optnumber(L, 8, 1.0);
    return 0;
}

static int l_rect(lua_State *L) { return l_shape(L, 0); }
static int l_circle(lua_State *L) { return l_shape(L, 1); }
static int l_line(lua_State *L) { return l_shape(L, 2); }

static const char *script_path(void) {
    static const char *paths[] = {"engine/vec.lua", "vec.lua"};
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (f) { fclose(f); return paths[i]; }
    }
    return paths[0];
}

int main(int argc, char **argv) {
    int maxFrames = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    lua_State *L = luaL_newstate();
    if (!L) { fprintf(stderr, "lua fail\n"); return 1; }
    luaL_openlibs(L);
    lua_register(L, "rect", l_rect);
    lua_register(L, "circle", l_circle);
    lua_register(L, "line", l_line);
    if (luaL_dofile(L, script_path()) != LUA_OK) {
        fprintf(stderr, "lua script: %s\n", lua_tostring(L, -1));
        return 1;
    }
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(640, 360, "lua demo", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    RenderCore *rc = rc_vec_create();
    if (!rc || !rc->init(rc, win)) { fprintf(stderr, "vec init fail\n"); return 1; }
    printf("core: %s caps=0x%x be=%u (lua scene)\n", rc->name, rc->caps, rc->backend);
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        shn = 0;
        lua_getglobal(L, "scene");
        lua_pushnumber(L, frame / 60.0);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            fprintf(stderr, "lua scene: %s\n", lua_tostring(L, -1));
            return 1;
        }
        if (shn > 0 && !rc->upload_vector(rc, shbuf, shn)) {
            fprintf(stderr, "upload fail\n");
            return 1;
        }
        RcView v;
        memset(&v, 0, sizeof v);
        int ww, hh;
        glfwGetFramebufferSize(win, &ww, &hh);
        v.resW = ww;
        v.resH = hh;
        rc->frame(rc, &v);
        if (frame % 60 == 0) printf("f=%d fps=%.0f shapes=%d\n", frame, rc->fps(rc), shn);
        frame++;
    }
    printf("done: %d frames\n", frame);
    rc->shutdown(rc);
    rc_destroy(rc);
    lua_close(L);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
