#include "undecedent/script.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace undecedent {
namespace {

struct ScriptValue {
    enum class Kind {
        Number,
        Entity,
    };

    Kind kind = Kind::Number;
    float number = 0.0F;
    std::uint64_t entity_id = 0;
};

ScriptValue number_value(const float value) {
    return ScriptValue{ScriptValue::Kind::Number, value, 0};
}

ScriptValue entity_value(const std::uint64_t value) {
    return ScriptValue{ScriptValue::Kind::Entity, 0.0F, value};
}

bool truthy(const ScriptValue value) {
    return value.kind == ScriptValue::Kind::Entity ? value.entity_id != 0 : std::abs(value.number) > 0.00001F;
}

float number_or_zero(const ScriptValue value) {
    return value.kind == ScriptValue::Kind::Number ? value.number : static_cast<float>(value.entity_id);
}

std::uint64_t entity_or_zero(const ScriptValue value) {
    return value.kind == ScriptValue::Kind::Entity ? value.entity_id : static_cast<std::uint64_t>(value.number);
}

bool has_function(const ScriptProgram& program, const std::string& name) {
    return std::any_of(program.functions.begin(), program.functions.end(), [&name](const ScriptFunction& function) {
        return function.name == name;
    });
}

enum class TokenKind {
    Identifier,
    Number,
    Symbol,
    End,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    float number = 0.0F;
    int line = 1;
};

struct TokenizeResult {
    bool ok = false;
    std::string message;
    std::vector<Token> tokens;
    std::vector<ScriptComment> comments;
};

bool identifier_start(const char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool identifier_body(const char c) {
    return identifier_start(c) || (c >= '0' && c <= '9');
}

TokenizeResult tokenize(const std::string& source) {
    TokenizeResult result;
    int line = 1;
    for (std::size_t i = 0; i < source.size();) {
        const char c = source[i];
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            const std::size_t start = i + 2;
            i = start;
            while (i < source.size() && source[i] != '\n') {
                ++i;
            }
            result.comments.push_back(ScriptComment{line, source.substr(start, i - start)});
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            const int start_line = line;
            const std::size_t start = i + 2;
            i = start;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) {
                if (source[i] == '\n') {
                    ++line;
                }
                ++i;
            }
            if (i + 1 >= source.size()) {
                result.message = "Unterminated block comment.";
                return result;
            }
            result.comments.push_back(ScriptComment{start_line, source.substr(start, i - start)});
            i += 2;
            continue;
        }
        if (identifier_start(c)) {
            const std::size_t start = i++;
            while (i < source.size() && identifier_body(source[i])) {
                ++i;
            }
            result.tokens.push_back(Token{TokenKind::Identifier, source.substr(start, i - start), 0.0F, line});
            continue;
        }
        if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < source.size() && source[i + 1] >= '0' && source[i + 1] <= '9')) {
            const std::size_t start = i++;
            while (i < source.size() && ((source[i] >= '0' && source[i] <= '9') || source[i] == '.')) {
                ++i;
            }
            const std::string text = source.substr(start, i - start);
            std::istringstream stream(text);
            float value = 0.0F;
            stream >> value;
            if (!stream || !std::isfinite(value)) {
                result.message = "Invalid number literal.";
                return result;
            }
            result.tokens.push_back(Token{TokenKind::Number, text, value, line});
            continue;
        }
        if (i + 1 < source.size()) {
            const std::string two = source.substr(i, 2);
            if (two == "==" || two == "!=" || two == "<=" || two == ">=") {
                result.tokens.push_back(Token{TokenKind::Symbol, two, 0.0F, line});
                i += 2;
                continue;
            }
        }
        result.tokens.push_back(Token{TokenKind::Symbol, std::string(1, c), 0.0F, line});
        ++i;
    }
    result.tokens.push_back(Token{TokenKind::End, "", 0.0F, line});
    result.ok = true;
    return result;
}

struct Compiler {
    std::vector<Token> tokens;
    std::size_t cursor = 0;
    ScriptProgram program;
    std::string message;
    std::unordered_map<std::string, int> locals;

    const Token& peek(const int offset = 0) const {
        const std::size_t index = std::min(cursor + static_cast<std::size_t>(std::max(offset, 0)), tokens.size() - 1U);
        return tokens[index];
    }

    bool at_end() const {
        return peek().kind == TokenKind::End;
    }

    bool match(const std::string& text) {
        if (peek().text != text) {
            return false;
        }
        ++cursor;
        return true;
    }

    bool expect(const std::string& text) {
        if (match(text)) {
            return true;
        }
        std::ostringstream stream;
        stream << "Line " << peek().line << ": expected '" << text << "' but found '" << peek().text << "'.";
        message = stream.str();
        return false;
    }

    bool expect_identifier(std::string& out) {
        if (peek().kind != TokenKind::Identifier) {
            std::ostringstream stream;
            stream << "Line " << peek().line << ": expected identifier.";
            message = stream.str();
            return false;
        }
        out = peek().text;
        ++cursor;
        return true;
    }

