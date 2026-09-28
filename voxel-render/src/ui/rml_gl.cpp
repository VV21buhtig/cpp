#include "ui/rml_gl.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/EventListener.h>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb/stb_image.h>
#include <cstdio>
#include <cstring>
#include <cstddef>

#include "shader.h"

RmlUI gRml;

namespace {
struct Geo {
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei count = 0;
};

// RenderInterface на нашем GL 4.5 (семантика как их GL3-пример, без их glad).
class RIRml : public Rml::RenderInterface {
public:
    Shader* sh = nullptr;
    Rml::Matrix4f proj, user;
    bool hasUser = false;
    int vw = 0, vh = 0;
    bool scOn = false;
    Rml::Rectanglei sc;
    RIRml(Shader* s) : sh(s) {}

    void beginFrame(int w, int h) {
        vw = w; vh = h;
        proj = Rml::Matrix4f::ProjectOrtho(0, (float)w, (float)h, 0, -10000, 10000);
    }
    Rml::Matrix4f cur() const { return hasUser ? (proj * user) : proj; }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> verts, Rml::Span<const int> inds) override {
        Geo* g = new Geo();
        g->count = (GLsizei)inds.size();
        glGenVertexArrays(1, &g->vao);
        glBindVertexArray(g->vao);
        glGenBuffers(1, &g->vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Rml::Vertex)), verts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), (void*)offsetof(Rml::Vertex, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex), (void*)offsetof(Rml::Vertex, colour));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), (void*)offsetof(Rml::Vertex, tex_coord));
        glGenBuffers(1, &g->ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(inds.size() * sizeof(int)), inds.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);
        return (Rml::CompiledGeometryHandle)g;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle h, Rml::Vector2f trans, Rml::TextureHandle tex) override {
        Geo* g = (Geo*)h;
        sh->use();
        sh->setVec2("_translate", trans.x, trans.y);
        glm::mat4 m;
        memcpy(glm::value_ptr(m), cur().data(), 16 * sizeof(float));
        sh->setMat4("_transform", m);
        sh->setInt("_tex", 0);
        bool useTex = tex != 0;
        sh->setInt("_useTex", useTex ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, useTex ? (GLuint)tex : 0);
        if (scOn) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(sc.p0.x, vh - sc.p1.y, sc.p1.x - sc.p0.x, sc.p1.y - sc.p0.y);
        } else glDisable(GL_SCISSOR_TEST);
        glBindVertexArray(g->vao);
        glDrawElements(GL_TRIANGLES, g->count, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle h) override {
        Geo* g = (Geo*)h;
        glDeleteVertexArrays(1, &g->vao);
        glDeleteBuffers(1, &g->vbo);
        glDeleteBuffers(1, &g->ibo);
        delete g;
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dims, const Rml::String& src) override {
        int w, h, ch;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* d = stbi_load(src.c_str(), &w, &h, &ch, 4);
        stbi_set_flip_vertically_on_load(1);
        if (!d) return 0;
        dims.x = w; dims.y = h;
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(d);
        return (Rml::TextureHandle)t;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> src, Rml::Vector2i dims) override {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dims.x, dims.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, src.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return (Rml::TextureHandle)t;
    }
    void ReleaseTexture(Rml::TextureHandle t) override {
        GLuint g = (GLuint)t;
        glDeleteTextures(1, &g);
    }
    void EnableScissorRegion(bool enable) override { scOn = enable; }
    void SetScissorRegion(Rml::Rectanglei region) override { sc = region; }
    void SetTransform(const Rml::Matrix4f* m) override {
        if (m) { user = *m; hasUser = true; }
        else hasUser = false;
    }
};

