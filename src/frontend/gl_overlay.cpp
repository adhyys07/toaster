#include "gl_overlay.h"

#include <SDL_opengl.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace emu::frontend {

GlOverlay::~GlOverlay() { shutdown(); }

void GlOverlay::shutdown() {
    if (texture_) {
        const GLuint name = static_cast<GLuint>(texture_);
        glDeleteTextures(1, &name);
        texture_ = 0;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (surface_) {
        SDL_FreeSurface(surface_);
        surface_ = nullptr;
    }
    window_ = nullptr;
    width_ = height_ = 0;
    drawn_ = false;
}

bool GlOverlay::resize(SDL_Window* window, std::string& error) {
    if (!window) {
        error = "The overlay needs a window.";
        return false;
    }

    int drawable_w = 0;
    int drawable_h = 0;
    SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
    if (drawable_w <= 0 || drawable_h <= 0) {
        error = "The window has no drawable area.";
        return false;
    }

    int logical_w = 0;
    int logical_h = 0;
    SDL_GetWindowSize(window, &logical_w, &logical_h);
    scale_ = logical_w > 0
        ? static_cast<float>(drawable_w) / static_cast<float>(logical_w)
        : 1.0f;
    window_ = window;

    if (renderer_ && drawable_w == width_ && drawable_h == height_) return true;

    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (surface_) {
        SDL_FreeSurface(surface_);
        surface_ = nullptr;
    }

    surface_ = SDL_CreateRGBSurfaceWithFormat(0, drawable_w, drawable_h, 32,
                                              SDL_PIXELFORMAT_ARGB8888);
    if (!surface_) {
        error = std::string("Could not create the overlay surface: ") +
                SDL_GetError();
        return false;
    }

    renderer_ = SDL_CreateSoftwareRenderer(surface_);
    if (!renderer_) {
        error = std::string("Could not create the overlay renderer: ") +
                SDL_GetError();
        SDL_FreeSurface(surface_);
        surface_ = nullptr;
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    width_ = drawable_w;
    height_ = drawable_h;
    drawn_ = false;
    return true;
}

void GlOverlay::clear() {
    if (!renderer_) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    drawn_ = false;
}

void GlOverlay::composite() {
    if (!drawn_ || !surface_ || !renderer_ || !window_) return;

    // Flushes the software renderer's batched commands into the surface.
    SDL_RenderPresent(renderer_);

    // The post-process pass leaves its own program bound, and a bound program
    // overrides the fixed-function pipeline the quad below relies on, so the
    // overlay would come out as whatever that shader made of it. glUseProgram
    // is not in OpenGL 1.1, so it is resolved rather than linked; a null
    // pointer means there is no programmable pipeline to turn off.
    using UseProgramFn = void (APIENTRY *)(GLuint);
    static UseProgramFn use_program = reinterpret_cast<UseProgramFn>(
        SDL_GL_GetProcAddress("glUseProgram"));
    if (use_program) use_program(0);

    if (!texture_) {
        GLuint name = 0;
        glGenTextures(1, &name);
        if (!name) return;
        texture_ = name;
    }

    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture_));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // ARGB8888 is byte order B,G,R,A on a little-endian host, which is what
    // GL_BGRA expects. The row length matters because SDL pads the pitch.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, surface_->pitch / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_BGRA,
                 GL_UNSIGNED_BYTE, surface_->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    // The filter pass letterboxes by narrowing the viewport. The overlay is
    // full-window, so it has to be widened back or the panel would be
    // squeezed into the picture area.
    glViewport(0, 0, width_, height_);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
    glEnd();

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

} // namespace emu::frontend
