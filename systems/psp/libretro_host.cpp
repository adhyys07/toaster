#include "libretro_host.h"

#include "libretro.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RENDERBUFFER 0x8D41
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif

namespace emu::libretro {
namespace {

struct LibretroApi {
    decltype(&retro_set_environment) set_environment = nullptr;
    decltype(&retro_set_video_refresh) set_video_refresh = nullptr;
    decltype(&retro_set_audio_sample) set_audio_sample = nullptr;
    decltype(&retro_set_audio_sample_batch) set_audio_sample_batch = nullptr;
    decltype(&retro_set_input_poll) set_input_poll = nullptr;
    decltype(&retro_set_input_state) set_input_state = nullptr;
    decltype(&retro_init) init = nullptr;
    decltype(&retro_deinit) deinit = nullptr;
    decltype(&retro_get_system_info) get_system_info = nullptr;
    decltype(&retro_get_system_av_info) get_system_av_info = nullptr;
    decltype(&retro_load_game) load_game = nullptr;
    decltype(&retro_unload_game) unload_game = nullptr;
    decltype(&retro_run) run = nullptr;
};

template <typename Function>
Function loadFunction(void* the_module, const char* name) {
    return reinterpret_cast<Function>(SDL_LoadFunction(the_module, name));
}

class HostState {
public:
    bool run(const std::string& core_path,
             const std::string& game_path,
             const std::string& system_directory,
             const std::string& system_name,
             bool fullscreen,
             bool mute,
             std::string& error) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            std::fprintf(stderr, "Controller support unavailable: %s\n", SDL_GetError());
        }
        if (!mute && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::fprintf(stderr, "%s audio unavailable: %s\n",
                         system_name.c_str(), SDL_GetError());
        }
        system_directory_ = system_directory;
        system_name_ = system_name;
        mute_ = mute;
        active_ = this;

        if (!createWindow(game_path, system_name_, fullscreen, error) ||
            !loadCore(core_path, error) || !initializeCore(game_path, error)) {
            shutdown();
            return false;
        }

        runLoop();
        shutdown();
        return true;
    }

private:
    static HostState* active_;

    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    SDL_GameController* controller_ = nullptr;
    SDL_Joystick* raw_joystick_ = nullptr;
    SDL_AudioDeviceID audio_device_ = 0;
    SDL_AudioSpec wanted_audio_spec_{};
    SDL_AudioSpec obtained_audio_spec_{};
    void* module_ = nullptr;
    LibretroApi api_{};
    retro_hw_render_callback hw_render_{};
    bool hw_render_requested_ = false;
    bool hw_context_active_ = false;
    bool core_initialized_ = false;
    bool game_loaded_ = false;
    bool mute_ = false;
    bool quit_ = false;
    uint16_t joypad_state_ = 0;
    int16_t analog_x_ = 0;
    int16_t analog_y_ = 0;
    int16_t right_analog_x_ = 0;
    int16_t right_analog_y_ = 0;
    std::string system_directory_;
    std::string system_name_;
    std::map<std::string, std::string> core_options_;
    std::vector<int16_t> audio_frames_;
    GLuint framebuffer_ = 0;
    GLuint framebuffer_texture_ = 0;
    GLuint depth_buffer_ = 0;
    bool logged_video_frame_ = false;
    unsigned framebuffer_width_ = 480;
    unsigned framebuffer_height_ = 272;
    unsigned frame_width_ = 480;
    unsigned frame_height_ = 272;

    using GenFramebuffersFn = void (APIENTRY *)(GLsizei, GLuint*);
    using BindFramebufferFn = void (APIENTRY *)(GLenum, GLuint);
    using CheckFramebufferStatusFn = GLenum (APIENTRY *)(GLenum);
    using FramebufferTexture2DFn = void (APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
    using GenRenderbuffersFn = void (APIENTRY *)(GLsizei, GLuint*);
    using BindRenderbufferFn = void (APIENTRY *)(GLenum, GLuint);
    using RenderbufferStorageFn = void (APIENTRY *)(GLenum, GLenum, GLsizei, GLsizei);
    using FramebufferRenderbufferFn = void (APIENTRY *)(GLenum, GLenum, GLenum, GLuint);
    using DeleteFramebuffersFn = void (APIENTRY *)(GLsizei, const GLuint*);
    using DeleteRenderbuffersFn = void (APIENTRY *)(GLsizei, const GLuint*);
    using BlitFramebufferFn = void (APIENTRY *)(GLint, GLint, GLint, GLint,
                                                GLint, GLint, GLint, GLint,
                                                GLbitfield, GLenum);

    GenFramebuffersFn gen_framebuffers_ = nullptr;
    BindFramebufferFn bind_framebuffer_ = nullptr;
    CheckFramebufferStatusFn check_framebuffer_status_ = nullptr;
    FramebufferTexture2DFn framebuffer_texture_2d_ = nullptr;
    GenRenderbuffersFn gen_renderbuffers_ = nullptr;
    BindRenderbufferFn bind_renderbuffer_ = nullptr;
    RenderbufferStorageFn renderbuffer_storage_ = nullptr;
    FramebufferRenderbufferFn framebuffer_renderbuffer_ = nullptr;
    DeleteFramebuffersFn delete_framebuffers_ = nullptr;
    DeleteRenderbuffersFn delete_renderbuffers_ = nullptr;
    BlitFramebufferFn blit_framebuffer_ = nullptr;

    static uintptr_t RETRO_CALLCONV currentFramebuffer() {
        return active_ ? active_->framebuffer_ : 0;
    }

    static retro_proc_address_t RETRO_CALLCONV getGlProcAddress(const char* name) {
        return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(name));
    }

