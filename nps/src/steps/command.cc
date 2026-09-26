#include "nps/steps/command.h"
#include "nps/steps/integer.h"
#include "nps/core/canonical.h"

namespace nps {
namespace {

bool space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool identifier_character(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_';
}

// Reads each primed name as diff(name), since the grammar has no prime, and refuses any other.
bool unprimed(const std::string &text, std::string *out) {
    out->clear();
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\'') {
            out->push_back(text[i]);
            continue;
        }
        size_t start = out->size();
        while (start > 0 && identifier_character((*out)[start - 1])) --start;
        if (start == out->size() || ((*out)[start] >= '0' && (*out)[start] <= '9')) return false;
        const std::string name = out->substr(start);
        out->resize(start);
        *out += "diff(" + name + ")";
    }
    return true;
}

bool calls_diff(const Arena &arena, NodeId node) {
    if (arena.at(node).kind == Kind::Call && arena.text(node) == "diff") return true;
    for (NodeId child : arena.children(node))
        if (calls_diff(arena, child)) return true;
    return false;
}

// diff(y) or diff(y, x) and nothing else, so a second derivative or another variable stays Giac's.
bool first_derivative(const Arena &arena, NodeId node, NodeId dependent, NodeId independent) {
    if (arena.at(node).kind != Kind::Call || arena.text(node) != "diff") return false;
    const ChildView arguments = arena.children(node);
    return (arguments.size() == 1 || (arguments.size() == 2 && arguments[1] == independent)) &&
           arguments[0] == dependent;
}

// A shape this family reads becomes Ready and anything else is left unhandled for Giac.
Command desolve_command(Arena &arena, NodeId root, const std::string &text, size_t operand_start) {
    const ChildView arguments = arena.children(root);
    if (arguments.size() != 3) return Command();
    const NodeId independent = arguments[1];
    const NodeId dependent = arguments[2];
    if (arena.at(independent).kind != Kind::Symbol || arena.at(dependent).kind != Kind::Symbol ||
        independent == dependent)
        return Command();
    NodeId equation = arguments[0];
    NodeId condition = kNoNode;
    if (arena.at(equation).kind == Kind::List) {
        const ChildView items = arena.children(equation);
        if (items.size() != 2) return Command();
        equation = items[0];
        condition = items[1];
    }
    if (arena.at(equation).kind != Kind::Equals) return Command();
    const ChildView sides = arena.children(equation);
    const bool left = first_derivative(arena, sides[0], dependent, independent);
    const bool right = first_derivative(arena, sides[1], dependent, independent);
    if (left == right) return Command();
    const NodeId slope = left ? sides[1] : sides[0];
    if (calls_diff(arena, slope)) return Command();
    Command command;
    if (condition != kNoNode) {
        if (arena.at(condition).kind != Kind::Equals) return Command();
        const ChildView point = arena.children(condition);
        if (arena.at(point[0]).kind != Kind::Call || arena.text(point[0]) != arena.text(dependent) ||
            arena.children(point[0]).size() != 1 || calls_diff(arena, point[1]))
            return Command();
        command.initial_point = arena.children(point[0])[0];
        command.initial_value = point[1];
    }
    command.kind = CommandKind::Desolve;
    command.status = CommandStatus::Ready;
    command.expression = slope;
    command.variable = independent;
    command.variable_name = arena.text(independent);
    command.dependent = dependent;
    size_t end = operand_start;
    for (size_t depth = 1; end < text.size(); ++end) {
        if (text[end] == '(' || text[end] == '[') ++depth;
        else if (text[end] == ')' || text[end] == ']') --depth;
        if (depth == 0 || (depth == 1 && text[end] == ',')) break;
    }
    command.operand_text = text.substr(operand_start, end - operand_start);
    return command;
}

CommandKind named_command(const std::string &name) {
    if (name == "solve") return CommandKind::Solve;
    if (name == "diff" || name == "d") return CommandKind::Differentiate;
    if (name == "int" || name == "integrate") return CommandKind::Integrate;
    if (name == "limit" || name == "lim") return CommandKind::Limit;
    if (name == "tangent") return CommandKind::Tangent;
    if (name == "linearize") return CommandKind::Linearize;
    if (name == "paramslope") return CommandKind::ParamSlope;
    if (name == "implicit") return CommandKind::Implicit;
    if (name == "simplify") return CommandKind::Simplify;
    if (name == "expand") return CommandKind::Expand;
    if (name == "factor") return CommandKind::Factor;
    if (name == "rearrange") return CommandKind::Rearrange;
    if (name == "ref") return CommandKind::Ref;
    if (name == "rref") return CommandKind::Rref;
    if (name == "det") return CommandKind::Determinant;
    if (name == "desolve") return CommandKind::Desolve;
    if (integer_command_arity(name)) return CommandKind::Integer;
    return CommandKind::Unhandled;
}

}

