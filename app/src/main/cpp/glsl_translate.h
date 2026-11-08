#pragma once
// Lapisan 4: GLSL desktop (Optifine/Iris, #version 120-330) -> GLSL ES 3.00 untuk pass composite/final.
#include <string>
#include <fstream>
#include <sstream>
#include <regex>

namespace tr {
inline bool readFile(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true;
}
inline std::string dirOf(const std::string& p) { auto i = p.find_last_of('/'); return i == std::string::npos ? "" : p.substr(0, i); }

// #include "/lib/x.glsl" (absolut dari folder shaders) atau relatif
inline std::string expand(const std::string& root, const std::string& file, int depth = 0) {
    std::string src; if (depth > 8 || !readFile(file, src)) return "";
    std::stringstream in(src), out; std::string line;
    while (std::getline(in, line)) {
        size_t a = line.find_first_not_of(" \t");
        if (a != std::string::npos && line.compare(a, 8, "#include") == 0) {
            size_t q1 = line.find('"'), q2 = line.rfind('"');
            if (q1 != std::string::npos && q2 > q1) {
                std::string inc = line.substr(q1 + 1, q2 - q1 - 1);
                out << expand(root, inc[0] == '/' ? root + inc : dirOf(file) + "/" + inc, depth + 1) << "\n"; continue;
            }
        }
        out << line << "\n";
    }
    return out.str();
}

inline std::string rep(const std::string& s, const char* re, const char* to) { return std::regex_replace(s, std::regex(re), to); }

inline std::string translate(std::string s, bool vert) {
    s = rep(s, "\r", "");
    s = rep(s, "[ \\t]*#[ \\t]*(version|extension)[^\n]*", "");
    s = rep(s, "gl_TexCoord\\s*\\[\\s*0\\s*\\]", "_tc0");
    s = rep(s, "\\bvarying\\b", vert ? "out" : "in");
    s = rep(s, "\\bgl_Vertex\\b", "vec4(_pos,0.0,1.0)");
    s = rep(s, "\\bgl_MultiTexCoord0\\b", "vec4(_pos*0.5+0.5,0.0,1.0)");
    s = rep(s, "\\bftransform\\s*\\(\\s*\\)", "vec4(_pos,0.0,1.0)");
    s = rep(s, "\\bgl_TextureMatrix\\s*\\[\\s*0\\s*\\]", "mat4(1.0)");
    s = rep(s, "\\bgl_(ModelViewProjectionMatrix|ModelViewMatrix|ProjectionMatrix)\\b", "mat4(1.0)");
    s = rep(s, "\\bgl_NormalMatrix\\b", "mat3(1.0)");
    s = rep(s, "\\bgl_Color\\b", "vec4(1.0)");
    s = rep(s, "\\bgl_Normal\\b", "vec3(0.0,0.0,1.0)");
    s = rep(s, "\\btexture2DLod\\b", "textureLod");
    s = rep(s, "\\btexture2DProj\\b", "textureProj");
    s = rep(s, "\\btexture2D\\b", "texture");
    s = rep(s, "void\\s+main\\s*\\(\\s*(void)?\\s*\\)", "void _user_main()");
    if (vert)
        return "#version 300 es\nprecision highp float;\nprecision highp int;\n"
               "layout(location=0) in vec2 _pos;\nout vec4 _tc0;\n" + s +
               "\nvoid main(){ _tc0=vec4(_pos*0.5+0.5,0.0,1.0); _user_main(); }\n";
    bool fd = s.find("gl_FragData") != std::string::npos;
    s = rep(s, "gl_FragData\\s*\\[\\s*([0-9])\\s*\\]", "_fd[$1]");
    s = rep(s, "\\bgl_FragColor\\b", "_fragColor");
    return "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
           "in vec4 _tc0;\nlayout(location=0) out vec4 _fragColor;\nvec4 _fd[8];\n" + s +
           std::string("\nvoid main(){ _user_main(); ") + (fd ? "_fragColor=_fd[0];" : "") + " }\n";
}
} // namespace tr
