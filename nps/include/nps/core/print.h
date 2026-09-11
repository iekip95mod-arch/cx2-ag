#ifndef NPS_PRINT_H
#define NPS_PRINT_H

#include <string>

#include "nps/core/ast.h"

namespace nps {

// The form a round trip is checked against: fully parenthesised where precedence would otherwise
// decide, so print then parse then print is a fixed point and a diff points at a real difference.
std::string print(const Arena &arena, NodeId id);

// Display-only division notation. Leaves the stored tree unchanged.
std::string print_math(const Arena &arena, NodeId id);

// What the Giac adapter sends. Separate from print because Giac's spelling is its own concern and
// leaking it into the canonical form would put backend syntax inside derivation records.
std::string print_giac(const Arena &arena, NodeId id);

}  // namespace nps

#endif