const char *command_kind_name(CommandKind kind) {
    switch (kind) {
        case CommandKind::Solve: return "solve";
        case CommandKind::Differentiate: return "differentiate";
        case CommandKind::Integrate: return "integrate";
        case CommandKind::DefiniteIntegral: return "definite integral";
        case CommandKind::Limit: return "limit";
        case CommandKind::Tangent: return "tangent";
        case CommandKind::Linearize: return "linearize";
        case CommandKind::ParamSlope: return "paramslope";
        case CommandKind::Implicit: return "implicit";
        case CommandKind::Simplify: return "simplify";
        case CommandKind::Expand: return "expand";
        case CommandKind::Factor: return "factor";
        case CommandKind::Rearrange: return "rearrange";
        case CommandKind::Integer: return "integer";
        case CommandKind::Ref: return "ref";
        case CommandKind::Rref: return "rref";
        case CommandKind::Determinant: return "determinant";
        case CommandKind::Desolve: return "differential equation";
        case CommandKind::Unhandled: return "command";
    }
    return "command";
}

Command parse_command(Arena &arena, const std::string &text, const std::string &default_variable) {
    Command command;
    if (text.find('\0') != std::string::npos) {
        command.status = CommandStatus::Invalid;
        command.detail = "embedded NUL is not allowed";
        return command;
    }
    size_t start = 0;
    while (start < text.size() && (space(text[start]) || text[start] == '(')) ++start;
    size_t end = start;
    std::string source;
    std::string name;
    if (text.compare(start, 3, "\xE2\x88\xAB") == 0 || text.compare(start, 3, "\xEF\x80\x88") == 0) {
        name = text.compare(start, 3, "\xE2\x88\xAB") == 0 ? "int" : "diff";
        end = start + 3;
        if (text.size() <= arena.limits().max_input_bytes) {
            source = text;
            source.replace(start, 3, name);
        }
    } else {
        while (end < text.size() && ((text[end] >= 'a' && text[end] <= 'z') || text[end] == '_')) ++end;
        name = text.substr(start, end - start);
    }
    command.kind = named_command(name);
    while (end < text.size() && space(text[end])) ++end;
    if (command.kind == CommandKind::Unhandled || end == text.size() || text[end] != '(')
        return Command();

    if (command.kind == CommandKind::Desolve && !unprimed(text, &source)) return Command();
    command.status = CommandStatus::Invalid;
    const ParseResult parsed = parse(arena, source.empty() ? text : source);
    if (!parsed.ok() && command.kind == CommandKind::Desolve && !resource_status(parsed.status))
        return Command();
    if (!parsed.ok()) {
        if (resource_status(parsed.status))
            command.status = CommandStatus::ResourceExceeded;
        command.detail = parsed.message.empty() ? status_name(parsed.status) : parsed.message;
        return command;
    }
    if (arena.at(parsed.root).kind != Kind::Call || arena.text(parsed.root) != name)
        return Command();

    if (command.kind == CommandKind::Desolve)
        return desolve_command(arena, parsed.root, source, end + 1);

    if (command.kind == CommandKind::Integer) {
        const size_t arity = *integer_command_arity(name);
        if (arena.children(parsed.root).size() != arity) {
            command.status = CommandStatus::Unsupported;
            command.detail = "the integer command has an unsupported number of arguments";
            return command;
        }
        command.expression = parsed.root;
        command.operand_text = text;
        command.status = CommandStatus::Ready;
        return command;
    }

    const size_t operand_start = end + 1;
    size_t depth = 1;
    for (++end; end < text.size(); ++end) {
        if (text[end] == '(' || text[end] == '[') ++depth;
        else if (text[end] == ')' || text[end] == ']') --depth;
        if (depth == 0 || (depth == 1 && text[end] == ',')) break;
    }
    command.operand_text = text.substr(operand_start, end - operand_start);

    const ChildView arguments = arena.children(parsed.root);
    if (command.kind == CommandKind::Ref || command.kind == CommandKind::Rref ||
        command.kind == CommandKind::Determinant) {
        if (arguments.size() != 1) {
            command.status = CommandStatus::Unsupported;
            command.detail = "matrix walkthroughs require exactly one matrix argument";
            return command;
        }
        command.expression = arguments[0];
        command.status = CommandStatus::Ready;
        return command;
    }
    if (command.kind == CommandKind::ParamSlope) {
        if (arguments.size() != 4) {
            command.status = CommandStatus::Unsupported;
            command.detail = "parametric slopes require x(t), y(t), the parameter and its value";
            return command;
        }
        if (arena.at(arguments[2]).kind != Kind::Symbol) {
            command.detail = "the parameter must be a single identifier";
            return command;
        }
        command.expression = arguments[0];
        command.companion = arguments[1];
        command.variable = arguments[2];
        command.variable_name = arena.text(command.variable);
        command.point = arguments[3];
        command.status = CommandStatus::Ready;
        return command;
    }
    if (command.kind == CommandKind::Implicit) {
        if (arguments.size() != 3) {
            command.status = CommandStatus::Unsupported;
            command.detail = "implicit differentiation requires an equation, the independent variable and the dependent one";
            return command;
        }
        if (arena.at(arguments[1]).kind != Kind::Symbol || arena.at(arguments[2]).kind != Kind::Symbol) {
            command.detail = "the variables must be single identifiers";
            return command;
        }
        command.expression = arguments[0];
        command.variable = arguments[1];
        command.variable_name = arena.text(command.variable);
        command.dependent = arguments[2];
        command.status = CommandStatus::Ready;
        return command;
    }
    const bool rewrite = command.kind == CommandKind::Simplify || command.kind == CommandKind::Expand ||
                         command.kind == CommandKind::Factor;
    const bool limit = command.kind == CommandKind::Limit;
    const bool tangent = command.kind == CommandKind::Tangent || command.kind == CommandKind::Linearize;
    const size_t minimum = limit || tangent ? 3 : command.kind == CommandKind::Rearrange ? 2 : 1;
    const size_t maximum = tangent ? 3 : limit || command.kind == CommandKind::Integrate ? 4
                         : command.kind == CommandKind::Differentiate ? 3 : rewrite ? 1 : 2;
    if (arguments.size() < minimum || arguments.size() > maximum) {
        command.status = CommandStatus::Unsupported;
        command.detail = tangent
                             ? "tangent lines and linearizations require an expression, a variable and the point"
                         : command.kind == CommandKind::Integrate
                             ? "integrals require an expression, a variable and optional lower and upper bounds"
                         : command.kind == CommandKind::Differentiate
                             ? "walkthroughs support only the first derivative with one variable"
                             : "the command has an unsupported number of arguments";
        return command;
    }
    if (command.kind == CommandKind::Integrate && arguments.size() == 3) {
        command.detail = "a definite integral needs both its lower and upper bounds";
        return command;
    }
    if (command.kind == CommandKind::Differentiate && arguments.size() == 3) {
        int64_t order = 0;
        if (!folded_integer(arena, arguments[2], &order) || order != 1) {
            command.status = arena.failed() ? CommandStatus::ResourceExceeded : CommandStatus::Unsupported;
            command.detail = arena.failed() ? "the command exceeded the expression limits"
                                            : "native derivative walkthroughs require order one";
            return command;
        }
    }
    if (command.kind == CommandKind::Integrate && arguments.size() == 4) {
        command.kind = CommandKind::DefiniteIntegral;
        command.lower = arguments[2];
        command.upper = arguments[3];
    }
    if (tangent) command.point = arguments[2];
    if (limit) {
        command.point = arguments[2];
        if (arguments.size() == 4) {
            int64_t direction = 0;
            if (!folded_integer(arena, arguments[3], &direction) || direction < -1 || direction > 1) {
                command.status = arena.failed() ? CommandStatus::ResourceExceeded : CommandStatus::Invalid;
                command.detail = arena.failed() ? "the command exceeded the expression limits"
                                                : "limit direction must be -1 (left), 0 (both) or 1 (right)";
                return command;
            }
            command.direction = static_cast<int>(direction);
        }
    }
    command.expression = arguments[0];
    if (!rewrite && arguments.size() >= 2) {
        command.variable = arguments[1];
        if (arena.at(command.variable).kind != Kind::Symbol) {
            command.detail = "the variable must be a single identifier";
            return command;
        }
        command.variable_name = arena.text(command.variable);
    } else {
        if (!is_identifier(default_variable, arena.limits().max_input_bytes)) {
            command.detail = "the default variable must be a single identifier without whitespace";
            return command;
        }
        command.variable_name = normalize_identifier(default_variable);
        command.variable = arena.symbol(command.variable_name);
    }
    if (arena.failed() || command.variable == kNoNode) {
        command.status = CommandStatus::ResourceExceeded;
        command.detail = "the command exceeded the expression limits";
        return command;
    }

    command.status = CommandStatus::Ready;
    return command;
}

}
