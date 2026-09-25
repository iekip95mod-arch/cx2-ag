#ifndef NPS_COMMAND_H
#define NPS_COMMAND_H

#include "nps/core/parser.h"

namespace nps {

enum class CommandKind { Unhandled, Solve, Differentiate, Integrate, DefiniteIntegral, Limit, Tangent, Linearize, Simplify, Expand, Factor, Rearrange, Integer, Ref, Rref, Determinant, Desolve };
enum class CommandStatus { Unhandled, Ready, Invalid, Unsupported, ResourceExceeded };

struct Command {
    CommandKind kind = CommandKind::Unhandled;
    CommandStatus status = CommandStatus::Unhandled;
    NodeId expression = kNoNode;
    NodeId variable = kNoNode;
    NodeId lower = kNoNode;
    NodeId upper = kNoNode;
    NodeId point = kNoNode;
    int direction = 0;
    // Desolve reads y as the dependent variable and an optional initial point y(x0) = y0.
    NodeId dependent = kNoNode;
    NodeId initial_point = kNoNode;
    NodeId initial_value = kNoNode;
    std::string operand_text;
    std::string variable_name;
    std::string detail;
};

const char *command_kind_name(CommandKind kind);
Command parse_command(Arena &arena, const std::string &text, const std::string &default_variable);

}

#endif
