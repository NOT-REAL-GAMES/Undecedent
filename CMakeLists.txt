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
    src/csg.cpp
    src/debug_draw.cpp
    src/deferred_renderer.cpp
    src/displacement.cpp
    src/editor.cpp
    src/editor_render.cpp
    src/editor_ui.cpp
    src/game_control.cpp
    src/map_io.cpp
    src/materials.cpp
    src/math3d.cpp
    src/physics.cpp
    src/runtime_geometry.cpp
    src/runtime_render_cache.cpp
    src/runtime_render.cpp
    src/runtime_pick.cpp
    src/runtime_visibility.cpp
    src/runtime_world.cpp
    src/screen_draw.cpp
    src/shadow_atlas.cpp
    src/sdl_platform.cpp
    src/triangulator.cpp
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

add_executable(UndecedentTriangulatorTests
    tests/triangulator_tests.cpp
    src/triangulator.cpp
)

target_include_directories(UndecedentTriangulatorTests PRIVATE include)

add_executable(UndecedentCsgTests
    tests/csg_tests.cpp
    src/csg.cpp
    src/displacement.cpp
    src/triangulator.cpp
)

target_include_directories(UndecedentCsgTests PRIVATE include)

add_executable(UndecedentEditorSliceTests
    tests/editor_slice_tests.cpp
)

target_include_directories(UndecedentEditorSliceTests PRIVATE include)

add_executable(UndecedentRuntimeGeometryTests
    tests/runtime_geometry_tests.cpp
    src/csg.cpp
    src/displacement.cpp
    src/runtime_geometry.cpp
    src/triangulator.cpp
)

target_include_directories(UndecedentRuntimeGeometryTests PRIVATE include)

add_executable(UndecedentRuntimeWorldTests
    tests/runtime_world_tests.cpp
    src/csg.cpp
    src/displacement.cpp
    src/runtime_visibility.cpp
    src/runtime_world.cpp
    src/triangulator.cpp
)

target_include_directories(UndecedentRuntimeWorldTests PRIVATE include glad/include)

add_executable(UndecedentPhysicsTests
    tests/physics_tests.cpp
    src/csg.cpp
    src/displacement.cpp
    src/physics.cpp
    src/runtime_world.cpp
    src/triangulator.cpp
)

target_include_directories(UndecedentPhysicsTests PRIVATE include)

add_executable(UndecedentMapIoTests
    tests/map_io_tests.cpp
    src/displacement.cpp
    src/map_io.cpp
    src/triangulator.cpp
)

target_include_directories(UndecedentMapIoTests PRIVATE include)

add_executable(UndecedentDisplacementTests
    tests/displacement_tests.cpp
    src/displacement.cpp
    src/triangulator.cpp
)

target_include_directories(UndecedentDisplacementTests PRIVATE include)

add_executable(UndecedentMaterialsTests
    tests/materials_tests.cpp
    src/materials.cpp
)

target_include_directories(UndecedentMaterialsTests PRIVATE include)

add_executable(UndecedentShadowAtlasTests
    tests/shadow_atlas_tests.cpp
    src/shadow_atlas.cpp
)

target_include_directories(UndecedentShadowAtlasTests PRIVATE include)

add_executable(UndecedentEditorEntityTests
    tests/editor_entity_tests.cpp
    src/csg.cpp
    src/displacement.cpp
    src/editor.cpp
    src/materials.cpp
    src/math3d.cpp
    src/runtime_geometry.cpp
    src/runtime_pick.cpp
    src/runtime_render_cache.cpp
    src/runtime_world.cpp
    src/screen_draw.cpp
    src/triangulator.cpp
    glad/src/glad.c
)

target_include_directories(UndecedentEditorEntityTests PRIVATE include glad/include)