// Слушатель паузы: click по кнопкам, change слайдеров.
class PauseListener : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& ev) override {
        Rml::Element* el = ev.GetTargetElement();
        if (!el) return;
        Rml::String id = el->GetId();
        Rml::String type = ev.GetType();
        if (type == "click") {
            if (id == "back" && gRml.onResume) gRml.onResume();
            else if (id == "opt") {
                Rml::Element* o = el->GetOwnerDocument()->GetElementById("opts");
                if (o) o->SetProperty("display", "block");
            } else if (id == "done") {
                Rml::Element* o = el->GetOwnerDocument()->GetElementById("opts");
                if (o) o->SetProperty("display", "none");
                if (gRml.onDone) gRml.onDone();
            } else if (id == "quit" && gRml.onQuit) gRml.onQuit();
            else if (id.compare(0, 2, "b_") == 0 && gRml.onCycle) gRml.onCycle(id.c_str());
        } else if (type == "change") {
            auto* fc = static_cast<Rml::ElementFormControl*>(el);
            float v = (float)atof(fc->GetValue().c_str());
            if (gRml.onSlider) gRml.onSlider(id.c_str(), v);
            // значение рядом со слайдером: s_fov -> v_fov
            if (id.compare(0, 2, "s_") == 0) {
                Rml::String vid = Rml::String("v_") + id.substr(2);
                Rml::Element* sp = el->GetOwnerDocument()->GetElementById(vid);
                if (sp) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%g", (double)v);
                    sp->SetInnerRML(Rml::String(buf));
                }
            }
        }
    }
};

static RIRml* ri = nullptr;
static Shader* rsh = nullptr;
static Rml::Context* ctx = nullptr;
static Rml::ElementDocument* pauseDoc = nullptr;
static PauseListener pauseListener;
static GLFWwindow* win = nullptr;
static int curW = 0, curH = 0;
static float curScale = 0.0f;
static bool pauseShown = false;

static Rml::Input::KeyIdentifier mapKey(int k) {
    switch (k) {
        case GLFW_KEY_A: return Rml::Input::KI_A; case GLFW_KEY_B: return Rml::Input::KI_B;
        case GLFW_KEY_C: return Rml::Input::KI_C; case GLFW_KEY_D: return Rml::Input::KI_D;
        case GLFW_KEY_E: return Rml::Input::KI_E; case GLFW_KEY_F: return Rml::Input::KI_F;
        case GLFW_KEY_G: return Rml::Input::KI_G; case GLFW_KEY_H: return Rml::Input::KI_H;
        case GLFW_KEY_I: return Rml::Input::KI_I; case GLFW_KEY_J: return Rml::Input::KI_J;
        case GLFW_KEY_K: return Rml::Input::KI_K; case GLFW_KEY_L: return Rml::Input::KI_L;
        case GLFW_KEY_M: return Rml::Input::KI_M; case GLFW_KEY_N: return Rml::Input::KI_N;
        case GLFW_KEY_O: return Rml::Input::KI_O; case GLFW_KEY_P: return Rml::Input::KI_P;
        case GLFW_KEY_Q: return Rml::Input::KI_Q; case GLFW_KEY_R: return Rml::Input::KI_R;
        case GLFW_KEY_S: return Rml::Input::KI_S; case GLFW_KEY_T: return Rml::Input::KI_T;
        case GLFW_KEY_U: return Rml::Input::KI_U; case GLFW_KEY_V: return Rml::Input::KI_V;
        case GLFW_KEY_W: return Rml::Input::KI_W; case GLFW_KEY_X: return Rml::Input::KI_X;
        case GLFW_KEY_Y: return Rml::Input::KI_Y; case GLFW_KEY_Z: return Rml::Input::KI_Z;
        case GLFW_KEY_0: return Rml::Input::KI_0; case GLFW_KEY_1: return Rml::Input::KI_1;
        case GLFW_KEY_2: return Rml::Input::KI_2; case GLFW_KEY_3: return Rml::Input::KI_3;
        case GLFW_KEY_4: return Rml::Input::KI_4; case GLFW_KEY_5: return Rml::Input::KI_5;
        case GLFW_KEY_6: return Rml::Input::KI_6; case GLFW_KEY_7: return Rml::Input::KI_7;
        case GLFW_KEY_8: return Rml::Input::KI_8; case GLFW_KEY_9: return Rml::Input::KI_9;
        case GLFW_KEY_BACKSPACE: return Rml::Input::KI_BACK; case GLFW_KEY_TAB: return Rml::Input::KI_TAB;
        case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: return Rml::Input::KI_RETURN;
        case GLFW_KEY_ESCAPE: return Rml::Input::KI_ESCAPE; case GLFW_KEY_SPACE: return Rml::Input::KI_SPACE;
        case GLFW_KEY_LEFT: return Rml::Input::KI_LEFT; case GLFW_KEY_RIGHT: return Rml::Input::KI_RIGHT;
        case GLFW_KEY_UP: return Rml::Input::KI_UP; case GLFW_KEY_DOWN: return Rml::Input::KI_DOWN;
        case GLFW_KEY_DELETE: return Rml::Input::KI_DELETE; case GLFW_KEY_INSERT: return Rml::Input::KI_INSERT;
        case GLFW_KEY_HOME: return Rml::Input::KI_HOME; case GLFW_KEY_END: return Rml::Input::KI_END;
        case GLFW_KEY_PAGE_UP: return Rml::Input::KI_PRIOR; case GLFW_KEY_PAGE_DOWN: return Rml::Input::KI_NEXT;
        case GLFW_KEY_F1: return Rml::Input::KI_F1; case GLFW_KEY_F2: return Rml::Input::KI_F2;
        case GLFW_KEY_F3: return Rml::Input::KI_F3; case GLFW_KEY_F4: return Rml::Input::KI_F4;
        case GLFW_KEY_F5: return Rml::Input::KI_F5; case GLFW_KEY_F6: return Rml::Input::KI_F6;
        case GLFW_KEY_F7: return Rml::Input::KI_F7; case GLFW_KEY_F8: return Rml::Input::KI_F8;
        case GLFW_KEY_F9: return Rml::Input::KI_F9; case GLFW_KEY_F10: return Rml::Input::KI_F10;
        case GLFW_KEY_F11: return Rml::Input::KI_F11; case GLFW_KEY_F12: return Rml::Input::KI_F12;
        case GLFW_KEY_LEFT_SHIFT: return Rml::Input::KI_LSHIFT; case GLFW_KEY_RIGHT_SHIFT: return Rml::Input::KI_RSHIFT;
        case GLFW_KEY_LEFT_CONTROL: return Rml::Input::KI_LCONTROL; case GLFW_KEY_RIGHT_CONTROL: return Rml::Input::KI_RCONTROL;
        case GLFW_KEY_LEFT_ALT: return Rml::Input::KI_LMENU; case GLFW_KEY_RIGHT_ALT: return Rml::Input::KI_RMENU;
        default: return Rml::Input::KI_UNKNOWN;
    }
}
static int mapMods(int m) {
    int r = 0;
    if (m & GLFW_MOD_SHIFT) r |= Rml::Input::KM_SHIFT;
    if (m & GLFW_MOD_CONTROL) r |= Rml::Input::KM_CTRL;
    if (m & GLFW_MOD_ALT) r |= Rml::Input::KM_ALT;
    return r;
}
} // namespace

