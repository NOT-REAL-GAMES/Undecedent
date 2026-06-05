#include "undecedent/editor.hpp"
#include "undecedent/script_editor.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

undecedent::SectorPlane scripted_sector(const std::uint64_t id) {
    undecedent::SectorPlane sector;
    sector.id = id;
    sector.outer.vertices = {
        undecedent::Vec2{0.0F, 0.0F},
        undecedent::Vec2{64.0F, 0.0F},
        undecedent::Vec2{64.0F, 64.0F},
        undecedent::Vec2{0.0F, 64.0F},
    };
    return sector;
}

} // namespace

int main() {
    {
        undecedent::EditorWorld world;
        world.sectors.push_back(scripted_sector(44));
        world.selected_sector = 0;
        world.selected_sectors.insert(0);
        expect(
            undecedent::default_script_editor_target(world).kind == undecedent::ScriptTargetKind::Sector,
            "default target should choose selected sector before map"
        );

        undecedent::PlayerSpawn spawn;
        spawn.set = true;
        undecedent::set_player_spawn_entity(world.entities, spawn);
        undecedent::ensure_editor_stable_ids(world);
        undecedent::select_entity(world, undecedent::SelectedEntityRef{undecedent::SelectedEntityKind::PlayerSpawn, 0});
        const undecedent::ScriptTargetRef target = undecedent::default_script_editor_target(world);
        expect(target.kind == undecedent::ScriptTargetKind::Entity, "default target should prefer selected entity");
        expect(target.id != 0, "selected entity target should have a stable id");
    }

    {
        undecedent::EditorWorld world;
        undecedent::open_script_editor(world, undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Map, 0});
        undecedent::ScriptEditorDraft* draft = undecedent::active_script_editor_draft(world);
        expect(draft != nullptr, "map draft should exist");
        draft->source = "function on_map_start() { print(7); }";
        draft->dirty = true;
        expect(undecedent::script_editor_apply_current(world), "valid map script should apply");
        expect(world.scripts.has_global_script, "map apply should write global script");
        expect(world.dirty_scripts, "map apply should dirty scripts");
    }

    {
        undecedent::EditorWorld world;
        undecedent::PlayerSpawn spawn;
        spawn.set = true;
        const undecedent::EntityHandle entity = undecedent::set_player_spawn_entity(world.entities, spawn);
        undecedent::ensure_editor_stable_ids(world);
        const std::uint64_t id = undecedent::entity_stable_id(world.entities, entity);
        undecedent::open_script_editor(world, undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Entity, id});
        undecedent::ScriptEditorDraft* draft = undecedent::active_script_editor_draft(world);
        draft->source = "function on_tick() { self.transform.x = self.transform.x + 1; }";
        draft->dirty = true;
        expect(undecedent::script_editor_apply_current(world), "valid entity script should apply");
        expect(world.scripts.entity_scripts.contains(id), "entity apply should write entity script");
        expect(undecedent::script_component(world.entities, entity) != nullptr, "entity apply should attach script component");
    }

    {
        undecedent::EditorWorld world;
        world.sectors.push_back(scripted_sector(77));
        undecedent::open_script_editor(world, undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Sector, 77});
        undecedent::ScriptEditorDraft* draft = undecedent::active_script_editor_draft(world);
        draft->source = "function on_sector_enter() { print(2); }";
        draft->dirty = true;
        expect(undecedent::script_editor_apply_current(world), "valid sector script should apply");
        expect(world.scripts.sector_scripts.contains(77), "sector apply should write sector script");
    }

    {
        undecedent::EditorWorld world;
        undecedent::open_script_editor(world, undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Map, 0});
        undecedent::ScriptEditorDraft* draft = undecedent::active_script_editor_draft(world);
        draft->source = "function broken(";
        draft->dirty = true;
        expect(!undecedent::script_editor_apply_current(world), "invalid script should fail apply");
        expect(!world.scripts.has_global_script, "failed apply must not commit bytecode");
        expect(!world.dirty_scripts, "failed apply must not dirty committed scripts");
        expect(draft->dirty, "failed apply should preserve dirty draft");
        expect(draft->compile_error, "failed apply should mark compile error");
    }

    {
        undecedent::EditorWorld world;
        undecedent::open_script_editor(world, undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Map, 0});
        undecedent::ScriptEditorDraft* draft = undecedent::active_script_editor_draft(world);
        draft->source = "function broken(";
        draft->dirty = true;
        expect(!undecedent::script_editor_apply_dirty_before_save(world), "save gate should block invalid dirty draft");
        draft->source = "function on_map_start() { print(3); }";
        draft->dirty = true;
        expect(undecedent::script_editor_apply_dirty_before_save(world), "save gate should apply valid dirty draft");
        expect(world.scripts.has_global_script, "save gate should commit valid script");
        undecedent::script_editor_clear_clean_drafts(world);
        expect(world.script_editor.drafts.empty(), "successful save should clear clean source drafts");
    }

    return EXIT_SUCCESS;
}
