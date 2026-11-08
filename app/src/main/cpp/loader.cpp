// libcustomloader.so  (armeabi-v7a)
// Lapisan 2-5: hook EGL/GLES + ImGui + post-process shader Java (composite*/final).
#include <jni.h>
#include <android/log.h>
#include <android/input.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "shadowhook.h"
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "glsl_translate.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "LOADER", __VA_ARGS__)

// ---------------------------------------------------------------- state
static JavaVM* g_vm; static jclass g_bridge; static jmethodID g_mImport;
static std::string g_root, g_active, g_status;
static std::vector<std::string> g_packs;
static std::atomic<bool> g_menu{false};
static std::string g_toLoad; static bool g_disable = false;
static EGLContext g_ctx = EGL_NO_CONTEXT; static EGLSurface g_surf = EGL_NO_SURFACE;
static bool g_glReady = false, g_imgui = false; static unsigned g_frame = 0;

struct Pass { GLuint prog; std::string name; };
static std::vector<Pass> g_passes;
static GLuint g_tex[2], g_fbo[2], g_vao, g_vbo, g_black, g_white, g_noise, g_smpLin, g_smpNear, g_smpRep;
static int g_tw = 0, g_th = 0;

// UI params
static float g_fov = 70.f, g_tod = 0.30f; static bool g_useDepth = true; static int g_depthIdx = 0;
static float g_rect[4] = {0, 0, 0, 0}; static bool g_cap = false;

// tangkapan dari game
static std::vector<GLuint> g_depthCands;
static float g_viewM[16], g_projM[16]; static bool g_haveView = false, g_haveProj = false;
static std::unordered_map<uint64_t, int> g_matKind; static std::unordered_set<GLuint> g_scanned;
static std::unordered_set<std::string> g_seenNames;

static bool exists(const std::string& p) { struct stat s; return stat(p.c_str(), &s) == 0; }

// ---------------------------------------------------------------- GL state save/restore
struct GLState {
    GLint prog, vao, abuf, fd, fr, vp[4], atex, tex[4], smp[4]; GLboolean en[5], cm[4];
    static constexpr GLenum caps[5] = {GL_BLEND, GL_DEPTH_TEST, GL_CULL_FACE, GL_SCISSOR_TEST, GL_STENCIL_TEST};
    void save() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &prog); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fd); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fr);
        glGetIntegerv(GL_VIEWPORT, vp); glGetIntegerv(GL_ACTIVE_TEXTURE, &atex);
        for (int i = 0; i < 4; i++) { glActiveTexture(GL_TEXTURE0 + i); glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex[i]); glGetIntegerv(GL_SAMPLER_BINDING, &smp[i]); }
        for (int i = 0; i < 5; i++) en[i] = glIsEnabled(caps[i]);
        glGetBooleanv(GL_COLOR_WRITEMASK, cm);
    }
    void restore() {
        for (int i = 0; i < 4; i++) { glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, tex[i]); glBindSampler(i, smp[i]); }
        glActiveTexture(atex);
        glUseProgram(prog); glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, abuf);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fd); glBindFramebuffer(GL_READ_FRAMEBUFFER, fr);
        glViewport(vp[0], vp[1], vp[2], vp[3]);
        for (int i = 0; i < 5; i++) en[i] ? glEnable(caps[i]) : glDisable(caps[i]);
        glColorMask(cm[0], cm[1], cm[2], cm[3]);
    }
};

// ---------------------------------------------------------------- matematika kecil
static void perspective(float* m, float fovDeg, float asp, float n, float f) {
    memset(m, 0, 64); float t = 1.f / tanf(fovDeg * 0.5f * 3.14159265f / 180.f);
    m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f);
}
static bool inv4(const float* m, float* o) {
    double a[4][8];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { a[i][j] = m[i * 4 + j]; a[i][j + 4] = i == j; }
    for (int c = 0; c < 4; c++) {
        int p = c; for (int r = c + 1; r < 4; r++) if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
        if (fabs(a[p][c]) < 1e-12) return false;
        for (int k = 0; k < 8; k++) std::swap(a[c][k], a[p][k]);
        double d = a[c][c]; for (int k = 0; k < 8; k++) a[c][k] /= d;
        for (int r = 0; r < 4; r++) if (r != c) { double f = a[r][c]; for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k]; }
    }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) o[i * 4 + j] = (float)a[i][j + 4];
    return true;
}