    void emit(const ScriptOpcode opcode, const int operand_i = 0, const float operand_f = 0.0F, const std::uint64_t operand_u = 0) {
        program.instructions.push_back(ScriptInstruction{opcode, operand_i, operand_f, operand_u});
    }

    int emit_jump(const ScriptOpcode opcode) {
        const int index = static_cast<int>(program.instructions.size());
        emit(opcode, -1);
        return index;
    }

    void patch_jump(const int index) {
        if (index >= 0 && index < static_cast<int>(program.instructions.size())) {
            program.instructions[static_cast<std::size_t>(index)].operand_i = static_cast<int>(program.instructions.size());
        }
    }

    int local_index(const std::string& name) {
        const auto found = locals.find(name);
        if (found != locals.end()) {
            return found->second;
        }
        const int index = static_cast<int>(locals.size());
        locals[name] = index;
        return index;
    }

    bool compile_program() {
        while (!at_end()) {
            if (!compile_function()) {
                return false;
            }
        }
        return true;
    }

    bool compile_function() {
        if (match("function")) {
        } else if (match("void")) {
        } else {
            message = "Expected function declaration.";
            return false;
        }

        std::string name;
        if (!expect_identifier(name) || !expect("(") || !expect(")")) {
            return false;
        }
        locals.clear();
        const int entry = static_cast<int>(program.instructions.size());
        program.functions.push_back(ScriptFunction{name, entry, 0});
        if (!expect("{")) {
            return false;
        }
        while (!at_end() && peek().text != "}") {
            if (!compile_statement()) {
                return false;
            }
        }
        if (!expect("}")) {
            return false;
        }
        emit(ScriptOpcode::Return);
        return true;
    }

    bool compile_statement() {
        if (match("var")) {
            std::string name;
            if (!expect_identifier(name)) {
                return false;
            }
            const int index = local_index(name);
            if (match("=")) {
                if (!compile_expression()) {
                    return false;
                }
            } else {
                emit(ScriptOpcode::PushNumber, 0, 0.0F);
            }
            emit(ScriptOpcode::StoreLocal, index);
            return expect(";");
        }
        if (match("return")) {
            if (peek().text != ";") {
                if (!compile_expression()) {
                    return false;
                }
                emit(ScriptOpcode::Pop);
            }
            emit(ScriptOpcode::Return);
            return expect(";");
        }
        if (match("if")) {
            if (!expect("(") || !compile_expression() || !expect(")")) {
                return false;
            }
            const int false_jump = emit_jump(ScriptOpcode::JumpIfFalse);
            if (!compile_block_or_statement()) {
                return false;
            }
            if (match("else")) {
                const int end_jump = emit_jump(ScriptOpcode::Jump);
                patch_jump(false_jump);
                if (!compile_block_or_statement()) {
                    return false;
                }
                patch_jump(end_jump);
            } else {
                patch_jump(false_jump);
            }
            return true;
        }
        if (match("while")) {
            const int loop_start = static_cast<int>(program.instructions.size());
            if (!expect("(") || !compile_expression() || !expect(")")) {
                return false;
            }
            const int exit_jump = emit_jump(ScriptOpcode::JumpIfFalse);
            if (!compile_block_or_statement()) {
                return false;
            }
            emit(ScriptOpcode::Jump, loop_start);
            patch_jump(exit_jump);
            return true;
        }

        if (peek().kind == TokenKind::Identifier && peek(1).text == "=") {
            const std::string name = peek().text;
            cursor += 2;
            if (!compile_expression()) {
                return false;
            }
            emit(ScriptOpcode::StoreLocal, local_index(name));
            return expect(";");
        }

        if (looks_like_component_assignment()) {
            return compile_component_assignment_statement();
        }

        if (!compile_expression()) {
            return false;
        }
        emit(ScriptOpcode::Pop);
        return expect(";");
    }

    bool compile_block_or_statement() {
        if (match("{")) {
            while (!at_end() && peek().text != "}") {
                if (!compile_statement()) {
                    return false;
                }
            }
            return expect("}");
        }
        return compile_statement();
    }

    bool looks_like_component_assignment() const {
        std::size_t i = cursor;
        if (tokens[i].text == "self") {
            ++i;
        } else if (tokens[i].text == "entity") {
            i += 4;
        } else {
            return false;
        }
        while (i < tokens.size() && tokens[i].text != ";" && tokens[i].text != "}") {
            if (tokens[i].text == "=") {
                return true;
            }
            ++i;
        }
        return false;
    }

    bool compile_component_assignment_statement() {
        ScriptOpcode setter = ScriptOpcode::SetTransformX;
        if (!compile_entity_reference() || !compile_property_opcode(true, setter) || !expect("=") || !compile_expression()) {
            return false;
        }
        emit(setter);
        return expect(";");
    }

    bool compile_expression() {
        return compile_comparison();
    }

    bool compile_comparison() {
        if (!compile_term()) {
            return false;
        }
        while (peek().text == "<" || peek().text == ">" || peek().text == "==") {
            const std::string op = peek().text;
            ++cursor;
            if (!compile_term()) {
                return false;
            }
            if (op == "<") {
                emit(ScriptOpcode::Less);
            } else if (op == ">") {
                emit(ScriptOpcode::Greater);
            } else {
                emit(ScriptOpcode::Equal);
            }
        }
        return true;
    }

