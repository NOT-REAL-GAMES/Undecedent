#pragma once

#include "undecedent/materials.hpp"

#include <glad/glad.h>

namespace undecedent {

struct MaterialTextureArray {
    GLuint texture = 0;
    MaterialLibrary uploaded_library;
    bool dirty = true;
};

void mark_material_textures_dirty(MaterialTextureArray& textures);
bool ensure_material_texture_array(MaterialTextureArray& textures, const MaterialLibrary& library);
void destroy_material_texture_array(MaterialTextureArray& textures);

} // namespace undecedent
