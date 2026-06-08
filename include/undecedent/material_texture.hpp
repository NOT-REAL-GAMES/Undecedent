#pragma once

#include "undecedent/materials.hpp"

#include <glad/glad.h>

#include <array>

namespace undecedent {

struct MaterialTextureArray {
    std::array<GLuint, kMaterialTextureChannelCount> textures{};
    MaterialLibrary uploaded_library;
    bool dirty = true;
};

GLuint material_texture_array_id(const MaterialTextureArray& textures, MaterialTextureChannel channel);
void mark_material_textures_dirty(MaterialTextureArray& textures);
bool ensure_material_texture_array(MaterialTextureArray& textures, const MaterialLibrary& library);
void destroy_material_texture_array(MaterialTextureArray& textures);

} // namespace undecedent
