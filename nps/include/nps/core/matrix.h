#ifndef NPS_CORE_MATRIX_H
#define NPS_CORE_MATRIX_H

#include <optional>
#include <span>
#include <variant>

#include "nps/core/ast.h"
#include "nps/core/evaluate.h"

namespace nps {

// Row-operation indices are zero-based.
struct MatrixRowSwap {
    size_t first = 0;
    size_t second = 0;
};

struct MatrixRowScale {
    size_t row = 0;
    Rational factor;
};

struct MatrixRowAddMultiple {
    size_t target = 0;
    size_t source = 0;
    Rational factor;
};

using MatrixRowOperation = std::variant<MatrixRowSwap, MatrixRowScale, MatrixRowAddMultiple>;

// The arena must outlive this view and stay at the same address.
class MatrixView {
  public:
    // This checks shape only. Numeric admission belongs to the solver.
    static std::optional<MatrixView> from(const Arena &arena, NodeId matrix) {
        if (arena.failed() || arena.at(matrix).kind != Kind::List)
            return std::nullopt;
        const ChildView rows = arena.children(matrix);
        if (rows.empty() || arena.at(rows[0]).kind != Kind::List)
            return std::nullopt;
        const size_t columns = arena.children(rows[0]).size();
        if (columns == 0)
            return std::nullopt;
        for (NodeId row : rows) {
            if (arena.at(row).kind != Kind::List || arena.children(row).size() != columns)
                return std::nullopt;
            for (NodeId cell : arena.children(row)) {
                if (arena.at(cell).kind == Kind::Invalid || contains_list(arena, cell))
                    return std::nullopt;
            }
        }
        return MatrixView(arena, matrix, rows.size(), columns);
    }

    NodeId root() const { return matrix_; }
    size_t rows() const { return rows_; }
    size_t columns() const { return columns_; }
    NodeId cell(size_t row, size_t column) const {
        if (row >= rows_ || column >= columns_)
            return kNoNode;
        return arena_->children(arena_->children(matrix_)[row])[column];
    }

  private:
    MatrixView(const Arena &arena, NodeId matrix, size_t rows, size_t columns)
        : arena_(&arena), matrix_(matrix), rows_(rows), columns_(columns) {}

    const Arena *arena_;
    NodeId matrix_;
    size_t rows_;
    size_t columns_;
};

namespace detail {
inline bool matrix_exact_arithmetic(const Arena &arena, NodeId id) {
    if (id >= arena.node_count())
        return false;
    return !arena.any_node(id, [&arena](NodeId current) {
        if (arena.is_approximate(current))
            return true;
        const Node &node = arena.at(current);
        switch (node.kind) {
            case Kind::Integer: return !node.small_valid;
            case Kind::Neg: return node.child_count != 1;
            case Kind::Pow: return node.child_count != 2;
            case Kind::Add:
            case Kind::Mul: return node.child_count == 0;
            default: return true;
        }
    });
}
}

// Decimal syntax and approximate provenance never enter exact checked evaluation.
inline bool read_matrix_rational(const Arena &arena, NodeId cell, Rational *out) {
    return !arena.failed() && detail::matrix_exact_arithmetic(arena, cell) &&
           evaluate_rational(arena, cell, {}, out);
}

inline bool read_exact_matrix(const Arena &arena, NodeId matrix, std::span<Rational> values = {}) {
    const auto view = MatrixView::from(arena, matrix);
    if (!view || arena.is_approximate(matrix) ||
        (!values.empty() && view->rows() > values.size() / view->columns()))
        return false;
    for (NodeId row : arena.children(matrix)) {
        if (arena.is_approximate(row))
            return false;
    }
    for (size_t row = 0; row < view->rows(); ++row) {
        for (size_t column = 0; column < view->columns(); ++column) {
            Rational value;
            if (!read_matrix_rational(arena, view->cell(row, column), &value))
                return false;
            if (!values.empty())
                values[row * view->columns() + column] = value;
        }
    }
    return true;
}

}  // namespace nps

#endif