    static void RETRO_CALLCONV videoRefresh(const void* data, unsigned width,
                                             unsigned height, size_t) {
        if (!active_) return;
        if (data != RETRO_HW_FRAME_BUFFER_VALID) {
            if (data && !active_->logged_video_frame_) {
                std::fprintf(stderr,
                    "[%s] Core submitted a software frame; this host expects an OpenGL hardware frame.\n",
                    active_->system_name_.c_str());
                active_->logged_video_frame_ = true;
            }
            return;
        }
        active_->frame_width_ = std::min(width, active_->framebuffer_width_);
        active_->frame_height_ = std::min(height, active_->framebuffer_height_);
        if (!active_->logged_video_frame_) {
            std::fprintf(stderr, "[%s] Received OpenGL frame: %ux%u (FBO %ux%u).\n",
                         active_->system_name_.c_str(),
                         active_->frame_width_, active_->frame_height_,
                         active_->framebuffer_width_, active_->framebuffer_height_);
            active_->logged_video_frame_ = true;
        }
    }

    template <typename Function>
    bool loadGlFunction(Function& function, const char* name) {
        function = reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
        return function != nullptr;
    }

    bool createFramebuffer(unsigned width, unsigned height, std::string& error) {
        framebuffer_width_ = std::clamp(width, 1u, 8192u);
        framebuffer_height_ = std::clamp(height, 1u, 8192u);
        frame_width_ = framebuffer_width_;
        frame_height_ = framebuffer_height_;

        if (!loadGlFunction(gen_framebuffers_, "glGenFramebuffers") ||
            !loadGlFunction(bind_framebuffer_, "glBindFramebuffer") ||
            !loadGlFunction(check_framebuffer_status_, "glCheckFramebufferStatus") ||
            !loadGlFunction(framebuffer_texture_2d_, "glFramebufferTexture2D") ||
            !loadGlFunction(gen_renderbuffers_, "glGenRenderbuffers") ||
            !loadGlFunction(bind_renderbuffer_, "glBindRenderbuffer") ||
            !loadGlFunction(renderbuffer_storage_, "glRenderbufferStorage") ||
            !loadGlFunction(framebuffer_renderbuffer_, "glFramebufferRenderbuffer") ||
            !loadGlFunction(delete_framebuffers_, "glDeleteFramebuffers") ||
            !loadGlFunction(delete_renderbuffers_, "glDeleteRenderbuffers") ||
            !loadGlFunction(blit_framebuffer_, "glBlitFramebuffer")) {
            error = "the OpenGL driver is missing framebuffer functions required by the core";
            return false;
        }

        glGenTextures(1, &framebuffer_texture_);
        glBindTexture(GL_TEXTURE_2D, framebuffer_texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     static_cast<GLsizei>(framebuffer_width_),
                     static_cast<GLsizei>(framebuffer_height_), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        gen_framebuffers_(1, &framebuffer_);
        bind_framebuffer_(GL_FRAMEBUFFER, framebuffer_);
        framebuffer_texture_2d_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, framebuffer_texture_, 0);

        if (hw_render_.depth) {
            gen_renderbuffers_(1, &depth_buffer_);
            bind_renderbuffer_(GL_RENDERBUFFER, depth_buffer_);
            const bool packed_depth_stencil = hw_render_.depth && hw_render_.stencil;
            renderbuffer_storage_(GL_RENDERBUFFER,
                                  packed_depth_stencil ? GL_DEPTH24_STENCIL8
                                                       : GL_DEPTH_COMPONENT24,
                                  static_cast<GLsizei>(framebuffer_width_),
                                  static_cast<GLsizei>(framebuffer_height_));
            framebuffer_renderbuffer_(GL_FRAMEBUFFER,
                                      packed_depth_stencil
                                          ? GL_DEPTH_STENCIL_ATTACHMENT
                                          : GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, depth_buffer_);
        }

        const GLenum status = check_framebuffer_status_(GL_FRAMEBUFFER);
        bind_framebuffer_(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            error = "could not create a complete OpenGL framebuffer for the core";
            destroyFramebuffer();
            return false;
        }
        return true;
    }