    bool compile_term() {
        if (!compile_factor()) {
            return false;
        }
        while (peek().text == "+" || peek().text == "-") {
            const std::string op = peek().text;
            ++cursor;
            if (!compile_factor()) {
                return false;
            }
            emit(op == "+" ? ScriptOpcode::Add : ScriptOpcode::Subtract);
        }
        return true;
    }

    bool compile_factor() {
        if (!compile_unary()) {
            return false;
        }
        while (peek().text == "*" || peek().text == "/") {
            const std::string op = peek().text;
            ++cursor;
            if (!compile_unary()) {
                return false;
            }
            emit(op == "*" ? ScriptOpcode::Multiply : ScriptOpcode::Divide);
        }
        return true;
    }

    bool compile_unary() {
        if (match("-")) {
            emit(ScriptOpcode::PushNumber, 0, 0.0F);
            if (!compile_unary()) {
                return false;
            }
            emit(ScriptOpcode::Subtract);
            return true;
        }
        return compile_primary();
    }

    bool compile_primary() {
        if (peek().kind == TokenKind::Number) {
            emit(ScriptOpcode::PushNumber, 0, peek().number);
            ++cursor;
            return true;
        }
        if (match("(")) {
            return compile_expression() && expect(")");
        }
        if (match("print")) {
            if (!expect("(") || !compile_expression() || !expect(")")) {
                return false;
            }
            emit(ScriptOpcode::Print);
            emit(ScriptOpcode::PushNumber, 0, 0.0F);
            return true;
        }
        if (match("class_type")) {
            if (!expect("(") || !compile_expression() || !expect(")")) {
                return false;
            }
            emit(ScriptOpcode::ClassType);
            return true;
        }
        if (match("class_ancestry")) {
            if (!expect("(") || !compile_expression() || !expect(")")) {
                return false;
            }
            emit(ScriptOpcode::ClassAncestry);
            return true;
        }
        if (match("component_mask")) {
            if (!expect("(") || !compile_expression() || !expect(")")) {
                return false;
            }
            emit(ScriptOpcode::ComponentMask);
            return true;
        }
        if (match("is_a")) {
            if (!expect("(") || !compile_expression() || !expect(",") || !compile_expression() || !expect(")")) {
                return false;
            }
            emit(ScriptOpcode::IsA);
            return true;
        }
        if (match("has_component")) {
            if (!expect("(") || !compile_expression() || !expect(",") || !compile_expression() || !expect(")")) {
                return false;
            }
            emit(ScriptOpcode::HasComponent);
            return true;
        }
        if (peek().text == "self" || peek().text == "entity") {
            if (!compile_entity_reference()) {
                return false;
            }
            if (peek().text == ".") {
                ScriptOpcode getter = ScriptOpcode::GetTransformX;
                if (!compile_property_opcode(false, getter)) {
                    return false;
                }
                emit(getter);
            }
            return true;
        }
        if (peek().kind == TokenKind::Identifier) {
            const std::string name = peek().text;
            ++cursor;
            float constant = 0.0F;
            if (constant_number(name, constant)) {
                emit(ScriptOpcode::PushNumber, 0, constant);
                return true;
            }
            emit(ScriptOpcode::LoadLocal, local_index(name));
            return true;
        }
        message = "Expected expression.";
        return false;
    }

    bool constant_number(const std::string& name, float& value) const {
        if (name == "CLASS_ENTITY") {
            value = static_cast<float>(kEntityClassEntity);
        } else if (name == "CLASS_PLAYER_SPAWN") {
            value = static_cast<float>(kEntityClassPlayerSpawn);
        } else if (name == "CLASS_POINT_LIGHT") {
            value = static_cast<float>(kEntityClassPointLight);
        } else if (name == "COMPONENT_TRANSFORM") {
            value = static_cast<float>(kComponentTransform);
        } else if (name == "COMPONENT_PLAYER_SPAWN") {
            value = static_cast<float>(kComponentPlayerSpawn);
        } else if (name == "COMPONENT_POINT_LIGHT") {
            value = static_cast<float>(kComponentPointLight);
        } else if (name == "COMPONENT_NAME") {
            value = static_cast<float>(kComponentName);
        } else if (name == "COMPONENT_SCRIPT") {
            value = static_cast<float>(kComponentScript);
        } else {
            return false;
        }
        return true;
    }

    bool compile_entity_reference() {
        if (match("self")) {
            emit(ScriptOpcode::PushSelf);
            return true;
        }
        if (!match("entity") || !expect("(")) {
            message = "Expected entity reference.";
            return false;
        }
        if (peek().kind != TokenKind::Number || peek().number < 0.0F) {
            message = "Expected non-negative entity id.";
            return false;
        }
        emit(ScriptOpcode::PushEntity, 0, 0.0F, static_cast<std::uint64_t>(peek().number));
        ++cursor;
        return expect(")");
    }

