#include "undecedent/sdl_platform.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace undecedent {
namespace {

std::string format_display_mode(const SDL_DisplayMode& mode) {
    std::ostringstream stream;
    stream << mode.w << 'x' << mode.h;
    if (mode.refresh_rate > 0.0F) {
        stream << '@' << std::fixed << std::setprecision(2) << mode.refresh_rate << "Hz";
    }
    return stream.str();
}

} // namespace

bool configure_gl_attributes() {
    bool ok = true;
    ok = SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) && ok;
    ok = SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) && ok;
    ok = SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY) && ok;
    ok = SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) && ok;
    ok = SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24) && ok;
    return ok;
}

void log_sdl_error(const char* action) {
    std::cerr << action << ": " << SDL_GetError() << '\n';
}

bool enter_exclusive_fullscreen(SDL_Window* window, FullscreenState& fullscreen_state) {
    if (fullscreen_state.exclusive) {
        return true;
    }

    SDL_GetWindowPosition(window, &fullscreen_state.windowed_x, &fullscreen_state.windowed_y);
    SDL_GetWindowSize(window, &fullscreen_state.windowed_w, &fullscreen_state.windowed_h);

    const SDL_DisplayID display_id = SDL_GetDisplayForWindow(window);
    if (display_id == 0) {
        log_sdl_error("SDL_GetDisplayForWindow failed");
        return false;
    }

    const SDL_DisplayMode* desktop_mode = SDL_GetDesktopDisplayMode(display_id);
    if (desktop_mode == nullptr) {
        log_sdl_error("SDL_GetDesktopDisplayMode failed");
        return false;
    }

    SDL_DisplayMode fullscreen_mode{};
    if (!SDL_GetClosestFullscreenDisplayMode(
            display_id,
            desktop_mode->w,
            desktop_mode->h,
            desktop_mode->refresh_rate,
            true,
            &fullscreen_mode
        )) {
        log_sdl_error("SDL_GetClosestFullscreenDisplayMode failed");
        return false;
    }

    if (!SDL_SetWindowFullscreenMode(window, &fullscreen_mode)) {
        log_sdl_error("SDL_SetWindowFullscreenMode failed");
        return false;
    }

    if (!SDL_SetWindowFullscreen(window, true)) {
        log_sdl_error("SDL_SetWindowFullscreen failed");
        SDL_SetWindowFullscreenMode(window, nullptr);
        return false;
    }

    if (!SDL_SyncWindow(window)) {
        log_sdl_error("SDL_SyncWindow after fullscreen enter failed");
        SDL_SetWindowFullscreen(window, false);
        SDL_SetWindowFullscreenMode(window, nullptr);
        return false;
    }

    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0) {
        std::cerr << "Exclusive fullscreen request was accepted but the window did not enter fullscreen\n";
        SDL_SetWindowFullscreenMode(window, nullptr);
        return false;
    }

    fullscreen_state.exclusive = true;
    std::cout << "Exclusive fullscreen enabled: " << format_display_mode(fullscreen_mode) << '\n';
    return true;
}

void exit_exclusive_fullscreen(SDL_Window* window, FullscreenState& fullscreen_state) {
    if (!fullscreen_state.exclusive) {
        return;
    }

    if (!SDL_SetWindowFullscreen(window, false)) {
        log_sdl_error("SDL_SetWindowFullscreen(false) failed");
        return;
    }

    if (!SDL_SyncWindow(window)) {
        log_sdl_error("SDL_SyncWindow after fullscreen exit failed");
    }

    if (!SDL_SetWindowFullscreenMode(window, nullptr)) {
        log_sdl_error("SDL_SetWindowFullscreenMode(nullptr) failed");
    }

    SDL_SetWindowSize(window, fullscreen_state.windowed_w, fullscreen_state.windowed_h);
    SDL_SetWindowPosition(window, fullscreen_state.windowed_x, fullscreen_state.windowed_y);
    fullscreen_state.exclusive = false;
    std::cout << "Exclusive fullscreen disabled\n";
}

void toggle_exclusive_fullscreen(SDL_Window* window, FullscreenState& fullscreen_state) {
    if (fullscreen_state.exclusive) {
        exit_exclusive_fullscreen(window, fullscreen_state);
    } else {
        enter_exclusive_fullscreen(window, fullscreen_state);
    }
}

} // namespace undecedent
