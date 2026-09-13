#ifndef NPS_PARSER_H
#define NPS_PARSER_H

#include <string>

#include "nps/core/ast.h"

namespace nps {

struct ParseResult {
    NodeId root = kNoNode;
    Status status = Status::Ok;
    // Byte offset the failure was found at, so a caller can point at it. A parser that only says
    // "syntax error" makes the user hunt, which is the whole reason this is a hand written descent
    // rather than a pattern that either matches or does not.
    size_t offset = 0;
    std::string message;

    bool ok() const { return status == Status::Ok && root != kNoNode; }
};

// Requires one name token spanning the input, including no surrounding whitespace.
bool is_identifier(const std::string &input,
                   size_t max_input_bytes = Limits{}.max_input_bytes);
std::string normalize_identifier(const std::string &input);
ParseResult parse(Arena &arena, const std::string &input);

}  // namespace nps

#endif
