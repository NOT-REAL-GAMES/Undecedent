#include <cstdlib>
#include <iostream>

#include <glad/glad.h>
#include <SDL3/SDL.h>

namespace {
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

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
} // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log_sdl_error("SDL_Init failed");
        return EXIT_FAILURE;
    }

    if (!configure_gl_attributes()) {
        log_sdl_error("OpenGL attribute setup failed");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Undecedent",
        kWindowWidth,
        kWindowHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (window == nullptr) {
        log_sdl_error("SDL_CreateWindow failed");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        log_sdl_error("SDL_GL_CreateContext failed");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::cerr << "gladLoadGLLoader failed\n";
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GL_SetSwapInterval(0);

    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode key = event.key.key;
                if (key == SDLK_ESCAPE || key == SDLK_Q) {
                    running = false;
                }
            }
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.02F, 0.025F, 0.03F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
