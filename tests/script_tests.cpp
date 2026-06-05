#include "undecedent/script.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    {
        const undecedent::ScriptCompileResult compiled = undecedent::compile_script(R"(
            // map start comment
            function on_map_start() {
                print(12 + 5);
            }
        )");
        expect(compiled.ok, compiled.message.c_str());
        expect(!compiled.program.comments.empty(), "compiler should preserve comments");
        expect(!compiled.program.instructions.empty(), "compiler should emit bytecode");
        const std::string disassembly = undecedent::disassemble_script(compiled.program);
        expect(disassembly.find("on_map_start") != std::string::npos, "disassembly should include function names");
        expect(disassembly.find("map start comment") != std::string::npos, "disassembly should include comments");
    }

    {
        undecedent::EntityRegistry registry;
        const undecedent::EntityHandle entity = undecedent::create_entity(registry, 7);
        undecedent::add_transform(registry, entity, undecedent::TransformComponent{undecedent::Vec3{1, 2, 3}, 0.0F});

        const undecedent::ScriptCompileResult compiled = undecedent::compile_script(R"(
            function on_tick() {
                self.transform.x = self.transform.x + 4;
                entity(7).transform.y = 20;
            }
        )");
        expect(compiled.ok, compiled.message.c_str());
        undecedent::ScriptVm vm;
        const undecedent::ScriptRunResult run =
            undecedent::run_script_event(vm, registry, compiled.program, "on_tick", 7);
        expect(run.ok, run.message.c_str());
        const undecedent::TransformComponent* transform = undecedent::transform_component(registry, entity);
        expect(transform != nullptr && transform->position.x == 5.0F, "self transform write should work");
        expect(transform != nullptr && transform->position.y == 20.0F, "entity transform write should work");
    }

    {
        undecedent::EntityRegistry registry;
        const undecedent::EntityHandle light_entity = undecedent::create_entity(registry, 11);
        undecedent::add_transform(registry, light_entity, {});
        undecedent::add_point_light(registry, light_entity, undecedent::PointLightComponent{});

        const undecedent::ScriptCompileResult compiled = undecedent::compile_script(R"(
            function on_use() {
                self.point_light.radius = 512;
                self.point_light.intensity = self.point_light.intensity + 0.5;
                self.point_light.shadow_bias = 3;
            }
        )");
        expect(compiled.ok, compiled.message.c_str());
        undecedent::ScriptStore store;
        undecedent::attach_entity_script(store, registry, 11, compiled.program);
        undecedent::ScriptVm vm;
        const std::vector<undecedent::ScriptRunResult> results =
            undecedent::run_script_store_event(vm, registry, store, "on_use");
        expect(results.size() == 1 && results.front().ok, "entity script event should run");
        const undecedent::PointLightComponent* light = undecedent::point_light_component(registry, light_entity);
        expect(light != nullptr && light->radius == 512.0F, "point light radius should be writable");
        expect(light != nullptr && light->intensity == 2.0F, "point light intensity should be writable");
        expect(light != nullptr && light->shadow_bias == 3.0F, "point light shadow bias should be writable");
    }

    {
        undecedent::EntityRegistry registry;
        undecedent::PointLight light;
        light.id = 13;
        const undecedent::EntityHandle entity = undecedent::add_point_light_entity(registry, light);
        undecedent::add_script(registry, entity, undecedent::ScriptComponent{13, true});

        const undecedent::ScriptCompileResult compiled = undecedent::compile_script(R"(
            function on_tick() {
                print(class_type(self));
                print(is_a(self, CLASS_ENTITY));
                print(is_a(self, CLASS_POINT_LIGHT));
                print(has_component(self, COMPONENT_TRANSFORM));
                print(has_component(self, COMPONENT_POINT_LIGHT));
                print(has_component(self, COMPONENT_SCRIPT));
                print(component_mask(self));
                print(class_ancestry(self));
            }
        )");
        expect(compiled.ok, compiled.message.c_str());
        undecedent::ScriptVm vm;
        const undecedent::ScriptRunResult run =
            undecedent::run_script_event(vm, registry, compiled.program, "on_tick", 13);
        expect(run.ok, run.message.c_str());
        expect(vm.log.size() == 8, "introspection script should print all probes");
        expect(vm.log[0].find("3") != std::string::npos, "class_type should return point light class id");
        expect(vm.log[1].find("1") != std::string::npos, "is_a entity should be true");
        expect(vm.log[2].find("1") != std::string::npos, "is_a point light should be true");
        expect(vm.log[3].find("1") != std::string::npos, "has transform should be true");
        expect(vm.log[4].find("1") != std::string::npos, "has point light should be true");
        expect(vm.log[5].find("1") != std::string::npos, "has script should be true");
        expect(vm.log[6].find("58") != std::string::npos, "component mask should include transform, light, name, script");
        expect(vm.log[7].find("10") != std::string::npos, "class ancestry should include entity and point light");
    }

    {
        undecedent::ScriptProgram program;
        program.functions.push_back(undecedent::ScriptFunction{"on_tick", 0, 0});
        program.instructions.push_back(undecedent::ScriptInstruction{undecedent::ScriptOpcode::Jump, 0});
        undecedent::EntityRegistry registry;
        undecedent::ScriptVm vm;
        const undecedent::ScriptRunResult run =
            undecedent::run_script_event(vm, registry, program, "on_tick", 0, undecedent::ScriptVmConfig{8});
        expect(!run.ok, "infinite loop should trip the instruction budget");
    }

    {
        const undecedent::ScriptCompileResult compiled = undecedent::compile_script(R"(
            function on_map_start() { print(1); }
        )");
        expect(compiled.ok, compiled.message.c_str());
        undecedent::ScriptStore store;
        undecedent::set_global_script(store, compiled.program);
        std::ostringstream output;
        expect(undecedent::write_script_store_payload(output, store), "script payload write should succeed");
        std::istringstream input(output.str());
        undecedent::ScriptStore restored;
        std::string message;
        expect(undecedent::read_script_store_payload(input, restored, message), message.c_str());
        expect(restored.has_global_script, "script payload should restore global script");
        expect(!restored.global_script.instructions.empty(), "script payload should restore bytecode");
    }

    {
        const undecedent::ScriptCompileResult compiled = undecedent::compile_script(R"(
            function on_map_start() { print(11); }
            function on_tick() { print(22); }
        )");
        expect(compiled.ok, compiled.message.c_str());
        undecedent::ScriptStore store;
        undecedent::set_global_script(store, compiled.program);
        undecedent::EntityRegistry registry;
        undecedent::ScriptVm vm;

        const std::vector<undecedent::ScriptRunResult> start_results =
            undecedent::run_script_store_event(vm, registry, store, "on_map_start");
        expect(start_results.size() == 1 && start_results.front().ok, "on_map_start should dispatch");
        expect(!vm.log.empty() && vm.log.back().find("11") != std::string::npos, "on_map_start print should log");

        vm.log.clear();
        const std::vector<undecedent::ScriptRunResult> tick_results =
            undecedent::run_script_store_event(vm, registry, store, "on_tick");
        expect(tick_results.size() == 1 && tick_results.front().ok, "on_tick should dispatch");
        expect(!vm.log.empty() && vm.log.back().find("22") != std::string::npos, "on_tick print should log");
    }

    {
        const undecedent::ScriptCompileResult compiled = undecedent::compile_script(R"(
            function on_sector_enter() { print(1); }
            function on_sector_stay() { print(2); }
            function on_sector_exit() { print(3); }
        )");
        expect(compiled.ok, compiled.message.c_str());

        undecedent::ScriptStore store;
        undecedent::set_sector_script(store, 44, compiled.program);
        undecedent::EntityRegistry registry;
        undecedent::ScriptVm vm;
        std::uint64_t current_sector = 0;
        bool failed = false;

        const auto dispatch = [&](const std::uint64_t next_sector) {
            const auto run = [&](const std::uint64_t sector, const char* event_name) {
                const undecedent::ScriptRunResult result =
                    undecedent::run_sector_script_event(vm, registry, store, sector, event_name);
                if (!result.ok) {
                    failed = true;
                }
            };
            if (current_sector != next_sector) {
                if (current_sector != 0) {
                    run(current_sector, "on_sector_exit");
                }
                current_sector = next_sector;
                if (current_sector != 0) {
                    run(current_sector, "on_sector_enter");
                }
            }
            if (current_sector != 0) {
                run(current_sector, "on_sector_stay");
            }
        };

        dispatch(44);
        dispatch(44);
        dispatch(55);
        dispatch(0);
        dispatch(44);

        expect(!failed, "sector script dispatch should not fail");
        expect(vm.log.size() == 6, "sector transition sequence should produce expected event count");
        expect(vm.log[0].find("1") != std::string::npos, "first sector event should be enter");
        expect(vm.log[1].find("2") != std::string::npos, "enter should be followed by stay");
        expect(vm.log[2].find("2") != std::string::npos, "same sector should stay");
        expect(vm.log[3].find("3") != std::string::npos, "leaving scripted sector should exit");
        expect(vm.log[4].find("1") != std::string::npos, "re-entering sector should enter again");
        expect(vm.log[5].find("2") != std::string::npos, "re-entering sector should stay again");
    }

    {
        const undecedent::ScriptCompileResult compiled = undecedent::compile_script(R"(
            function on_tick() { print(3); }
        )");
        expect(compiled.ok, compiled.message.c_str());

        undecedent::ScriptStore store;
        undecedent::set_script_for_target(
            store,
            undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Map, 0},
            compiled.program
        );
        expect(
            undecedent::script_for_target(
                store,
                undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Map, 0}
            ) != nullptr,
            "target helper should find map script"
        );

        undecedent::set_script_for_target(
            store,
            undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Entity, 22},
            compiled.program
        );
        expect(
            undecedent::script_for_target(
                store,
                undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Entity, 22}
            ) != nullptr,
            "target helper should find entity script"
        );

        undecedent::set_script_for_target(
            store,
            undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Sector, 33},
            compiled.program
        );
        expect(
            undecedent::script_for_target(
                store,
                undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Sector, 33}
            ) != nullptr,
            "target helper should find sector script"
        );
        expect(
            undecedent::clear_script_for_target(
                store,
                undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Sector, 33}
            ),
            "target helper should clear sector script"
        );
        expect(
            undecedent::script_for_target(
                store,
                undecedent::ScriptTargetRef{undecedent::ScriptTargetKind::Sector, 33}
            ) == nullptr,
            "cleared sector script should not be found"
        );
    }

    {
        const undecedent::ScriptCompileResult compiled = undecedent::compile_script(R"(
            function on_sector_enter() { print(9); }
        )");
        expect(compiled.ok, compiled.message.c_str());
        undecedent::ScriptStore store;
        undecedent::set_global_script(store, compiled.program);
        undecedent::set_sector_script(store, 44, compiled.program);

        std::ostringstream output;
        expect(undecedent::write_script_store_payload(output, store), "scripts v2 payload write should succeed");
        expect(output.str().find("scripts 2") != std::string::npos, "script payload should write version 2");
        expect(output.str().find("sector_scripts 1") != std::string::npos, "script payload should include sector scripts");

        std::istringstream input(output.str());
        undecedent::ScriptStore restored;
        std::string message;
        expect(undecedent::read_script_store_payload(input, restored, message), message.c_str());
        expect(restored.has_global_script, "scripts v2 should restore global script");
        expect(restored.sector_scripts.contains(44), "scripts v2 should restore sector script");
    }

    {
        const std::string legacy_payload =
            "scripts 1\n"
            "global 0\n"
            "entity_scripts 0\n";
        std::istringstream input(legacy_payload);
        undecedent::ScriptStore restored;
        std::string message;
        expect(undecedent::read_script_store_payload(input, restored, message), message.c_str());
        expect(restored.sector_scripts.empty(), "scripts v1 should load with no sector scripts");
    }

    {
        const undecedent::ScriptCompileResult compiled = undecedent::compile_script(R"(
            function on_sector_enter() { print(1); }
        )");
        expect(compiled.ok, compiled.message.c_str());
        undecedent::ScriptStore store;
        undecedent::set_sector_script(store, 55, compiled.program);
        std::ostringstream output;
        expect(undecedent::write_script_store_payload(output, store), "scripts v2 duplicate setup should write");
        std::string payload = output.str();
        const std::string record = "sector_script 55";
        const std::size_t position = payload.find(record);
        expect(position != std::string::npos, "sector script record should exist");
        payload.insert(position, record + "\n" + payload.substr(payload.find("program", position), payload.size()));
        payload.replace(payload.find("sector_scripts 1"), std::string("sector_scripts 1").size(), "sector_scripts 2");

        std::istringstream input(payload);
        undecedent::ScriptStore restored;
        std::string message;
        expect(!undecedent::read_script_store_payload(input, restored, message), "duplicate sector script id should reject");
    }

    {
        const std::string bad_payload =
            "scripts 2\n"
            "global 0\n"
            "entity_scripts 0\n"
            "sector_scripts 1\n"
            "sector_script 0\n"
            "program 0 0 0\n";
        std::istringstream input(bad_payload);
        undecedent::ScriptStore restored;
        std::string message;
        expect(!undecedent::read_script_store_payload(input, restored, message), "zero sector script id should reject");
    }

    return EXIT_SUCCESS;
}