// ---------------------------------------------------------------- resource GL
static GLuint mkTex(int w, int h, const void* px) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
static GLuint mkSampler(GLenum filt, GLenum wrap) {
    GLuint s; glGenSamplers(1, &s);
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, filt); glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, filt);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, wrap); glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wrap); return s;
}
static void glInit() {
    GLState st; st.save(); glActiveTexture(GL_TEXTURE0);
    unsigned char b[4] = {0, 0, 0, 255}, w[4] = {255, 255, 255, 255};
    g_black = mkTex(1, 1, b); g_white = mkTex(1, 1, w);
    std::vector<unsigned char> n(256 * 256 * 4); uint32_t s = 12345; for (auto& v : n) { s = s * 1664525u + 1013904223u; v = s >> 24; }
    g_noise = mkTex(256, 256, n.data());
    g_smpLin = mkSampler(GL_LINEAR, GL_CLAMP_TO_EDGE); g_smpNear = mkSampler(GL_NEAREST, GL_CLAMP_TO_EDGE); g_smpRep = mkSampler(GL_NEAREST, GL_REPEAT);
    float tri[6] = {-1, -1, 3, -1, -1, 3};
    glGenVertexArrays(1, &g_vao); glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo); glBindBuffer(GL_ARRAY_BUFFER, g_vbo); glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    st.restore();
}
static void ensureTargets(int w, int h) {
    if (w == g_tw && h == g_th) return;
    if (g_tw) { glDeleteTextures(2, g_tex); glDeleteFramebuffers(2, g_fbo); }
    for (int i = 0; i < 2; i++) {
        g_tex[i] = mkTex(w, h, nullptr);
        glGenFramebuffers(1, &g_fbo[i]); glBindFramebuffer(GL_FRAMEBUFFER, g_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex[i], 0);
    }
    g_tw = w; g_th = h;
}

// ---------------------------------------------------------------- shader pack
static GLuint compile(GLenum t, const std::string& src, std::string& log) {
    GLuint s = glCreateShader(t); const char* c = src.c_str(); glShaderSource(s, 1, &c, nullptr); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[1500]; glGetShaderInfoLog(s, sizeof buf, nullptr, buf); log += buf; glDeleteShader(s); return 0; }
    return s;
}
static void clearPasses() { for (auto& p : g_passes) glDeleteProgram(p.prog); g_passes.clear(); }
static const char* kDefVert = "#version 300 es\nprecision highp float;\nlayout(location=0) in vec2 _pos;\nout vec4 _tc0;\n"
                              "void main(){ _tc0=vec4(_pos*0.5+0.5,0.0,1.0); gl_Position=vec4(_pos,0.0,1.0); }\n";
static void loadPack(const std::string& name) {
    clearPasses(); g_active = name; g_status.clear();
    std::string dir = g_root + "/" + name;
    std::vector<std::string> stages = {"composite"};
    for (int i = 1; i < 16; i++) stages.push_back("composite" + std::to_string(i));
    stages.push_back("final");
    for (auto& st : stages) {
        std::string fp = dir + "/" + st + ".fsh"; if (!exists(fp)) continue;
        std::string vp = dir + "/" + st + ".vsh";
        std::string fs = tr::translate(tr::expand(dir, fp), false);
        std::string vs = exists(vp) ? tr::translate(tr::expand(dir, vp), true) : std::string(kDefVert);
        std::string log; GLuint v = compile(GL_VERTEX_SHADER, vs, log), f = compile(GL_FRAGMENT_SHADER, fs, log);
        if (!v || !f) { g_status += st + ": GAGAL compile\n" + log + "\n"; continue; }
        GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glBindAttribLocation(p, 0, "_pos"); glLinkProgram(p);
        glDeleteShader(v); glDeleteShader(f);
        GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) { char buf[1500]; glGetProgramInfoLog(p, sizeof buf, nullptr, buf); g_status += st + ": GAGAL link\n" + std::string(buf) + "\n"; glDeleteProgram(p); continue; }
        g_passes.push_back({p, st});
    }
    g_status = g_passes.empty() ? "Tidak ada pass yang valid (butuh composite*.fsh / final.fsh)\n" + g_status
                                : "OK: " + std::to_string(g_passes.size()) + " pass\n" + g_status;
    LOG("pack %s: %s", name.c_str(), g_status.c_str());
}
static void scanPacks() {
    g_packs.clear(); DIR* d = opendir(g_root.c_str()); if (!d) return;
    while (auto* e = readdir(d)) if (e->d_name[0] != '.') g_packs.push_back(e->d_name);
    closedir(d);
}