    bool compile_property_opcode(const bool setter, ScriptOpcode& opcode) {
        if (!expect(".")) {
            return false;
        }
        std::string component;
        std::string field;
        if (!expect_identifier(component) || !expect(".") || !expect_identifier(field)) {
            return false;
        }
        if (component == "transform") {
            if (field == "x") {
                opcode = setter ? ScriptOpcode::SetTransformX : ScriptOpcode::GetTransformX;
            } else if (field == "y") {
                opcode = setter ? ScriptOpcode::SetTransformY : ScriptOpcode::GetTransformY;
            } else if (field == "z") {
                opcode = setter ? ScriptOpcode::SetTransformZ : ScriptOpcode::GetTransformZ;
            } else if (field == "yaw") {
                opcode = setter ? ScriptOpcode::SetTransformYaw : ScriptOpcode::GetTransformYaw;
            } else {
                message = "Unknown transform field.";
                return false;
            }
            return true;
        }
        if (component == "point_light") {
            if (field == "radius") {
                opcode = setter ? ScriptOpcode::SetPointLightRadius : ScriptOpcode::GetPointLightRadius;
            } else if (field == "intensity") {
                opcode = setter ? ScriptOpcode::SetPointLightIntensity : ScriptOpcode::GetPointLightIntensity;
            } else if (field == "shadow_bias") {
                opcode = setter ? ScriptOpcode::SetPointLightShadowBias : ScriptOpcode::GetPointLightShadowBias;
            } else {
                message = "Unknown point_light field.";
                return false;
            }
            return true;
        }
        message = "Unknown script component API.";
        return false;
    }
};

bool pop_value(std::vector<ScriptValue>& stack, ScriptValue& out, std::string& message) {
    if (stack.empty()) {
        message = "Script stack underflow.";
        return false;
    }
    out = stack.back();
    stack.pop_back();
    return true;
}

TransformComponent* transform_by_id(EntityRegistry& registry, const std::uint64_t id) {
    return transform_component(registry, find_entity_by_stable_id(registry, id));
}

PointLightComponent* point_light_by_id(EntityRegistry& registry, const std::uint64_t id) {
    return point_light_component(registry, find_entity_by_stable_id(registry, id));
}

bool read_program(std::istream& input, ScriptProgram& program, std::string& message) {
    std::string token;
    if (!(input >> token) || token != "program") {
        message = "Expected script program.";
        return false;
    }
    std::size_t instruction_count = 0;
    std::size_t function_count = 0;
    std::size_t comment_count = 0;
    if (!(input >> instruction_count >> function_count >> comment_count)) {
        message = "Malformed script program header.";
        return false;
    }
    program = {};
    for (std::size_t i = 0; i < function_count; ++i) {
        ScriptFunction function;
        if (!(input >> token) || token != "function" ||
            !(input >> std::quoted(function.name) >> function.entry >> function.arity)) {
            message = "Malformed script function record.";
            return false;
        }
        program.functions.push_back(std::move(function));
    }
    for (std::size_t i = 0; i < comment_count; ++i) {
        ScriptComment comment;
        if (!(input >> token) || token != "comment" || !(input >> comment.line >> std::quoted(comment.text))) {
            message = "Malformed script comment record.";
            return false;
        }
        program.comments.push_back(std::move(comment));
    }
    for (std::size_t i = 0; i < instruction_count; ++i) {
        std::string opcode_name;
        ScriptInstruction instruction;
        if (!(input >> token) || token != "ins" ||
            !(input >> opcode_name >> instruction.operand_i >> instruction.operand_f >> instruction.operand_u)) {
            message = "Malformed script instruction record.";
            return false;
        }
        if (!script_opcode_from_name(opcode_name, instruction.opcode)) {
            message = "Unknown script opcode in payload.";
            return false;
        }
        program.instructions.push_back(instruction);
    }
    for (const ScriptFunction& function : program.functions) {
        if (function.entry < 0 || function.entry >= static_cast<int>(program.instructions.size())) {
            message = "Script function entry is out of bounds.";
            return false;
        }
    }
    return true;
}

void write_program(std::ostream& output, const ScriptProgram& program) {
    output << "program " << program.instructions.size() << ' '
           << program.functions.size() << ' '
           << program.comments.size() << '\n';
    for (const ScriptFunction& function : program.functions) {
        output << "function " << std::quoted(function.name) << ' '
               << function.entry << ' ' << function.arity << '\n';
    }
    for (const ScriptComment& comment : program.comments) {
        output << "comment " << comment.line << ' ' << std::quoted(comment.text) << '\n';
    }
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (const ScriptInstruction& instruction : program.instructions) {
        output << "ins " << script_opcode_name(instruction.opcode) << ' '
               << instruction.operand_i << ' '
               << instruction.operand_f << ' '
               << instruction.operand_u << '\n';
    }
}

void write_program_map(
    std::ostream& output,
    const char* count_token,
    const char* record_token,
    const std::unordered_map<std::uint64_t, ScriptProgram>& programs
) {
    std::vector<std::uint64_t> ids;
    ids.reserve(programs.size());
    for (const auto& [id, program] : programs) {
        (void)program;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());

    output << count_token << ' ' << ids.size() << '\n';
    for (const std::uint64_t id : ids) {
        output << record_token << ' ' << id << '\n';
        write_program(output, programs.at(id));
    }
}

