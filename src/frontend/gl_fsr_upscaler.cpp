#include "gl_fsr_upscaler.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif

namespace emu::frontend {
namespace {

template <typename T>
bool loadGlFunction(T& function, const char* name, std::string& error) {
    function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    if (function) return true;
    error = std::string("Missing OpenGL function: ") + name;
    return false;
}

std::string readTextFile(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Could not open shader '" + path + "'";
        return {};
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

} // namespace

struct FsrUpscaler::Impl {
    using CreateShaderFn = GLuint (APIENTRY *)(GLenum);
    using ShaderSourceFn = void (APIENTRY *)(GLuint, GLsizei, const char* const*, const GLint*);
    using CompileShaderFn = void (APIENTRY *)(GLuint);
    using GetShaderivFn = void (APIENTRY *)(GLuint, GLenum, GLint*);
    using GetShaderInfoLogFn = void (APIENTRY *)(GLuint, GLsizei, GLsizei*, char*);
    using DeleteShaderFn = void (APIENTRY *)(GLuint);
    using CreateProgramFn = GLuint (APIENTRY *)();
    using AttachShaderFn = void (APIENTRY *)(GLuint, GLuint);
    using LinkProgramFn = void (APIENTRY *)(GLuint);
    using GetProgramivFn = void (APIENTRY *)(GLuint, GLenum, GLint*);
    using GetProgramInfoLogFn = void (APIENTRY *)(GLuint, GLsizei, GLsizei*, char*);
    using DeleteProgramFn = void (APIENTRY *)(GLuint);
    using UseProgramFn = void (APIENTRY *)(GLuint);
    using GetUniformLocationFn = GLint (APIENTRY *)(GLuint, const char*);
    using Uniform1iFn = void (APIENTRY *)(GLint, GLint);
    using Uniform2fFn = void (APIENTRY *)(GLint, GLfloat, GLfloat);
    using Uniform4fFn = void (APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    using ActiveTextureFn = void (APIENTRY *)(GLenum);
    using GenFramebuffersFn = void (APIENTRY *)(GLsizei, GLuint*);
    using BindFramebufferFn = void (APIENTRY *)(GLenum, GLuint);
    using FramebufferTexture2DFn = void (APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
    using CheckFramebufferStatusFn = GLenum (APIENTRY *)(GLenum);
    using DeleteFramebuffersFn = void (APIENTRY *)(GLsizei, const GLuint*);

    CreateShaderFn create_shader = nullptr;
    ShaderSourceFn shader_source = nullptr;
    CompileShaderFn compile_shader = nullptr;
    GetShaderivFn get_shader_iv = nullptr;
    GetShaderInfoLogFn get_shader_log = nullptr;
    DeleteShaderFn delete_shader = nullptr;
    CreateProgramFn create_program = nullptr;
    AttachShaderFn attach_shader = nullptr;
    LinkProgramFn link_program = nullptr;
    GetProgramivFn get_program_iv = nullptr;
    GetProgramInfoLogFn get_program_log = nullptr;
    DeleteProgramFn delete_program = nullptr;
    UseProgramFn use_program = nullptr;
    GetUniformLocationFn get_uniform_location = nullptr;
    Uniform1iFn uniform_1i = nullptr;
    Uniform2fFn uniform_2f = nullptr;
    Uniform4fFn uniform_4f = nullptr;
    ActiveTextureFn active_texture = nullptr;
    GenFramebuffersFn gen_framebuffers = nullptr;
    BindFramebufferFn bind_framebuffer = nullptr;
    FramebufferTexture2DFn framebuffer_texture_2d = nullptr;
    CheckFramebufferStatusFn check_framebuffer_status = nullptr;
    DeleteFramebuffersFn delete_framebuffers = nullptr;

    SDL_Window* window = nullptr;
    GLuint input_texture = 0;
    GLuint easu_texture = 0;
    GLuint easu_framebuffer = 0;
    GLuint easu_program = 0;
    GLuint rcas_program = 0;
    int input_width = 0;
    int input_height = 0;
    int output_width = 0;
    int output_height = 0;

    bool loadFunctions(std::string& error) {
#define LOAD_GL(member, symbol) \
        if (!loadGlFunction(member, symbol, error)) return false
        LOAD_GL(create_shader, "glCreateShader");
        LOAD_GL(shader_source, "glShaderSource");
        LOAD_GL(compile_shader, "glCompileShader");
        LOAD_GL(get_shader_iv, "glGetShaderiv");
        LOAD_GL(get_shader_log, "glGetShaderInfoLog");
        LOAD_GL(delete_shader, "glDeleteShader");
        LOAD_GL(create_program, "glCreateProgram");
        LOAD_GL(attach_shader, "glAttachShader");
        LOAD_GL(link_program, "glLinkProgram");
        LOAD_GL(get_program_iv, "glGetProgramiv");
        LOAD_GL(get_program_log, "glGetProgramInfoLog");
        LOAD_GL(delete_program, "glDeleteProgram");
        LOAD_GL(use_program, "glUseProgram");
        LOAD_GL(get_uniform_location, "glGetUniformLocation");
        LOAD_GL(uniform_1i, "glUniform1i");
        LOAD_GL(uniform_2f, "glUniform2f");
        LOAD_GL(uniform_4f, "glUniform4f");
        LOAD_GL(active_texture, "glActiveTexture");
        LOAD_GL(gen_framebuffers, "glGenFramebuffers");
        LOAD_GL(bind_framebuffer, "glBindFramebuffer");
        LOAD_GL(framebuffer_texture_2d, "glFramebufferTexture2D");
        LOAD_GL(check_framebuffer_status, "glCheckFramebufferStatus");
        LOAD_GL(delete_framebuffers, "glDeleteFramebuffers");
#undef LOAD_GL
        return true;
    }

    GLuint compile(GLenum type, const std::string& source, std::string& error) {
        const GLuint shader = create_shader(type);
        if (!shader) {
            error = "glCreateShader failed";
            return 0;
        }

        const char* source_ptr = source.c_str();
        shader_source(shader, 1, &source_ptr, nullptr);
        compile_shader(shader);

        GLint success = GL_FALSE;
        get_shader_iv(shader, GL_COMPILE_STATUS, &success);
        if (success == GL_TRUE) return shader;

        GLint log_length = 0;
        get_shader_iv(shader, GL_INFO_LOG_LENGTH, &log_length);
        std::string log(static_cast<size_t>(std::max(log_length, 1)), '\0');
        get_shader_log(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        error = "Shader compilation failed: " + log;
        delete_shader(shader);
        return 0;
    }

    GLuint makeProgram(const std::string& vertex_source,
                       const std::string& fragment_source,
                       std::string& error) {
        const GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source, error);
        if (!vertex) return 0;
        const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source, error);
        if (!fragment) {
            delete_shader(vertex);
            return 0;
        }

        const GLuint program = create_program();
        if (program) {
            attach_shader(program, vertex);
            attach_shader(program, fragment);
            link_program(program);
        }
        delete_shader(vertex);
        delete_shader(fragment);

        if (!program) {
            error = "glCreateProgram failed";
            return 0;
        }

        GLint success = GL_FALSE;
        get_program_iv(program, GL_LINK_STATUS, &success);
        if (success == GL_TRUE) return program;

        GLint log_length = 0;
        get_program_iv(program, GL_INFO_LOG_LENGTH, &log_length);
        std::string log(static_cast<size_t>(std::max(log_length, 1)), '\0');
        get_program_log(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        error = "Shader program link failed: " + log;
        delete_program(program);
        return 0;
    }

    bool ensureInputTexture(int width, int height, const uint32_t* pixels) {
        if (!input_texture) glGenTextures(1, &input_texture);
        glBindTexture(GL_TEXTURE_2D, input_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        if (width != input_width || height != input_height) {
            input_width = width;
            input_height = height;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, pixels);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                            GL_BGRA, GL_UNSIGNED_BYTE, pixels);
        }
        return glGetError() == GL_NO_ERROR;
    }

    bool ensureOutputTarget(int width, int height, std::string& error) {
        if (!easu_framebuffer) gen_framebuffers(1, &easu_framebuffer);
        if (!easu_texture) glGenTextures(1, &easu_texture);

        if (width != output_width || height != output_height) {
            output_width = width;
            output_height = height;
            glBindTexture(GL_TEXTURE_2D, easu_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }

        bind_framebuffer(GL_FRAMEBUFFER, easu_framebuffer);
        framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, easu_texture, 0);
        if (check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            error = "FSR EASU framebuffer is incomplete";
            bind_framebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
        bind_framebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    static void drawFullscreenQuad(bool flip_vertical = false) {
        glBegin(GL_QUADS);
        const float top_v = flip_vertical ? 1.0f : 0.0f;
        const float bottom_v = flip_vertical ? 0.0f : 1.0f;
        glTexCoord2f(0.0f, top_v); glVertex2f(-1.0f,  1.0f);
        glTexCoord2f(1.0f, top_v); glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(1.0f, bottom_v); glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(0.0f, bottom_v); glVertex2f(-1.0f, -1.0f);
        glEnd();
    }
};

FsrUpscaler::FsrUpscaler() : impl_(std::make_unique<Impl>()) {}

FsrUpscaler::~FsrUpscaler() {
    shutdown();
}

bool FsrUpscaler::initialize(SDL_Window* window,
                             const std::string& shader_directory,
                             std::string& error) {
    shutdown();
    impl_ = std::make_unique<Impl>();
    impl_->window = window;

    if (!window) {
        error = "FSR needs an SDL OpenGL window";
        return false;
    }
    if (!impl_->loadFunctions(error)) return false;

    const std::string vertex = readTextFile(shader_directory + "/fsr_fullscreen.vert", error);
    if (vertex.empty()) return false;
    const std::string easu = readTextFile(shader_directory + "/fsr_easu.frag", error);
    if (easu.empty()) return false;
    const std::string rcas = readTextFile(shader_directory + "/fsr_rcas.frag", error);
    if (rcas.empty()) return false;

    impl_->easu_program = impl_->makeProgram(vertex, easu, error);
    if (!impl_->easu_program) return false;
    impl_->rcas_program = impl_->makeProgram(vertex, rcas, error);
    if (!impl_->rcas_program) return false;

    glGenTextures(1, &impl_->input_texture);
    impl_->gen_framebuffers(1, &impl_->easu_framebuffer);
    glGenTextures(1, &impl_->easu_texture);
    if (!impl_->input_texture || !impl_->easu_framebuffer || !impl_->easu_texture) {
        error = "Could not allocate FSR textures/framebuffer";
        return false;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    SDL_GL_SetSwapInterval(1);
    return true;
}

bool FsrUpscaler::render(const emu::VideoFrame& frame,
                         double aspect_ratio,
                         float sharpness,
                         std::string& error) {
    if (!impl_ || !impl_->window || !frame.pixels ||
        frame.width <= 0 || frame.height <= 0) {
        error = "FSR received an invalid frame or has not been initialized";
        return false;
    }

    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(impl_->window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) return true;

    if (!std::isfinite(aspect_ratio) || aspect_ratio <= 0.0) {
        aspect_ratio = static_cast<double>(frame.width) / frame.height;
    }

    int viewport_width = drawable_width;
    int viewport_height = drawable_height;
    const double window_aspect = static_cast<double>(drawable_width) / drawable_height;
    if (window_aspect > aspect_ratio) {
        viewport_width = static_cast<int>(std::lround(drawable_height * aspect_ratio));
    } else {
        viewport_height = static_cast<int>(std::lround(drawable_width / aspect_ratio));
    }
    viewport_width = std::max(viewport_width, 1);
    viewport_height = std::max(viewport_height, 1);
    const int viewport_x = (drawable_width - viewport_width) / 2;
    const int viewport_y = (drawable_height - viewport_height) / 2;

    if (!impl_->ensureInputTexture(frame.width, frame.height, frame.pixels) ||
        !impl_->ensureOutputTarget(viewport_width, viewport_height, error)) {
        if (error.empty()) error = "Could not upload the emulator frame to OpenGL";
        return false;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glViewport(0, 0, viewport_width, viewport_height);
    impl_->bind_framebuffer(GL_FRAMEBUFFER, impl_->easu_framebuffer);
    impl_->use_program(impl_->easu_program);
    impl_->active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl_->input_texture);
    impl_->uniform_1i(impl_->get_uniform_location(impl_->easu_program, "sampler0"), 0);
    impl_->uniform_2f(impl_->get_uniform_location(impl_->easu_program, "u_texelDelta"),
                      1.0f / frame.width, 1.0f / frame.height);
    impl_->uniform_2f(impl_->get_uniform_location(impl_->easu_program, "u_outputSize"),
                      static_cast<float>(viewport_width),
                      static_cast<float>(viewport_height));
    Impl::drawFullscreenQuad();

    impl_->bind_framebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, drawable_width, drawable_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(viewport_x, viewport_y, viewport_width, viewport_height);
    impl_->use_program(impl_->rcas_program);
    impl_->active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl_->easu_texture);
    impl_->uniform_1i(impl_->get_uniform_location(impl_->rcas_program, "sampler0"), 0);
    impl_->uniform_2f(impl_->get_uniform_location(impl_->rcas_program, "u_texelDelta"),
                      1.0f / viewport_width, 1.0f / viewport_height);
    sharpness = std::clamp(sharpness, 0.0f, 1.0f);
    impl_->uniform_4f(impl_->get_uniform_location(impl_->rcas_program, "u_setting"),
                      sharpness, 0.0f, 0.0f, 0.0f);
    // A render target's vertical origin is opposite the uploaded source
    // image's origin, so flip the EASU texture when presenting it.
    Impl::drawFullscreenQuad(true);
    impl_->use_program(0);

    return glGetError() == GL_NO_ERROR;
}

void FsrUpscaler::shutdown() {
    if (!impl_) return;

    if (impl_->easu_program && impl_->delete_program)
        impl_->delete_program(impl_->easu_program);
    if (impl_->rcas_program && impl_->delete_program)
        impl_->delete_program(impl_->rcas_program);
    if (impl_->input_texture) glDeleteTextures(1, &impl_->input_texture);
    if (impl_->easu_texture) glDeleteTextures(1, &impl_->easu_texture);
    if (impl_->easu_framebuffer && impl_->delete_framebuffers)
        impl_->delete_framebuffers(1, &impl_->easu_framebuffer);

    impl_->easu_program = 0;
    impl_->rcas_program = 0;
    impl_->input_texture = 0;
    impl_->easu_texture = 0;
    impl_->easu_framebuffer = 0;
}

} // namespace emu::frontend