// ---------------------------------------------------------------- uniform
static void u1f(GLuint p, const char* n, float v) { GLint l = glGetUniformLocation(p, n); if (l >= 0) glUniform1f(l, v); }
static void u1i(GLuint p, const char* n, int v) { GLint l = glGetUniformLocation(p, n); if (l >= 0) glUniform1i(l, v); }
static void u3f(GLuint p, const char* n, const float* v) { GLint l = glGetUniformLocation(p, n); if (l >= 0) glUniform3fv(l, 1, v); }
static void um4(GLuint p, const char* n, const float* v) { GLint l = glGetUniformLocation(p, n); if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, v); }

static void setUniforms(GLuint p, int w, int h) {
    static auto t0 = std::chrono::steady_clock::now();
    float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    float proj[16], view[16], ip[16], iv[16];
    if (g_haveProj) memcpy(proj, g_projM, 64); else perspective(proj, g_fov, (float)w / h, 0.05f, 512.f);
    if (g_haveView) memcpy(view, g_viewM, 64); else { memset(view, 0, 64); view[0] = view[5] = view[10] = view[15] = 1; }
    if (!inv4(proj, ip)) memcpy(ip, proj, 64); if (!inv4(view, iv)) memcpy(iv, view, 64);
    float a = g_tod * 6.2831853f, sd[3] = {cosf(a), sinf(a), 0.f};   // arah matahari (dunia): 0=terbit, 0.25=siang
    auto rot = [&](const float* d, float* o) { for (int i = 0; i < 3; i++) o[i] = 100.f * (view[i] * d[0] + view[4 + i] * d[1] + view[8 + i] * d[2]); };
    float sun[3], moon[3]; rot(sd, sun); for (int i = 0; i < 3; i++) moon[i] = -sun[i];
    float cam[3] = {0, 64, 0};
    u1f(p, "viewWidth", w); u1f(p, "viewHeight", h); u1f(p, "aspectRatio", (float)w / h);
    u1f(p, "near", 0.05f); u1f(p, "far", 512.f); u1f(p, "frameTimeCounter", t); u1i(p, "frameCounter", g_frame);
    u1f(p, "rainStrength", 0); u1f(p, "wetness", 0); u1i(p, "worldTime", (int)(g_tod * 24000)); u1f(p, "sunAngle", g_tod);
    u3f(p, "sunPosition", sun); u3f(p, "moonPosition", moon); u3f(p, "shadowLightPosition", g_tod < 0.5f ? sun : moon);
    u3f(p, "cameraPosition", cam); u3f(p, "previousCameraPosition", cam);
    um4(p, "gbufferProjection", proj); um4(p, "gbufferProjectionInverse", ip); um4(p, "gbufferPreviousProjection", proj);
    um4(p, "gbufferModelView", view); um4(p, "gbufferModelViewInverse", iv); um4(p, "gbufferPreviousModelView", view);
    // unit: 0 warna scene | 1 depth | 2 hitam (colortex1-7, shadow*, dll belum didukung) | 3 noise
    static const char* u0[] = {"colortex0", "gcolor", nullptr};
    static const char* u1[] = {"depthtex0", "depthtex1", "depthtex2", "gdepthtex", nullptr};
    static const char* u2[] = {"colortex1", "colortex2", "colortex3", "colortex4", "colortex5", "colortex6", "colortex7", "gdepth", "gnormal",
                               "composite", "gaux1", "gaux2", "gaux3", "gaux4", "shadowtex0", "shadowtex1", "shadowcolor0", "shadowcolor1", nullptr};
    for (auto n = u0; *n; n++) u1i(p, *n, 0);
    for (auto n = u1; *n; n++) u1i(p, *n, 1);
    for (auto n = u2; *n; n++) u1i(p, *n, 2);
    u1i(p, "noisetex", 3);
}