bool read_program_map(
    std::istream& input,
    const char* count_token,
    const char* record_token,
    const char* malformed_record_message,
    const char* duplicate_message,
    std::unordered_map<std::uint64_t, ScriptProgram>& programs,
    std::string& message
) {
    std::string token;
    std::size_t count = 0;
    if (!(input >> token) || token != count_token || !(input >> count)) {
        message = std::string("Malformed ") + count_token + " count.";
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        std::uint64_t id = 0;
        if (!(input >> token) || token != record_token || !(input >> id) || id == 0) {
            message = malformed_record_message;
            return false;
        }
        ScriptProgram program;
        if (!read_program(input, program, message)) {
            return false;
        }
        if (!programs.emplace(id, std::move(program)).second) {
            message = duplicate_message;
            return false;
        }
    }
    return true;
}

} // namespace

ScriptCompileResult compile_script(const std::string& source) {
    TokenizeResult tokenized = tokenize(source);
    if (!tokenized.ok) {
        return ScriptCompileResult{false, tokenized.message, {}};
    }

    Compiler compiler;
    compiler.tokens = std::move(tokenized.tokens);
    compiler.program.comments = std::move(tokenized.comments);
    if (!compiler.compile_program()) {
        return ScriptCompileResult{false, compiler.message, {}};
    }
    return ScriptCompileResult{true, "Compiled script.", std::move(compiler.program)};
}

std::string disassemble_script(const ScriptProgram& program) {
    std::ostringstream output;
    for (const ScriptComment& comment : program.comments) {
        output << "# line " << comment.line << ": " << comment.text << '\n';
    }
    for (const ScriptFunction& function : program.functions) {
        output << "function " << function.name << " @ " << function.entry << " arity " << function.arity << '\n';
    }
    for (std::size_t i = 0; i < program.instructions.size(); ++i) {
        const ScriptInstruction& instruction = program.instructions[i];
        output << i << ": " << script_opcode_name(instruction.opcode)
               << ' ' << instruction.operand_i
               << ' ' << instruction.operand_f
               << ' ' << instruction.operand_u << '\n';
    }
    return output.str();
}

bool write_script_store_payload(std::ostream& output, const ScriptStore& scripts) {
    output << "scripts 2\n";
    output << "global " << (scripts.has_global_script ? 1 : 0) << '\n';
    if (scripts.has_global_script) {
        write_program(output, scripts.global_script);
    }
    write_program_map(output, "entity_scripts", "entity_script", scripts.entity_scripts);
    write_program_map(output, "sector_scripts", "sector_script", scripts.sector_scripts);
    return output.good();
}

bool read_script_store_payload(std::istream& input, ScriptStore& scripts, std::string& message) {
    std::string token;
    int version = 0;
    if (!(input >> token) || token != "scripts" || !(input >> version) || version < 1 || version > 2) {
        message = "Unsupported scripts chunk version.";
        return false;
    }
    scripts = {};
    int has_global = 0;
    if (!(input >> token) || token != "global" || !(input >> has_global) || (has_global != 0 && has_global != 1)) {
        message = "Malformed scripts global flag.";
        return false;
    }
    scripts.has_global_script = has_global != 0;
    if (scripts.has_global_script && !read_program(input, scripts.global_script, message)) {
        return false;
    }
    if (!read_program_map(
            input,
            "entity_scripts",
            "entity_script",
            "Malformed entity script record.",
            "Duplicate entity script id.",
            scripts.entity_scripts,
            message
        )) {
        return false;
    }
    if (version >= 2 &&
        !read_program_map(
            input,
            "sector_scripts",
            "sector_script",
            "Malformed sector script record.",
            "Duplicate sector script id.",
            scripts.sector_scripts,
            message
        )) {
        return false;
    }
    std::string trailing;
    if (input >> trailing) {
        message = "Unexpected trailing scripts token: " + trailing;
        return false;
    }
    return true;
}