    void presentFramebuffer() {
        if (!framebuffer_ || !blit_framebuffer_ ||
            drawable_width_ <= 0 || drawable_height_ <= 0) return;
        // A core may leave scissoring enabled. That state can clip the
        // frontend's framebuffer blit and leave the window looking black.
        glDisable(GL_SCISSOR_TEST);
        bind_framebuffer_(GL_READ_FRAMEBUFFER, framebuffer_);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        bind_framebuffer_(GL_DRAW_FRAMEBUFFER, 0);
        glDrawBuffer(GL_BACK);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        int target_width = drawable_width_;
        int target_height = drawable_height_;
        const double window_aspect = static_cast<double>(drawable_width_) /
                                     std::max(drawable_height_, 1);
        const double source_aspect = aspect_ratio_ > 0.0
            ? aspect_ratio_
            : static_cast<double>(frame_width_) / std::max(frame_height_, 1u);
        if (window_aspect > source_aspect) {
            target_width = static_cast<int>(std::lround(drawable_height_ * source_aspect));
        } else {
            target_height = static_cast<int>(std::lround(drawable_width_ / source_aspect));
        }
        const int target_x = (drawable_width_ - target_width) / 2;
        const int target_y = (drawable_height_ - target_height) / 2;
        blit_framebuffer_(0, 0,
                          static_cast<GLint>(frame_width_),
                          static_cast<GLint>(frame_height_),
                          target_x, target_y,
                          target_x + target_width, target_y + target_height,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        bind_framebuffer_(GL_FRAMEBUFFER, 0);
    }

    void destroyFramebuffer() {
        if (depth_buffer_ && delete_renderbuffers_) {
            delete_renderbuffers_(1, &depth_buffer_);
            depth_buffer_ = 0;
        }
        if (framebuffer_ && delete_framebuffers_) {
            delete_framebuffers_(1, &framebuffer_);
            framebuffer_ = 0;
        }
        if (framebuffer_texture_) {
            glDeleteTextures(1, &framebuffer_texture_);
            framebuffer_texture_ = 0;
        }
    }

    static void RETRO_CALLCONV audioSample(int16_t left, int16_t right) {
        if (!active_ || active_->mute_) return;
        active_->audio_frames_.push_back(left);
        active_->audio_frames_.push_back(right);
    }

    static size_t RETRO_CALLCONV audioSampleBatch(const int16_t* samples,
                                                   size_t frames) {
        if (!active_ || active_->mute_ || !samples) return frames;
        active_->audio_frames_.insert(active_->audio_frames_.end(),
                                      samples, samples + frames * 2);
        return frames;
    }

    static void RETRO_CALLCONV inputPoll() {
        if (!active_) return;
        active_->updateInput();
    }

    static int16_t RETRO_CALLCONV inputState(unsigned port, unsigned device,
                                              unsigned index, unsigned id) {
        if (!active_ || port != 0) return 0;
        if (device == RETRO_DEVICE_JOYPAD) {
            if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return active_->joypad_state_;
            if (id < 16) return (active_->joypad_state_ & (1u << id)) ? 1 : 0;
        } else if (device == RETRO_DEVICE_ANALOG &&
                   (index == RETRO_DEVICE_INDEX_ANALOG_LEFT ||
                    index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)) {
            const bool right_stick = index == RETRO_DEVICE_INDEX_ANALOG_RIGHT;
            const int16_t x = right_stick ? active_->right_analog_x_ : active_->analog_x_;
            const int16_t y = right_stick ? active_->right_analog_y_ : active_->analog_y_;
            if (id == RETRO_DEVICE_ID_ANALOG_X) return x;
            if (id == RETRO_DEVICE_ID_ANALOG_Y) return y;
        }
        return 0;
    }

    static void RETRO_CALLCONV logMessage(enum retro_log_level level,
                                           const char* format, ...) {
        if (!format || level < RETRO_LOG_ERROR) return;

        char message[2048];
        va_list args;
        va_start(args, format);
        std::vsnprintf(message, sizeof(message), format, args);
        va_end(args);

        // These network/platform notices do not prevent offline gameplay.
        // Keep graphics errors visible for troubleshooting.
        if (std::strstr(message, "[sceNet] Error parsing mac address") ||
            std::strstr(message,
                        "Exception handler not implemented on this platform") ||
            std::strstr(message, "[HLE] ALREADY=scePowerRegisterCallback")) {
            return;
        }

        std::fputs(message, stderr);
    }

    static bool RETRO_CALLCONV environment(unsigned command, void* data) {
        if (!active_) return false;
        HostState& host = *active_;

        switch (command) {
        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
            if (!data) return false;
            *static_cast<retro_hw_context_type*>(data) = RETRO_HW_CONTEXT_OPENGL_CORE;
            return true;
        case RETRO_ENVIRONMENT_SET_HW_RENDER: {
            if (!data) return false;
            auto* callback = static_cast<retro_hw_render_callback*>(data);
            if (callback->context_type != RETRO_HW_CONTEXT_OPENGL_CORE &&
                callback->context_type != RETRO_HW_CONTEXT_OPENGL) {
                std::fprintf(stderr,
                    "This %s core requested an unsupported rendering API (%d).\n",
                    host.system_name_.c_str(),
                    static_cast<int>(callback->context_type));
                return false;
            }
            if (!callback->context_reset || !host.gl_context_) return false;

            callback->get_current_framebuffer = currentFramebuffer;
            callback->get_proc_address = getGlProcAddress;
            host.hw_render_ = *callback;
            host.hw_render_requested_ = true;
            if (SDL_GL_MakeCurrent(host.window_, host.gl_context_) != 0) return false;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            if (!data) return false;
            *static_cast<const char**>(data) = host.system_directory_.c_str();
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            if (!data) return false;
            auto* variable = static_cast<retro_variable*>(data);
            const auto found = host.core_options_.find(variable->key ? variable->key : "");
            variable->value = found == host.core_options_.end()
                ? nullptr : found->second.c_str();
            return true;
        }
        case RETRO_ENVIRONMENT_SET_VARIABLES: {
            if (!data) return false;
            const auto* vars = static_cast<const retro_variable*>(data);
            for (; vars->key && vars->value; ++vars) {
                const std::string definition(vars->value);
                const size_t separator = definition.find(';');
                if (separator == std::string::npos) continue;
                std::string default_value = definition.substr(separator + 1);
                const size_t first = default_value.find_first_not_of(" \t");
                if (first != std::string::npos) default_value.erase(0, first);
                const size_t end = default_value.find_first_of("| \t");
                if (end != std::string::npos) default_value.resize(end);
                host.core_options_[vars->key] = default_value;
            }
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if (data) *static_cast<bool*>(data) = false;
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if (data) *static_cast<bool*>(data) = true;
            return true;
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            if (data) *static_cast<bool*>(data) = true;
            return true;
        case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
            if (data) {
                *static_cast<uint64_t*>(data) = (1ULL << RETRO_DEVICE_JOYPAD) |
                                                (1ULL << RETRO_DEVICE_ANALOG);
            }
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            if (!data) return false;
            static_cast<retro_log_callback*>(data)->log = logMessage;
            return true;
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            if (data) *static_cast<unsigned*>(data) = 2;
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
            if (!data) return false;
            const auto* options = static_cast<const retro_core_options_v2*>(data);
            if (options->definitions) {
                for (const auto* option = options->definitions; option->key; ++option) {
                    if (option->default_value)
                        host.core_options_[option->key] = option->default_value;
                }
            }
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
            if (!data) return false;
            const auto* options = static_cast<const retro_core_options_v2_intl*>(data);
            if (options->us && options->us->definitions) {
                for (const auto* option = options->us->definitions;
                     option->key; ++option) {
                    if (option->default_value)
                        host.core_options_[option->key] = option->default_value;
                }
            }
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
            if (!data) return false;
            const auto* options = static_cast<const retro_core_option_definition*>(data);
            for (const auto* option = options; option->key; ++option) {
                if (option->default_value)
                    host.core_options_[option->key] = option->default_value;
            }
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
            if (!data) return false;
            const auto* options = static_cast<const retro_core_options_intl*>(data);
            if (options->us) {
                for (const auto* option = options->us; option->key; ++option) {
                    if (option->default_value)
                        host.core_options_[option->key] = option->default_value;
                }
            }
            return true;
        }
        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            if (data) *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
            return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            if (!data) return false;
            const auto format = *static_cast<const retro_pixel_format*>(data);
            return format == RETRO_PIXEL_FORMAT_0RGB1555 ||
                   format == RETRO_PIXEL_FORMAT_RGB565 ||
                   format == RETRO_PIXEL_FORMAT_XRGB8888;
        }
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_MESSAGE:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
            return true;
        default:
            return false;
        }
    }

