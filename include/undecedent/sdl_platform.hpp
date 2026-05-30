#pragma once

#include <SDL3/SDL.h>

namespace undecedent {

struct FullscreenState {
    bool exclusive = false;
    int windowed_x = SDL_WINDOWPOS_UNDEFINED;
    int windowed_y = SDL_WINDOWPOS_UNDEFINED;
    int windowed_w = 1280;
    int windowed_h = 720;
};

bool configure_gl_attributes();
void log_sdl_error(const char* action);
bool enter_exclusive_fullscreen(SDL_Window* window, FullscreenState& fullscreen_state);
void exit_exclusive_fullscreen(SDL_Window* window, FullscreenState& fullscreen_state);
void toggle_exclusive_fullscreen(SDL_Window* window, FullscreenState& fullscreen_state);

} // namespace undecedent