static void runPostFX(int w, int h) {
    if (g_passes.empty()) return;
    GLState st; st.save(); ensureTargets(w, h);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
    glColorMask(1, 1, 1, 1); glViewport(0, 0, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_tex[0]); glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    GLuint depth = (g_useDepth && !g_depthCands.empty()) ? g_depthCands[g_depthIdx % g_depthCands.size()] : g_white;
    glBindVertexArray(g_vao);
    int src = 0;
    for (size_t i = 0; i < g_passes.size(); i++) {
        bool last = i + 1 == g_passes.size();
        glBindFramebuffer(GL_FRAMEBUFFER, last ? 0 : g_fbo[1 - src]);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_tex[src]); glBindSampler(0, g_smpLin);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, depth); glBindSampler(1, g_smpNear);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_black); glBindSampler(2, g_smpLin);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, g_noise); glBindSampler(3, g_smpRep);
        glUseProgram(g_passes[i].prog); setUniforms(g_passes[i].prog, w, h);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        src = 1 - src;
    }
    st.restore();
}

// ---------------------------------------------------------------- ImGui
static void callImport() {
    JNIEnv* env; bool att = false;
    if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) { g_vm->AttachCurrentThread(&env, nullptr); att = true; }
    env->CallStaticVoidMethod(g_bridge, g_mImport); if (att) g_vm->DetachCurrentThread();
}
static void imguiInit() {
    if (!ImGui::GetCurrentContext()) {
        ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr;
        ImGui::StyleColorsDark(); ImGui::GetStyle().ScaleAllSizes(2.f); io.FontGlobalScale = 2.f;
    }
    ImGui_ImplOpenGL3_Init("#version 300 es");
}
static void drawMenu(int w, int h) {
    static unsigned lastScan = 0; if (g_frame - lastScan > 60 || g_packs.empty()) { scanPacks(); lastScan = g_frame; }
    ImGui::GetIO().DisplaySize = ImVec2((float)w, (float)h);
    ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(w * 0.12f, h * 0.05f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(w * 0.55f, h * 0.85f), ImGuiCond_FirstUseEver);
    ImGui::Begin("JSLOADER");
    ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize(); g_rect[0] = p.x; g_rect[1] = p.y; g_rect[2] = sz.x; g_rect[3] = sz.y;
    if (ImGui::Button("Impor Shader ZIP", ImVec2(-1, 0))) callImport();
    ImGui::Separator(); ImGui::Text("ZIP yang sudah diimpor:");
    for (auto& n : g_packs) if (ImGui::Selectable(n.c_str(), n == g_active)) g_toLoad = n;
    if (ImGui::Button("Matikan shader")) g_disable = true;
    ImGui::Separator();
    ImGui::SliderFloat("FOV", &g_fov, 30, 110); ImGui::SliderFloat("Waktu (0=terbit .25=siang)", &g_tod, 0, 1);
    ImGui::Checkbox("Pakai depth", &g_useDepth);
    ImGui::Text("Depth kandidat: %d (aktif #%d)", (int)g_depthCands.size(), g_depthIdx);
    if (ImGui::Button("Depth berikutnya")) g_depthIdx++;
    ImGui::Text("Matriks game: view=%d proj=%d", g_haveView, g_haveProj);
    ImGui::Separator(); ImGui::TextWrapped("%s", g_status.c_str());
    ImGui::Separator(); ImGui::TextDisabled("Credit");
    ImGui::TextWrapped("YouTube : youtube.com/@xeziez");
    ImGui::TextWrapped("TikTok : tiktok.com/@initial_a.n");
    ImGui::TextWrapped("GitHub : github.com/rintaga");
    ImGui::End(); ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ---------------------------------------------------------------- hook
static EGLBoolean (*orig_swap)(EGLDisplay, EGLSurface);
static EGLBoolean (*orig_swapD)(EGLDisplay, EGLSurface, EGLint*, EGLint);
static void (*orig_fbtex)(GLenum, GLenum, GLenum, GLuint, GLint);
static void (*orig_umat)(GLint, GLsizei, GLboolean, const GLfloat*);
static int32_t (*orig_getEvent)(AInputQueue*, AInputEvent**);

static void frame(EGLDisplay d, EGLSurface s) {
    EGLContext ctx = eglGetCurrentContext(); if (ctx == EGL_NO_CONTEXT) return;
    EGLint w = 0, h = 0; eglQuerySurface(d, s, EGL_WIDTH, &w); eglQuerySurface(d, s, EGL_HEIGHT, &h);
    if (w < 200 || h < 200) return;
    if (g_surf != EGL_NO_SURFACE && s != g_surf && ctx == g_ctx) return;   // hanya surface game
    g_surf = s;
    if (ctx != g_ctx) {   // context baru (resume/recreate): reset resource
        if (g_imgui) { ImGui_ImplOpenGL3_Shutdown(); g_imgui = false; }
        g_passes.clear(); g_glReady = false; g_tw = g_th = 0; g_ctx = ctx;
        g_depthCands.clear(); g_scanned.clear(); g_matKind.clear(); if (!g_active.empty()) g_toLoad = g_active;
    }
    if (!g_glReady) { glInit(); g_glReady = true; }
    if (!g_imgui) { imguiInit(); g_imgui = true; }
    if (g_disable) { g_disable = false; clearPasses(); g_active.clear(); g_status = "Shader dimatikan"; }
    if (!g_toLoad.empty()) { std::string n = g_toLoad; g_toLoad.clear(); loadPack(n); }
    g_frame++;
    runPostFX(w, h);
    if (g_menu) drawMenu(w, h);
}
static EGLBoolean my_swap(EGLDisplay d, EGLSurface s) { frame(d, s); return orig_swap(d, s); }
static EGLBoolean my_swapD(EGLDisplay d, EGLSurface s, EGLint* r, EGLint n) { frame(d, s); return orig_swapD(d, s, r, n); }

// kandidat depth texture: setiap depth attachment yang dipasang game
static void my_fbtex(GLenum tg, GLenum att, GLenum ttg, GLuint tex, GLint lv) {
    if ((att == GL_DEPTH_ATTACHMENT || att == GL_DEPTH_STENCIL_ATTACHMENT) && tex) {
        bool have = false; for (auto t : g_depthCands) have |= (t == tex);
        if (!have) { if (g_depthCands.size() >= 16) g_depthCands.erase(g_depthCands.begin()); g_depthCands.push_back(tex); }
    }
    orig_fbtex(tg, att, ttg, tex, lv);
}

// tangkap matriks view/proj dari uniform game (nama dicatat di logcat tag LOADER)
static const char* kViewN[] = {"u_view", "View", "u_viewMatrix", "VIEW", nullptr};
static const char* kProjN[] = {"u_proj", "Proj", "u_projMatrix", "PROJ", nullptr};
static bool inList(const std::string& s, const char** l) { for (; *l; l++) if (s == *l) return true; return false; }
static void scanProgram(GLuint p) {
    GLint n = 0; glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &n);
    for (GLint i = 0; i < n; i++) {
        char nm[96]; GLsizei len; GLint sz; GLenum ty; glGetActiveUniform(p, i, sizeof nm, &len, &sz, &ty, nm);
        if (ty != GL_FLOAT_MAT4) continue;
        std::string s(nm); auto b = s.find('['); if (b != std::string::npos) s = s.substr(0, b);
        GLint loc = glGetUniformLocation(p, nm);
        g_matKind[((uint64_t)p << 32) | (uint32_t)loc] = inList(s, kViewN) ? 1 : inList(s, kProjN) ? 2 : 0;
        if (g_seenNames.insert(s).second) LOG("mat4 uniform: %s", s.c_str());
    }
}
static void my_umat(GLint loc, GLsizei cnt, GLboolean tp, const GLfloat* v) {
    if (cnt == 1 && !tp && g_imgui) {
        GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        if (prog && !g_scanned.count(prog)) { g_scanned.insert(prog); scanProgram(prog); }
        auto it = g_matKind.find(((uint64_t)prog << 32) | (uint32_t)loc);
        if (it != g_matKind.end()) {
            if (it->second == 2 && fabsf(v[15]) < 1e-4f) { memcpy(g_projM, v, 64); g_haveProj = true; }        // hanya perspektif
            if (it->second == 1 && fabsf(v[3]) + fabsf(v[7]) + fabsf(v[11]) < 1e-4f) { memcpy(g_viewM, v, 64); g_haveView = true; }
        }
    }
    orig_umat(loc, cnt, tp, v);
}