bool RmlUI::init(GLFWwindow* window) {
    win = window;
    rsh = new Shader("shaders/rml.vs", "shaders/rml.fs");
    ri = new RIRml(rsh);
    Rml::SetRenderInterface(ri);
    if (!Rml::Initialise()) return false;
    int ww, hh;
    glfwGetFramebufferSize(window, &ww, &hh);
    ctx = Rml::CreateContext("main", Rml::Vector2i(ww, hh));
    if (!ctx) return false;
    curW = ww; curH = hh;
    Rml::LoadFontFace("fonts/Monocraft.ttf");
    pauseDoc = ctx->LoadDocument("ui/pause.rml");
    if (!pauseDoc) return false;
    pauseDoc->AddEventListener("click", &pauseListener);
    pauseDoc->AddEventListener("change", &pauseListener);
    pauseDoc->Hide();
    ok = true;
    return true;
}

void RmlUI::shutdown() {
    if (!ok) return;
    ok = false;
    ctx = nullptr;
    pauseDoc = nullptr;
    Rml::Shutdown();
    delete ri; ri = nullptr;
    delete rsh; rsh = nullptr;
}

void RmlUI::setSize(int w, int h) {
    if (!ok || !ctx || (w == curW && h == curH)) return;
    curW = w; curH = h;
    ctx->SetDimensions(Rml::Vector2i(w, h));
}

