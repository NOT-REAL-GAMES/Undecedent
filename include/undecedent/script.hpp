#pragma once

#include "undecedent/entity.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

namespace undecedent {

enum class ScriptOpcode {
    PushNumber,
    PushEntity,
    PushSelf,
    LoadLocal,
    StoreLocal,
    Add,
    Subtract,
    Multiply,
    Divide,
    Less,
    Greater,
    Equal,
    Jump,
    JumpIfFalse,
    GetTransformX,
    GetTransformY,
    GetTransformZ,
    GetTransformYaw,
    SetTransformX,
    SetTransformY,
    SetTransformZ,
    SetTransformYaw,
    GetPointLightRadius,
    GetPointLightIntensity,
    GetPointLightShadowBias,
    SetPointLightRadius,
    SetPointLightIntensity,
    SetPointLightShadowBias,
    ClassType,
    ClassAncestry,
    ComponentMask,
    IsA,
    HasComponent,
    Print,
    Pop,
    Return,
};

struct ScriptInstruction {
    ScriptOpcode opcode = ScriptOpcode::Return;
    int operand_i = 0;
    float operand_f = 0.0F;
    std::uint64_t operand_u = 0;
};

struct ScriptFunction {
    std::string name;
    int entry = 0;
    int arity = 0;
};

struct ScriptComment {
    int line = 0;
    std::string text;
};

struct ScriptProgram {
    std::vector<ScriptInstruction> instructions;
    std::vector<ScriptFunction> functions;
    std::vector<ScriptComment> comments;
};

struct ScriptStore {
    bool has_global_script = false;
    ScriptProgram global_script;
    std::unordered_map<std::uint64_t, ScriptProgram> entity_scripts;
    std::unordered_map<std::uint64_t, ScriptProgram> sector_scripts;
};

enum class ScriptTargetKind {
    Map,
    Entity,
    Sector,
};

struct ScriptTargetRef {
    ScriptTargetKind kind = ScriptTargetKind::Map;
    std::uint64_t id = 0;
};

struct ScriptCompileResult {
    bool ok = false;
    std::string message;
    ScriptProgram program;
};

struct ScriptRunResult {
    bool ok = false;
    std::string message;
    int instructions_executed = 0;
};

struct ScriptVmConfig {
    int instruction_budget = 4096;
};

struct ScriptVm {
    std::vector<std::string> log;
};

ScriptCompileResult compile_script(const std::string& source);
std::string disassemble_script(const ScriptProgram& program);
bool write_script_store_payload(std::ostream& output, const ScriptStore& scripts);
bool read_script_store_payload(std::istream& input, ScriptStore& scripts, std::string& message);

ScriptRunResult run_script_event(
    ScriptVm& vm,
    EntityRegistry& registry,
    const ScriptProgram& program,
    const std::string& event_name,
    std::uint64_t self_entity_id = 0,
    ScriptVmConfig config = {}
);

std::vector<ScriptRunResult> run_script_store_event(
    ScriptVm& vm,
    EntityRegistry& registry,
    const ScriptStore& scripts,
    const std::string& event_name,
    ScriptVmConfig config = {}
);

ScriptRunResult run_sector_script_event(
    ScriptVm& vm,
    EntityRegistry& registry,
    const ScriptStore& scripts,
    std::uint64_t sector_id,
    const std::string& event_name,
    ScriptVmConfig config = {}
);

void attach_entity_script(ScriptStore& scripts, EntityRegistry& registry, std::uint64_t entity_id, ScriptProgram program);
void detach_entity_script(ScriptStore& scripts, EntityRegistry& registry, std::uint64_t entity_id);
void set_global_script(ScriptStore& scripts, ScriptProgram program);
void clear_global_script(ScriptStore& scripts);
const ScriptProgram* script_for_target(const ScriptStore& scripts, ScriptTargetRef target);
void set_script_for_target(ScriptStore& scripts, ScriptTargetRef target, ScriptProgram program);
bool clear_script_for_target(ScriptStore& scripts, ScriptTargetRef target);
void set_sector_script(ScriptStore& scripts, std::uint64_t sector_id, ScriptProgram program);
bool clear_sector_script(ScriptStore& scripts, std::uint64_t sector_id);

const char* script_opcode_name(ScriptOpcode opcode);
bool script_opcode_from_name(const std::string& name, ScriptOpcode& opcode);

} // namespace undecedent
