cmake_minimum_required(VERSION 3.10)

project(Undecedent VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include(FetchContent)


FetchContent_Declare(
    SDL3 STATIC
    GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git"
    GIT_TAG "main"
)

FetchContent_MakeAvailable(SDL3)

find_package(OpenGL REQUIRED)

add_executable(Undecedent
    main.cpp
    glad/src/glad.c
)

target_include_directories(Undecedent PRIVATE include glad/include)
target_link_libraries(Undecedent PUBLIC OpenGL::GL SDL3::SDL3)

if(WIN32)
    add_custom_command(TARGET Undecedent POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:Undecedent>
    )
endif()