void RmlUI::mouseMove(double x, double y) {
    if (!ok || !ctx || !inputActive) return;
    ctx->ProcessMouseMove((int)x, (int)y, 0);
}
void RmlUI::mouseButton(int btn, bool down) {
    if (!ok || !ctx || !inputActive) return;
    if (down) ctx->ProcessMouseButtonDown(btn, 0);
    else ctx->ProcessMouseButtonUp(btn, 0);
}
void RmlUI::mouseWheel(double y) {
    if (!ok || !ctx || !inputActive) return;
    ctx->ProcessMouseWheel((float)-y, 0);
}
void RmlUI::keyEvent(int key, bool down, int mods) {
    if (!ok || !ctx || !inputActive) return;
    Rml::Input::KeyIdentifier k = mapKey(key);
    if (k == Rml::Input::KI_UNKNOWN) return;
    if (down) ctx->ProcessKeyDown(k, mapMods(mods));
    else ctx->ProcessKeyUp(k, mapMods(mods));
}
void RmlUI::textInput(unsigned int cp) {
    if (!ok || !ctx || !inputActive) return;
    ctx->ProcessTextInput((Rml::Character)cp);
}

void RmlUI::setScale(float s) {
    if (!ok || !ctx || s == curScale) return;
    if (s < 0.5f) s = 0.5f;
    if (s > 4.0f) s = 4.0f;
    curScale = s;
    ctx->SetDensityIndependentPixelRatio(s);
}

void RmlUI::showPause(bool show) {
    if (!ok || !pauseDoc || show == pauseShown) return;
    pauseShown = show;
    if (show) { syncPauseValues(); pauseDoc->Show(); }
    else pauseDoc->Hide();
}

void RmlUI::syncPauseValues() {
    if (!ok || !pauseDoc || !getSlider) return;
    const char* ids[] = {"s_fov", "s_gamma", "s_fog"};
    for (auto id : ids) {
        Rml::Element* el = pauseDoc->GetElementById(id);
        auto* fc = static_cast<Rml::ElementFormControl*>(el);
        if (!fc) continue;
        char buf[32];
        snprintf(buf, sizeof(buf), "%g", (double)getSlider(id));
        fc->SetValue(Rml::String(buf));
        Rml::String vid = Rml::String("v_") + Rml::String(id).substr(2);
        Rml::Element* sp = pauseDoc->GetElementById(vid);
        if (sp) sp->SetInnerRML(Rml::String(buf));
    }
    if (!getLabel) return;
    const char* bids[] = {"b_dist", "b_filter", "b_vsync", "b_res", "b_fs", "b_aa", "b_gscale"};
    for (auto id : bids) {
        Rml::Element* el = pauseDoc->GetElementById(id);
        if (el) el->SetInnerRML(Rml::String(getLabel(id).c_str()));
    }
}

void RmlUI::frame() {
    if (!ok || !ctx) return;
    int ww, hh;
    glfwGetFramebufferSize(win, &ww, &hh);
    // бэкап GL-состояния (после нас рисуют ImGui-консоль и следующий кадр)
    GLint prog, vao, abuf, ebuf, tex, active, sRgb, dRgb, sA, dA, vp[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    GLboolean blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST), sciss = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_BLEND_SRC_RGB, &sRgb); glGetIntegerv(GL_BLEND_DST_RGB, &dRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &sA); glGetIntegerv(GL_BLEND_DST_ALPHA, &dA);
    glGetIntegerv(GL_VIEWPORT, vp);
    ctx->Update();
    glViewport(0, 0, ww, hh);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // вершины RmlUi premultiplied
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    ri->beginFrame(ww, hh);
    ctx->Render();
    glUseProgram(prog);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, abuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebuf);
    glActiveTexture(active);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate(sRgb, dRgb, sA, dA);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (sciss) glEnable(GL_SCISSOR_TEST);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

void gRmlScroll(double, double y) { gRml.mouseWheel(y); }
void gRmlKey(int key, int action, int mods) {
    if (action == 2) gRml.keyEvent(key, true, mods); // repeat как down
    else gRml.keyEvent(key, action == 1, mods);
}
void gRmlChar(unsigned int cp) { gRml.textInput(cp); }