ScriptRunResult run_script_event(
    ScriptVm& vm,
    EntityRegistry& registry,
    const ScriptProgram& program,
    const std::string& event_name,
    const std::uint64_t self_entity_id,
    const ScriptVmConfig config
) {
    const auto found = std::find_if(program.functions.begin(), program.functions.end(), [&event_name](const ScriptFunction& function) {
        return function.name == event_name;
    });
    if (found == program.functions.end()) {
        return ScriptRunResult{true, "Script event not present.", 0};
    }

    std::vector<ScriptValue> stack;
    std::vector<ScriptValue> locals(64);
    int pc = found->entry;
    int executed = 0;
    std::string message;
    while (pc >= 0 && pc < static_cast<int>(program.instructions.size())) {
        if (++executed > config.instruction_budget) {
            return ScriptRunResult{false, "Script instruction budget exceeded.", executed};
        }
        const ScriptInstruction instruction = program.instructions[static_cast<std::size_t>(pc++)];
        ScriptValue a;
        ScriptValue b;
        switch (instruction.opcode) {
        case ScriptOpcode::PushNumber:
            stack.push_back(number_value(instruction.operand_f));
            break;
        case ScriptOpcode::PushEntity:
            stack.push_back(entity_value(instruction.operand_u));
            break;
        case ScriptOpcode::PushSelf:
            stack.push_back(entity_value(self_entity_id));
            break;
        case ScriptOpcode::LoadLocal:
            if (instruction.operand_i < 0 || instruction.operand_i >= static_cast<int>(locals.size())) {
                return ScriptRunResult{false, "Script local read is out of bounds.", executed};
            }
            stack.push_back(locals[static_cast<std::size_t>(instruction.operand_i)]);
            break;
        case ScriptOpcode::StoreLocal:
            if (instruction.operand_i < 0 || instruction.operand_i >= static_cast<int>(locals.size())) {
                return ScriptRunResult{false, "Script local write is out of bounds.", executed};
            }
            if (!pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            locals[static_cast<std::size_t>(instruction.operand_i)] = a;
            break;
        case ScriptOpcode::Add:
        case ScriptOpcode::Subtract:
        case ScriptOpcode::Multiply:
        case ScriptOpcode::Divide:
        case ScriptOpcode::Less:
        case ScriptOpcode::Greater:
        case ScriptOpcode::Equal:
            if (!pop_value(stack, b, message) || !pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            if (instruction.opcode == ScriptOpcode::Add) {
                stack.push_back(number_value(number_or_zero(a) + number_or_zero(b)));
            } else if (instruction.opcode == ScriptOpcode::Subtract) {
                stack.push_back(number_value(number_or_zero(a) - number_or_zero(b)));
            } else if (instruction.opcode == ScriptOpcode::Multiply) {
                stack.push_back(number_value(number_or_zero(a) * number_or_zero(b)));
            } else if (instruction.opcode == ScriptOpcode::Divide) {
                stack.push_back(number_value(number_or_zero(b) == 0.0F ? 0.0F : number_or_zero(a) / number_or_zero(b)));
            } else if (instruction.opcode == ScriptOpcode::Less) {
                stack.push_back(number_value(number_or_zero(a) < number_or_zero(b) ? 1.0F : 0.0F));
            } else if (instruction.opcode == ScriptOpcode::Greater) {
                stack.push_back(number_value(number_or_zero(a) > number_or_zero(b) ? 1.0F : 0.0F));
            } else {
                stack.push_back(number_value(number_or_zero(a) == number_or_zero(b) ? 1.0F : 0.0F));
            }
            break;
        case ScriptOpcode::Jump:
            pc = instruction.operand_i;
            break;
        case ScriptOpcode::JumpIfFalse:
            if (!pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            if (!truthy(a)) {
                pc = instruction.operand_i;
            }
            break;
        case ScriptOpcode::GetTransformX:
        case ScriptOpcode::GetTransformY:
        case ScriptOpcode::GetTransformZ:
        case ScriptOpcode::GetTransformYaw:
            if (!pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            if (const TransformComponent* transform = transform_by_id(registry, entity_or_zero(a))) {
                if (instruction.opcode == ScriptOpcode::GetTransformX) {
                    stack.push_back(number_value(transform->position.x));
                } else if (instruction.opcode == ScriptOpcode::GetTransformY) {
                    stack.push_back(number_value(transform->position.y));
                } else if (instruction.opcode == ScriptOpcode::GetTransformZ) {
                    stack.push_back(number_value(transform->position.z));
                } else {
                    stack.push_back(number_value(transform->yaw));
                }
            } else {
                return ScriptRunResult{false, "Entity has no transform component.", executed};
            }
            break;
        case ScriptOpcode::SetTransformX:
        case ScriptOpcode::SetTransformY:
        case ScriptOpcode::SetTransformZ:
        case ScriptOpcode::SetTransformYaw:
            if (!pop_value(stack, b, message) || !pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            if (TransformComponent* transform = transform_by_id(registry, entity_or_zero(a))) {
                if (instruction.opcode == ScriptOpcode::SetTransformX) {
                    transform->position.x = number_or_zero(b);
                } else if (instruction.opcode == ScriptOpcode::SetTransformY) {
                    transform->position.y = number_or_zero(b);
                } else if (instruction.opcode == ScriptOpcode::SetTransformZ) {
                    transform->position.z = number_or_zero(b);
                } else {
                    transform->yaw = number_or_zero(b);
                }
            } else {
                return ScriptRunResult{false, "Entity has no transform component.", executed};
            }
            break;
        case ScriptOpcode::GetPointLightRadius:
        case ScriptOpcode::GetPointLightIntensity:
        case ScriptOpcode::GetPointLightShadowBias:
            if (!pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            if (const PointLightComponent* light = point_light_by_id(registry, entity_or_zero(a))) {
                if (instruction.opcode == ScriptOpcode::GetPointLightRadius) {
                    stack.push_back(number_value(light->radius));
                } else if (instruction.opcode == ScriptOpcode::GetPointLightIntensity) {
                    stack.push_back(number_value(light->intensity));
                } else {
                    stack.push_back(number_value(light->shadow_bias));
                }
            } else {
                return ScriptRunResult{false, "Entity has no point_light component.", executed};
            }
            break;
        case ScriptOpcode::SetPointLightRadius:
        case ScriptOpcode::SetPointLightIntensity:
        case ScriptOpcode::SetPointLightShadowBias:
            if (!pop_value(stack, b, message) || !pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            if (PointLightComponent* light = point_light_by_id(registry, entity_or_zero(a))) {
                const float value = std::max(number_or_zero(b), 0.0F);
                if (instruction.opcode == ScriptOpcode::SetPointLightRadius) {
                    light->radius = value;
                } else if (instruction.opcode == ScriptOpcode::SetPointLightIntensity) {
                    light->intensity = value;
                } else {
                    light->shadow_bias = value;
                }
            } else {
                return ScriptRunResult{false, "Entity has no point_light component.", executed};
            }
            break;
        case ScriptOpcode::ClassType:
            if (!pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            stack.push_back(number_value(static_cast<float>(entity_class_type(registry, entity_or_zero(a)))));
            break;
        case ScriptOpcode::ClassAncestry:
            if (!pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            stack.push_back(number_value(static_cast<float>(entity_class_ancestry(registry, entity_or_zero(a)))));
            break;
        case ScriptOpcode::ComponentMask:
            if (!pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            stack.push_back(number_value(static_cast<float>(entity_component_mask(registry, entity_or_zero(a)))));
            break;
        case ScriptOpcode::IsA:
            if (!pop_value(stack, b, message) || !pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            stack.push_back(number_value(entity_is_a(
                registry,
                entity_or_zero(a),
                static_cast<std::uint32_t>(std::max(number_or_zero(b), 0.0F))
            ) ? 1.0F : 0.0F));
            break;
        case ScriptOpcode::HasComponent:
            if (!pop_value(stack, b, message) || !pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            stack.push_back(number_value(entity_has_component(
                registry,
                entity_or_zero(a),
                static_cast<std::uint32_t>(std::max(number_or_zero(b), 0.0F))
            ) ? 1.0F : 0.0F));
            break;
        case ScriptOpcode::Print:
            if (!pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            vm.log.push_back(a.kind == ScriptValue::Kind::Entity ? ("entity:" + std::to_string(a.entity_id)) : std::to_string(a.number));
            break;
        case ScriptOpcode::Pop:
            if (!pop_value(stack, a, message)) {
                return ScriptRunResult{false, message, executed};
            }
            break;
        case ScriptOpcode::Return:
            return ScriptRunResult{true, "Script event completed.", executed};
        }
    }
    return ScriptRunResult{false, "Script program counter left bytecode range.", executed};
}

std::vector<ScriptRunResult> run_script_store_event(
    ScriptVm& vm,
    EntityRegistry& registry,
    const ScriptStore& scripts,
    const std::string& event_name,
    const ScriptVmConfig config
) {
    std::vector<ScriptRunResult> results;
    if (scripts.has_global_script) {
        results.push_back(run_script_event(vm, registry, scripts.global_script, event_name, 0, config));
    }

    std::vector<std::uint64_t> ids;
    ids.reserve(scripts.entity_scripts.size());
    for (const auto& [id, program] : scripts.entity_scripts) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    for (const std::uint64_t id : ids) {
        const EntityHandle entity = find_entity_by_stable_id(registry, id);
        const ScriptComponent* component = script_component(registry, entity);
        if (component == nullptr || !component->enabled || component->script_id != id) {
            continue;
        }
        results.push_back(run_script_event(vm, registry, scripts.entity_scripts.at(id), event_name, id, config));
    }
    return results;
}

ScriptRunResult run_sector_script_event(
    ScriptVm& vm,
    EntityRegistry& registry,
    const ScriptStore& scripts,
    const std::uint64_t sector_id,
    const std::string& event_name,
    const ScriptVmConfig config
) {
    if (sector_id == 0) {
        return ScriptRunResult{true, "No sector script target.", 0};
    }
    const auto found = scripts.sector_scripts.find(sector_id);
    if (found == scripts.sector_scripts.end()) {
        return ScriptRunResult{true, "Sector script not present.", 0};
    }
    return run_script_event(vm, registry, found->second, event_name, 0, config);
}

void attach_entity_script(ScriptStore& scripts, EntityRegistry& registry, const std::uint64_t entity_id, ScriptProgram program) {
    const EntityHandle entity = find_entity_by_stable_id(registry, entity_id);
    if (entity_alive(registry, entity)) {
        add_script(registry, entity, ScriptComponent{entity_id, true});
    }
    scripts.entity_scripts[entity_id] = std::move(program);
}

void detach_entity_script(ScriptStore& scripts, EntityRegistry& registry, const std::uint64_t entity_id) {
    scripts.entity_scripts.erase(entity_id);
    if (ScriptComponent* component = script_component(registry, find_entity_by_stable_id(registry, entity_id))) {
        component->enabled = false;
        component->script_id = 0;
    }
}

void set_global_script(ScriptStore& scripts, ScriptProgram program) {
    scripts.global_script = std::move(program);
    scripts.has_global_script = true;
}

void clear_global_script(ScriptStore& scripts) {
    scripts.global_script = {};
    scripts.has_global_script = false;
}

const ScriptProgram* script_for_target(const ScriptStore& scripts, const ScriptTargetRef target) {
    switch (target.kind) {
    case ScriptTargetKind::Map:
        return scripts.has_global_script ? &scripts.global_script : nullptr;
    case ScriptTargetKind::Entity: {
        const auto found = scripts.entity_scripts.find(target.id);
        return found == scripts.entity_scripts.end() ? nullptr : &found->second;
    }
    case ScriptTargetKind::Sector: {
        const auto found = scripts.sector_scripts.find(target.id);
        return found == scripts.sector_scripts.end() ? nullptr : &found->second;
    }
    }
    return nullptr;
}

void set_script_for_target(ScriptStore& scripts, const ScriptTargetRef target, ScriptProgram program) {
    switch (target.kind) {
    case ScriptTargetKind::Map:
        set_global_script(scripts, std::move(program));
        break;
    case ScriptTargetKind::Entity:
        if (target.id != 0) {
            scripts.entity_scripts[target.id] = std::move(program);
        }
        break;
    case ScriptTargetKind::Sector:
        if (target.id != 0) {
            set_sector_script(scripts, target.id, std::move(program));
        }
        break;
    }
}

bool clear_script_for_target(ScriptStore& scripts, const ScriptTargetRef target) {
    switch (target.kind) {
    case ScriptTargetKind::Map:
        if (!scripts.has_global_script) {
            return false;
        }
        clear_global_script(scripts);
        return true;
    case ScriptTargetKind::Entity:
        return scripts.entity_scripts.erase(target.id) > 0;
    case ScriptTargetKind::Sector:
        return clear_sector_script(scripts, target.id);
    }
    return false;
}

void set_sector_script(ScriptStore& scripts, const std::uint64_t sector_id, ScriptProgram program) {
    if (sector_id == 0) {
        return;
    }
    scripts.sector_scripts[sector_id] = std::move(program);
}

bool clear_sector_script(ScriptStore& scripts, const std::uint64_t sector_id) {
    return scripts.sector_scripts.erase(sector_id) > 0;
}

const char* script_opcode_name(const ScriptOpcode opcode) {
    switch (opcode) {
    case ScriptOpcode::PushNumber: return "PushNumber";
    case ScriptOpcode::PushEntity: return "PushEntity";
    case ScriptOpcode::PushSelf: return "PushSelf";
    case ScriptOpcode::LoadLocal: return "LoadLocal";
    case ScriptOpcode::StoreLocal: return "StoreLocal";
    case ScriptOpcode::Add: return "Add";
    case ScriptOpcode::Subtract: return "Subtract";
    case ScriptOpcode::Multiply: return "Multiply";
    case ScriptOpcode::Divide: return "Divide";
    case ScriptOpcode::Less: return "Less";
    case ScriptOpcode::Greater: return "Greater";
    case ScriptOpcode::Equal: return "Equal";
    case ScriptOpcode::Jump: return "Jump";
    case ScriptOpcode::JumpIfFalse: return "JumpIfFalse";
    case ScriptOpcode::GetTransformX: return "GetTransformX";
    case ScriptOpcode::GetTransformY: return "GetTransformY";
    case ScriptOpcode::GetTransformZ: return "GetTransformZ";
    case ScriptOpcode::GetTransformYaw: return "GetTransformYaw";
    case ScriptOpcode::SetTransformX: return "SetTransformX";
    case ScriptOpcode::SetTransformY: return "SetTransformY";
    case ScriptOpcode::SetTransformZ: return "SetTransformZ";
    case ScriptOpcode::SetTransformYaw: return "SetTransformYaw";
    case ScriptOpcode::GetPointLightRadius: return "GetPointLightRadius";
    case ScriptOpcode::GetPointLightIntensity: return "GetPointLightIntensity";
    case ScriptOpcode::GetPointLightShadowBias: return "GetPointLightShadowBias";
    case ScriptOpcode::SetPointLightRadius: return "SetPointLightRadius";
    case ScriptOpcode::SetPointLightIntensity: return "SetPointLightIntensity";
    case ScriptOpcode::SetPointLightShadowBias: return "SetPointLightShadowBias";
    case ScriptOpcode::ClassType: return "ClassType";
    case ScriptOpcode::ClassAncestry: return "ClassAncestry";
    case ScriptOpcode::ComponentMask: return "ComponentMask";
    case ScriptOpcode::IsA: return "IsA";
    case ScriptOpcode::HasComponent: return "HasComponent";
    case ScriptOpcode::Print: return "Print";
    case ScriptOpcode::Pop: return "Pop";
    case ScriptOpcode::Return: return "Return";
    }
    return "Return";
}

bool script_opcode_from_name(const std::string& name, ScriptOpcode& opcode) {
    for (int i = static_cast<int>(ScriptOpcode::PushNumber); i <= static_cast<int>(ScriptOpcode::Return); ++i) {
        const auto candidate = static_cast<ScriptOpcode>(i);
        if (name == script_opcode_name(candidate)) {
            opcode = candidate;
            return true;
        }
    }
    return false;
}

} // namespace undecedent