// sentuhan: kalau menu terbuka & jari mulai di dalam jendela ImGui, event tidak diteruskan ke game
static int32_t my_getEvent(AInputQueue* q, AInputEvent** ev) {
    int32_t r = orig_getEvent(q, ev);
    if (r >= 0 && *ev && g_menu && g_imgui && AInputEvent_getType(*ev) == AINPUT_EVENT_TYPE_MOTION) {
        int a = AMotionEvent_getAction(*ev) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(*ev, 0), y = AMotionEvent_getY(*ev, 0);
        if (a == AMOTION_EVENT_ACTION_DOWN)
            g_cap = x >= g_rect[0] && x <= g_rect[0] + g_rect[2] && y >= g_rect[1] && y <= g_rect[1] + g_rect[3];
        if (g_cap) {
            ImGuiIO& io = ImGui::GetIO(); io.AddMousePosEvent(x, y);
            if (a == AMOTION_EVENT_ACTION_DOWN) io.AddMouseButtonEvent(0, true);
            if (a == AMOTION_EVENT_ACTION_UP || a == AMOTION_EVENT_ACTION_CANCEL) { io.AddMouseButtonEvent(0, false); g_cap = false; }
            AInputQueue_finishEvent(q, *ev, 1); *ev = nullptr; return -1;
        }
    }
    return r;
}