    bool createWindow(const std::string& game_path,
                      const std::string& system_name,
                      bool fullscreen,
                      std::string& error) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
        if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        const std::string title = system_name + " - " + game_path;
        window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, 960, 544, flags);
        if (!window_) {
            error = std::string("could not create OpenGL window: ") + SDL_GetError();
            return false;
        }

        gl_context_ = SDL_GL_CreateContext(window_);
        if (!gl_context_) {
            error = std::string("could not create OpenGL 3.3 context: ") + SDL_GetError();
            return false;
        }
        if (SDL_GL_MakeCurrent(window_, gl_context_) != 0) {
            error = std::string("could not activate OpenGL context: ") + SDL_GetError();
            return false;
        }
        SDL_GL_SetSwapInterval(1);
        SDL_GL_GetDrawableSize(window_, &drawable_width_, &drawable_height_);
        return true;
    }

    bool loadCore(const std::string& core_path, std::string& error) {
        module_ = SDL_LoadObject(core_path.c_str());
        if (!module_) {
            error = "could not load Libretro core '" + core_path + "': " + SDL_GetError();
            return false;
        }

#define LOAD_RETRO(member, symbol) \
        api_.member = loadFunction<decltype(api_.member)>(module_, symbol); \
        if (!api_.member) { error = std::string("Libretro core is missing ") + symbol; return false; }
        LOAD_RETRO(set_environment, "retro_set_environment")
        LOAD_RETRO(set_video_refresh, "retro_set_video_refresh")
        LOAD_RETRO(set_audio_sample, "retro_set_audio_sample")
        LOAD_RETRO(set_audio_sample_batch, "retro_set_audio_sample_batch")
        LOAD_RETRO(set_input_poll, "retro_set_input_poll")
        LOAD_RETRO(set_input_state, "retro_set_input_state")
        LOAD_RETRO(init, "retro_init")
        LOAD_RETRO(deinit, "retro_deinit")
        LOAD_RETRO(get_system_info, "retro_get_system_info")
        LOAD_RETRO(get_system_av_info, "retro_get_system_av_info")
        LOAD_RETRO(load_game, "retro_load_game")
        LOAD_RETRO(unload_game, "retro_unload_game")
        LOAD_RETRO(run, "retro_run")
#undef LOAD_RETRO
        return true;
    }

    bool initializeCore(const std::string& game_path, std::string& error) {
        api_.set_environment(environment);
        api_.set_video_refresh(videoRefresh);
        api_.set_audio_sample(audioSample);
        api_.set_audio_sample_batch(audioSampleBatch);
        api_.set_input_poll(inputPoll);
        api_.set_input_state(inputState);
        api_.init();
        core_initialized_ = true;

        // Birdman's Shooter's own PPSSPP instructions recommend software
        // rendering for its graphics. The Libretro core exposes this as a
        // core option, so select it before retro_load_game reads the options.
        std::string normalized_game_path = game_path;
        std::transform(normalized_game_path.begin(), normalized_game_path.end(),
                       normalized_game_path.begin(), [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        if (normalized_game_path.find("birdmans-shooter") != std::string::npos) {
            core_options_["ppsspp_software_rendering"] = "enabled";
            std::fprintf(stderr,
                         "[PSP] Enabling PPSSPP software rendering for Birdman's Shooter.\n");
        }

        retro_system_info info{};
        api_.get_system_info(&info);
        if (info.library_name) {
            std::printf("Libretro core: %s %s\n", info.library_name,
                        info.library_version ? info.library_version : "");
        }

        retro_game_info game{};
        game.path = game_path.c_str();
        const bool loaded = api_.load_game(&game);
        if (!loaded) {
            error = "the Libretro core could not load this game";
            return false;
        }
        game_loaded_ = true;
        if (!hw_render_requested_) {
            error = "the core did not request hardware rendering; this host needs OpenGL rendering";
            return false;
        }

        retro_system_av_info av{};
        api_.get_system_av_info(&av);
        const unsigned render_width = av.geometry.max_width
            ? av.geometry.max_width : av.geometry.base_width;
        const unsigned render_height = av.geometry.max_height
            ? av.geometry.max_height : av.geometry.base_height;
        aspect_ratio_ = av.geometry.aspect_ratio > 0.0f
            ? av.geometry.aspect_ratio
            : (av.geometry.base_height
                ? static_cast<double>(av.geometry.base_width) / av.geometry.base_height
                : 4.0 / 3.0);
        if (SDL_GL_MakeCurrent(window_, gl_context_) != 0) {
            error = std::string("could not activate OpenGL context: ") + SDL_GetError();
            return false;
        }
        if (!createFramebuffer(render_width, render_height, error)) return false;
        if (!hw_render_.context_reset) {
            error = "the core did not provide an OpenGL context reset callback";
            return false;
        }
        hw_render_.context_reset();
        hw_context_active_ = true;

        const double fps = av.timing.fps > 1.0 ? av.timing.fps : 60.0;
        frame_duration_ = 1.0 / fps;
        if (av.timing.sample_rate > 0.0 && !mute_) {
            wanted_audio_spec_ = {};
            wanted_audio_spec_.freq = static_cast<int>(av.timing.sample_rate);
            wanted_audio_spec_.format = AUDIO_S16SYS;
            wanted_audio_spec_.channels = 2;
            wanted_audio_spec_.samples = 2048;
            audio_device_ = SDL_OpenAudioDevice(nullptr, 0, &wanted_audio_spec_,
                                                 &obtained_audio_spec_, 0);
            if (audio_device_) SDL_PauseAudioDevice(audio_device_, 0);
            else std::fprintf(stderr, "%s audio unavailable: %s\n",
                              system_name_.c_str(), SDL_GetError());
        }
        std::fprintf(stderr, "[%s] Game loaded: %s\n",
                     system_name_.c_str(), game_path.c_str());
        std::fprintf(stderr,
                     "[%s] Keys: arrows/numpad 2-4-6-8 D-pad; WASD left stick; "
                     "IJKL right stick; Z Cross, X Circle, C Square, V Triangle; "
                     "Q/E L1/R1; 1/3 L2/R2; Enter Start; Backspace Select.\n",
                     system_name_.c_str());
        return true;
    }

    void updateInput() {
        SDL_PumpEvents();
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        uint16_t state = 0;
        const auto set = [&state](unsigned id, bool pressed) {
            if (pressed) state = static_cast<uint16_t>(state | (1u << id));
        };

        // Standard PlayStation face layout: B=Cross, A=Circle,
        // X=Triangle, Y=Square. Z/X/C/V are the face keys.
        set(RETRO_DEVICE_ID_JOYPAD_B, keys[SDL_SCANCODE_Z]); // Cross
        set(RETRO_DEVICE_ID_JOYPAD_A, keys[SDL_SCANCODE_X]); // Circle
        set(RETRO_DEVICE_ID_JOYPAD_Y, keys[SDL_SCANCODE_C]); // Square
        set(RETRO_DEVICE_ID_JOYPAD_X, keys[SDL_SCANCODE_V]); // Triangle
        set(RETRO_DEVICE_ID_JOYPAD_START,
            keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]);
        set(RETRO_DEVICE_ID_JOYPAD_SELECT,
            keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_RSHIFT]);
        set(RETRO_DEVICE_ID_JOYPAD_UP,
            keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_KP_8]);
        set(RETRO_DEVICE_ID_JOYPAD_DOWN,
            keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_KP_2]);
        set(RETRO_DEVICE_ID_JOYPAD_LEFT,
            keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_KP_4]);
        set(RETRO_DEVICE_ID_JOYPAD_RIGHT,
            keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_KP_6]);
        set(RETRO_DEVICE_ID_JOYPAD_L, keys[SDL_SCANCODE_Q]);
        set(RETRO_DEVICE_ID_JOYPAD_R, keys[SDL_SCANCODE_E]);
        set(RETRO_DEVICE_ID_JOYPAD_L2, keys[SDL_SCANCODE_1]);
        set(RETRO_DEVICE_ID_JOYPAD_R2, keys[SDL_SCANCODE_3]);

        analog_x_ = static_cast<int16_t>((keys[SDL_SCANCODE_D] ? 32767 : 0) -
                                         (keys[SDL_SCANCODE_A] ? 32768 : 0));
        analog_y_ = static_cast<int16_t>((keys[SDL_SCANCODE_S] ? 32767 : 0) -
                                         (keys[SDL_SCANCODE_W] ? 32768 : 0));
        right_analog_x_ = static_cast<int16_t>((keys[SDL_SCANCODE_L] ? 32767 : 0) -
                                               (keys[SDL_SCANCODE_J] ? 32768 : 0));
        right_analog_y_ = static_cast<int16_t>((keys[SDL_SCANCODE_K] ? 32767 : 0) -
                                               (keys[SDL_SCANCODE_I] ? 32768 : 0));

        if (controller_) {
            const auto button = [this](SDL_GameControllerButton id) {
                return SDL_GameControllerGetButton(controller_, id) != 0;
            };
            // Xbox-style controller buttons: bottom=A, right=B, left=X, top=Y.
            // Map those positions to Cross, Circle, Square, Triangle.
            set(RETRO_DEVICE_ID_JOYPAD_B, button(SDL_CONTROLLER_BUTTON_A)); // Cross
            set(RETRO_DEVICE_ID_JOYPAD_A, button(SDL_CONTROLLER_BUTTON_B)); // Circle
            set(RETRO_DEVICE_ID_JOYPAD_Y, button(SDL_CONTROLLER_BUTTON_X)); // Square
            set(RETRO_DEVICE_ID_JOYPAD_X, button(SDL_CONTROLLER_BUTTON_Y)); // Triangle
            set(RETRO_DEVICE_ID_JOYPAD_START, button(SDL_CONTROLLER_BUTTON_START));
            set(RETRO_DEVICE_ID_JOYPAD_SELECT, button(SDL_CONTROLLER_BUTTON_BACK));
            set(RETRO_DEVICE_ID_JOYPAD_UP, button(SDL_CONTROLLER_BUTTON_DPAD_UP));
            set(RETRO_DEVICE_ID_JOYPAD_DOWN, button(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
            set(RETRO_DEVICE_ID_JOYPAD_LEFT, button(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
            set(RETRO_DEVICE_ID_JOYPAD_RIGHT, button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
            const Sint16 left_trigger = SDL_GameControllerGetAxis(
                controller_, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            const Sint16 right_trigger = SDL_GameControllerGetAxis(
                controller_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            set(RETRO_DEVICE_ID_JOYPAD_L,
                button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
            set(RETRO_DEVICE_ID_JOYPAD_R,
                button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
            set(RETRO_DEVICE_ID_JOYPAD_L2, left_trigger > 16000);
            set(RETRO_DEVICE_ID_JOYPAD_R2, right_trigger > 16000);
            constexpr int deadzone = 7000;
            const Sint16 controller_x = SDL_GameControllerGetAxis(
                controller_, SDL_CONTROLLER_AXIS_LEFTX);
            const Sint16 controller_y = SDL_GameControllerGetAxis(
                controller_, SDL_CONTROLLER_AXIS_LEFTY);
            const Sint16 controller_right_x = SDL_GameControllerGetAxis(
                controller_, SDL_CONTROLLER_AXIS_RIGHTX);
            const Sint16 controller_right_y = SDL_GameControllerGetAxis(
                controller_, SDL_CONTROLLER_AXIS_RIGHTY);
            if (std::abs(static_cast<int>(controller_x)) >= deadzone)
                analog_x_ = controller_x;
            if (std::abs(static_cast<int>(controller_y)) >= deadzone)
                analog_y_ = controller_y;
            if (std::abs(static_cast<int>(controller_right_x)) >= deadzone)
                right_analog_x_ = controller_right_x;
            if (std::abs(static_cast<int>(controller_right_y)) >= deadzone)
                right_analog_y_ = controller_right_y;
            set(RETRO_DEVICE_ID_JOYPAD_LEFT, controller_x < -deadzone);
            set(RETRO_DEVICE_ID_JOYPAD_RIGHT, controller_x > deadzone);
            set(RETRO_DEVICE_ID_JOYPAD_UP, controller_y < -deadzone);
            set(RETRO_DEVICE_ID_JOYPAD_DOWN, controller_y > deadzone);
        } else if (raw_joystick_) {
            // Fallback for devices SDL does not recognize as a mapped
            // GameController. This follows the common SDL/XInput layout.
            const int button_count = SDL_JoystickNumButtons(raw_joystick_);
            const auto button = [this, button_count](int id) {
                return id < button_count && SDL_JoystickGetButton(raw_joystick_, id) != 0;
            };
            set(RETRO_DEVICE_ID_JOYPAD_B, button(0)); // bottom face: Cross
            set(RETRO_DEVICE_ID_JOYPAD_A, button(1)); // right face: Circle
            set(RETRO_DEVICE_ID_JOYPAD_Y, button(2)); // left face: Square
            set(RETRO_DEVICE_ID_JOYPAD_X, button(3)); // top face: Triangle
            set(RETRO_DEVICE_ID_JOYPAD_L, button(4));
            set(RETRO_DEVICE_ID_JOYPAD_R, button(5));
            set(RETRO_DEVICE_ID_JOYPAD_SELECT, button(6));
            set(RETRO_DEVICE_ID_JOYPAD_START, button(7));

            if (SDL_JoystickNumHats(raw_joystick_) > 0) {
                const Uint8 hat = SDL_JoystickGetHat(raw_joystick_, 0);
                set(RETRO_DEVICE_ID_JOYPAD_UP, (hat & SDL_HAT_UP) != 0);
                set(RETRO_DEVICE_ID_JOYPAD_DOWN, (hat & SDL_HAT_DOWN) != 0);
                set(RETRO_DEVICE_ID_JOYPAD_LEFT, (hat & SDL_HAT_LEFT) != 0);
                set(RETRO_DEVICE_ID_JOYPAD_RIGHT, (hat & SDL_HAT_RIGHT) != 0);
            }

            constexpr int deadzone = 7000;
            const int axis_count = SDL_JoystickNumAxes(raw_joystick_);
            const Sint16 controller_x = axis_count > 0
                ? SDL_JoystickGetAxis(raw_joystick_, 0) : 0;
            const Sint16 controller_y = axis_count > 1
                ? SDL_JoystickGetAxis(raw_joystick_, 1) : 0;
            const Sint16 controller_right_x = axis_count > 2
                ? SDL_JoystickGetAxis(raw_joystick_, 2) : 0;
            const Sint16 controller_right_y = axis_count > 3
                ? SDL_JoystickGetAxis(raw_joystick_, 3) : 0;
            const Sint16 left_trigger = axis_count > 4
                ? SDL_JoystickGetAxis(raw_joystick_, 4) : 0;
            const Sint16 right_trigger = axis_count > 5
                ? SDL_JoystickGetAxis(raw_joystick_, 5) : 0;
            set(RETRO_DEVICE_ID_JOYPAD_L2, left_trigger > 16000);
            set(RETRO_DEVICE_ID_JOYPAD_R2, right_trigger > 16000);
            if (std::abs(static_cast<int>(controller_x)) >= deadzone)
                analog_x_ = controller_x;
            if (std::abs(static_cast<int>(controller_y)) >= deadzone)
                analog_y_ = controller_y;
            if (std::abs(static_cast<int>(controller_right_x)) >= deadzone)
                right_analog_x_ = controller_right_x;
            if (std::abs(static_cast<int>(controller_right_y)) >= deadzone)
                right_analog_y_ = controller_right_y;
            set(RETRO_DEVICE_ID_JOYPAD_LEFT, controller_x < -deadzone);
            set(RETRO_DEVICE_ID_JOYPAD_RIGHT, controller_x > deadzone);
            set(RETRO_DEVICE_ID_JOYPAD_UP, controller_y < -deadzone);
            set(RETRO_DEVICE_ID_JOYPAD_DOWN, controller_y > deadzone);
        }
        joypad_state_ = state;
    }

    void openController() {
        if (controller_ || raw_joystick_) return;
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_IsGameController(i)) {
                controller_ = SDL_GameControllerOpen(i);
                if (controller_) break;
            }
        }
        if (controller_) {
            const char* name = SDL_GameControllerName(controller_);
            std::fprintf(stderr, "[%s] Controller connected: %s\n",
                         system_name_.c_str(),
                         name ? name : "(unnamed)");
            return;
        }
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (!SDL_IsGameController(i)) {
                raw_joystick_ = SDL_JoystickOpen(i);
                if (raw_joystick_) break;
            }
        }
        if (raw_joystick_) {
            const char* name = SDL_JoystickName(raw_joystick_);
            std::fprintf(stderr,
                         "[%s] Unmapped joystick connected: %s (using common button layout)\n",
                         system_name_.c_str(),
                         name ? name : "(unnamed)");
        }
    }

    void runLoop() {
        openController();
        quit_ = false;
        const Uint64 frequency = SDL_GetPerformanceFrequency();
        const double frame_ticks = frame_duration_ * static_cast<double>(frequency);
        double next_frame = static_cast<double>(SDL_GetPerformanceCounter());

        while (!quit_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) quit_ = true;
                else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) quit_ = true;
                    if (event.key.keysym.scancode == SDL_SCANCODE_F11) {
                        const bool current = (SDL_GetWindowFlags(window_) &
                            SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                        SDL_SetWindowFullscreen(window_, current ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
                    openController();
                } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller_ &&
                           SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller_)) ==
                               event.cdevice.which) {
                    SDL_GameControllerClose(controller_);
                    controller_ = nullptr;
                    analog_x_ = analog_y_ = 0;
                    right_analog_x_ = right_analog_y_ = 0;
                    openController();
                } else if (event.type == SDL_JOYDEVICEADDED) {
                    openController();
                } else if (event.type == SDL_JOYDEVICEREMOVED && raw_joystick_ &&
                           SDL_JoystickInstanceID(raw_joystick_) == event.jdevice.which) {
                    SDL_JoystickClose(raw_joystick_);
                    raw_joystick_ = nullptr;
                    analog_x_ = analog_y_ = 0;
                    right_analog_x_ = right_analog_y_ = 0;
                    openController();
                }
            }

            if (SDL_GL_MakeCurrent(window_, gl_context_) != 0) {
                std::fprintf(stderr, "Could not activate %s OpenGL context: %s\n",
                             system_name_.c_str(), SDL_GetError());
                break;
            }
            SDL_GL_GetDrawableSize(window_, &drawable_width_, &drawable_height_);
            using ViewportFn = void (APIENTRY *)(GLint, GLint, GLsizei, GLsizei);
            const auto viewport = reinterpret_cast<ViewportFn>(SDL_GL_GetProcAddress("glViewport"));
            // The core renders into the libretro FBO, whose dimensions are
            // the core's maximum geometry, not the window's drawable size.
            if (bind_framebuffer_) bind_framebuffer_(GL_FRAMEBUFFER, framebuffer_);
            if (viewport) {
                viewport(0, 0, static_cast<GLsizei>(framebuffer_width_),
                         static_cast<GLsizei>(framebuffer_height_));
            }
            api_.run();
            presentFramebuffer();
            SDL_GL_SwapWindow(window_);

            if (audio_device_ && !audio_frames_.empty()) {
                const Uint32 queued_limit = static_cast<Uint32>(
                    std::max(1, obtained_audio_spec_.freq) * 2 * sizeof(int16_t) / 8);
                if (SDL_GetQueuedAudioSize(audio_device_) > queued_limit) {
                    SDL_ClearQueuedAudio(audio_device_);
                }
                SDL_QueueAudio(audio_device_, audio_frames_.data(),
                    static_cast<Uint32>(audio_frames_.size() * sizeof(int16_t)));
                audio_frames_.clear();
            }

            next_frame += frame_ticks;
            const double now = static_cast<double>(SDL_GetPerformanceCounter());
            if (next_frame > now) {
                const Uint32 delay = static_cast<Uint32>(
                    (next_frame - now) * 1000.0 / static_cast<double>(frequency));
                if (delay) SDL_Delay(delay);
            } else {
                next_frame = now;
            }
        }
    }

    void shutdown() {
        if (audio_device_) {
            SDL_ClearQueuedAudio(audio_device_);
            SDL_CloseAudioDevice(audio_device_);
            audio_device_ = 0;
        }
        // Keep the core's live graphics context current during context teardown.
        // Notify it while the core and game are still initialized.
        if (hw_context_active_ && hw_render_.context_destroy && gl_context_) {
            SDL_GL_MakeCurrent(window_, gl_context_);
            hw_render_.context_destroy();
            hw_context_active_ = false;
        }
        if (gl_context_) {
            SDL_GL_MakeCurrent(window_, gl_context_);
            destroyFramebuffer();
        }
        if (game_loaded_ && api_.unload_game) {
            api_.unload_game();
            game_loaded_ = false;
        }
        if (core_initialized_ && api_.deinit) {
            api_.deinit();
            core_initialized_ = false;
        }
        if (controller_) {
            SDL_GameControllerClose(controller_);
            controller_ = nullptr;
        }
        if (raw_joystick_) {
            SDL_JoystickClose(raw_joystick_);
            raw_joystick_ = nullptr;
        }
        if (gl_context_) {
            SDL_GL_DeleteContext(gl_context_);
            gl_context_ = nullptr;
        }
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (module_) {
            SDL_UnloadObject(module_);
            module_ = nullptr;
        }
        active_ = nullptr;
    }

    double frame_duration_ = 1.0 / 60.0;
    double aspect_ratio_ = 4.0 / 3.0;
    int drawable_width_ = 960;
    int drawable_height_ = 544;
};

HostState* HostState::active_ = nullptr;

}  // namespace

bool LibretroHost::run(const std::string& core_path,
                       const std::string& game_path,
                       const std::string& system_directory,
                       const std::string& system_name,
                       bool fullscreen,
                       bool mute,
                       std::string& error) {
    HostState host;
    return host.run(core_path, game_path, system_directory, system_name,
                    fullscreen, mute, error);
}

}  // namespace emu::libretro
