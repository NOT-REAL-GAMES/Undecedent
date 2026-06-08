cmake_minimum_required(VERSION 3.20)

project(Undecedent VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(MSVC)
    add_compile_options(/FS)
    string(APPEND CMAKE_C_FLAGS_DEBUG " /FS /Z7")
    string(APPEND CMAKE_CXX_FLAGS_DEBUG " /FS /Z7")
endif()

include(FetchContent)

FetchContent_Declare(
    SDL3 STATIC
    GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git"
    GIT_TAG "main"
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(XZ_NLS OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_XZ OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_XZDEC OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_LZMADEC OFF CACHE BOOL "" FORCE)
set(XZ_TOOL_LZMAINFO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    xz
    GIT_REPOSITORY "https://github.com/tukaani-project/xz.git"
    GIT_TAG "v5.8.3"
)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_MANPAGES OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_BENCHMARK OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_JNI OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_SJPEG OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_OPENEXR OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_TRANSCODE_JPEG OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_PLUGINS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_VIEWERS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_DOXYGEN OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_JPEGLI OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_JPEGLI_LIBJPEG OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libjxl
    GIT_REPOSITORY "https://github.com/libjxl/libjxl.git"
    GIT_TAG "v0.11.2"
    GIT_SUBMODULES_RECURSE TRUE
)

FetchContent_MakeAvailable(SDL3 xz libjxl)

find_package(OpenGL REQUIRED)

add_library(UndecedentTextureCodecs STATIC
    src/texture_compression.cpp
    src/texture_image_codec.cpp
)

target_include_directories(UndecedentTextureCodecs PUBLIC include "${xz_SOURCE_DIR}/src/liblzma/api")
target_link_libraries(UndecedentTextureCodecs PUBLIC SDL3::SDL3 liblzma jxl)
target_link_libraries(UndecedentTextureCodecs PUBLIC $<$<TARGET_EXISTS:jxl_threads>:jxl_threads>)

add_executable(Undecedent
    main.cpp
    src/core_draw.cpp
    src/csg.cpp
    src/debug_draw.cpp
    src/deferred_renderer.cpp
    src/displacement.cpp
    src/editor.cpp
    src/editor_render.cpp
    src/editor_ui.cpp
    src/entity.cpp
    src/game_control.cpp
    src/map_io.cpp
    src/material_texture.cpp
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
    src/sdf_text.cpp
    src/shadow_cache.cpp
    src/shadow_atlas.cpp
    src/sdl_platform.cpp
    src/script.cpp
    src/script_editor.cpp
    src/script_editor_ui.cpp
    src/triangulator.cpp
    glad/src/glad.c
)

target_include_directories(Undecedent PRIVATE include glad/include)
target_link_libraries(Undecedent PUBLIC OpenGL::GL SDL3::SDL3 UndecedentTextureCodecs)

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

add_executable(UndecedentCoreDrawTests
    tests/core_draw_tests.cpp
    src/core_draw.cpp
    glad/src/glad.c
)

target_include_directories(UndecedentCoreDrawTests PRIVATE include glad/include)

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
    src/entity.cpp
    src/map_io.cpp
    src/materials.cpp
    src/script.cpp
    src/triangulator.cpp
)

target_include_directories(UndecedentMapIoTests PRIVATE include)
target_link_libraries(UndecedentMapIoTests PRIVATE UndecedentTextureCodecs)

add_executable(UndecedentScriptTests
    tests/script_tests.cpp
    src/entity.cpp
    src/script.cpp
)

target_include_directories(UndecedentScriptTests PRIVATE include)

add_executable(UndecedentScriptEditorTests
    tests/script_editor_tests.cpp
    src/core_draw.cpp
    src/csg.cpp
    src/displacement.cpp
    src/editor.cpp
    src/entity.cpp
    src/materials.cpp
    src/math3d.cpp
    src/runtime_geometry.cpp
    src/runtime_pick.cpp
    src/runtime_render_cache.cpp
    src/runtime_world.cpp
    src/screen_draw.cpp
    src/script.cpp
    src/script_editor.cpp
    src/triangulator.cpp
    glad/src/glad.c
)

target_include_directories(UndecedentScriptEditorTests PRIVATE include glad/include)

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

add_executable(UndecedentTextureCodecTests
    tests/texture_codec_tests.cpp
)

target_include_directories(UndecedentTextureCodecTests PRIVATE include)
target_link_libraries(UndecedentTextureCodecTests PRIVATE UndecedentTextureCodecs)

add_executable(UndecedentSdfTextTests
    tests/sdf_text_tests.cpp
    src/sdf_text.cpp
    glad/src/glad.c
)

target_include_directories(UndecedentSdfTextTests PRIVATE include glad/include)

add_executable(UndecedentShadowAtlasTests
    tests/shadow_atlas_tests.cpp
    src/shadow_cache.cpp
    src/shadow_atlas.cpp
)

target_include_directories(UndecedentShadowAtlasTests PRIVATE include)

add_executable(UndecedentEntityTests
    tests/entity_tests.cpp
    src/entity.cpp
)

target_include_directories(UndecedentEntityTests PRIVATE include)

add_executable(UndecedentEditorEntityTests
    tests/editor_entity_tests.cpp
    src/core_draw.cpp
    src/csg.cpp
    src/displacement.cpp
    src/editor.cpp
    src/materials.cpp
    src/math3d.cpp
    src/entity.cpp
    src/runtime_geometry.cpp
    src/runtime_pick.cpp
    src/runtime_render_cache.cpp
    src/runtime_world.cpp
    src/screen_draw.cpp
    src/script.cpp
    src/triangulator.cpp
    glad/src/glad.c
)

target_include_directories(UndecedentEditorEntityTests PRIVATE include glad/include)