static void installHooks() {
    shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    dlopen("libEGL.so", RTLD_NOW); dlopen("libGLESv2.so", RTLD_NOW); dlopen("libandroid.so", RTLD_NOW);
    LOG("hook swap=%p swapD=%p fbtex=%p umat=%p input=%p",
        shadowhook_hook_sym_name("libEGL.so", "eglSwapBuffers", (void*)my_swap, (void**)&orig_swap),
        shadowhook_hook_sym_name("libEGL.so", "eglSwapBuffersWithDamageKHR", (void*)my_swapD, (void**)&orig_swapD),
        shadowhook_hook_sym_name("libGLESv2.so", "glFramebufferTexture2D", (void*)my_fbtex, (void**)&orig_fbtex),
        shadowhook_hook_sym_name("libGLESv2.so", "glUniformMatrix4fv", (void*)my_umat, (void**)&orig_umat),
        shadowhook_hook_sym_name("libandroid.so", "AInputQueue_getEvent", (void*)my_getEvent, (void**)&orig_getEvent));
}

// ---------------------------------------------------------------- JNI
extern "C" {
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm; JNIEnv* env; vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    jclass c = env->FindClass("com/loader/app/Bridge");
    g_bridge = (jclass)env->NewGlobalRef(c); g_mImport = env->GetStaticMethodID(c, "requestImport", "()V");
    installHooks();
    return JNI_VERSION_1_6;
}
JNIEXPORT void JNICALL Java_com_loader_app_Native_init(JNIEnv* e, jclass, jstring dir) {
    const char* c = e->GetStringUTFChars(dir, nullptr); g_root = c; e->ReleaseStringUTFChars(dir, c);
}
JNIEXPORT void JNICALL Java_com_loader_app_Native_toggleMenu(JNIEnv*, jclass) { g_menu = !g_menu; }
}
