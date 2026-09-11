#ifndef NPS_AST_H
#define NPS_AST_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "nps/core/budgets.h"

namespace nps {

enum class Kind : uint8_t {
    Integer,
    Decimal,
    Symbol,
    Add,
    Mul,
    Pow,
    Neg,
    Call,
    // Four relations rather than one, because they make different claims and only one of them is
    // an equation to solve. Assign binds a name, Equals states a constraint, Approx says the two
    // sides are near each other, and Identity says they agree for every value. Collapsing them
    // would let an approximation be read as exact, which is the thing MATH-002 exists to stop.
    Equals,
    Assign,
    Approx,
    Identity,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    // Invalid retains its original value when new kinds are appended.
    Invalid,
    List,
};

using NodeId = uint32_t;
const NodeId kNoNode = 0xFFFFFFFFu;

struct Node {
    Kind kind;
    // Integer and Decimal keep their text rather than a machine number. The device has GMP behind
    // Giac and the core has no bignum of its own, so narrowing here would lose digits the backend
    // can handle. Decimal also has to round trip exactly as written for PERF-007.
    uint32_t text;
    int64_t small;
    bool small_valid;
    // Cached to keep scalar preflight bounded on shared subtrees.
    bool has_list = false;
    // Children live in the arena's one child pool, addressed rather than owned. A node carries an
    // index and a count, so building one costs no allocation of its own and a node is trivially
    // copyable. Read them through Arena::children.
    // Initialised here rather than by each constructor, because only add_node knows the pool and it
    // fills them on the one path that stores the node.
    uint32_t child_offset = 0;
    uint32_t child_count = 0;
    uint32_t depth;
    uint32_t size;
};

enum class Status : uint8_t {
    Ok,
    DepthExceeded,
    SizeExceeded,
    InputTooLong,
    SyntaxError,
};

const char *status_name(Status s);

// Whether the status is StepCAS running out of room rather than a property of the input.
bool resource_status(Status s);

class Arena;

enum class Sign : uint8_t { Negative, Zero, Positive, Unknown };

// Read numeral signs without expanding exponents, including unary negation.
Sign literal_sign(const Arena &arena, NodeId id);

// An integer the parser may have spelled as Neg(Integer), which is how every negative literal and
// every division exponent arrives. Refuses INT64_MIN, so a caller is free to negate what it gets.
bool small_integer(const Arena &arena, NodeId id, int64_t *out);

// The decimal text of a computed value, sign included, in the spelling Arena::integer expects.
std::string integer_text(int64_t v);

// Whether the variable occurs anywhere in the expression. Every rule engine sorts its constants
// from its varying parts with this, so there is one answer to what counts as constant.
bool depends_on(const Arena &arena, NodeId id, NodeId variable);
// Includes lists nested in calls, arithmetic and relations.
bool contains_list(const Arena &arena, NodeId id);

// Whether the expression contains a form that has no value, which today means zero raised to a
// negative power, the shape a division by zero parses into. Asked once at the entry of every engine
// rather than in the branches that happen to build one, so a rule added later needs no new guard.
// Structural only: x/(1-1) hides its zero until the arithmetic folds, so callers test the canonical
// form through divides_by_zero in canonical.h rather than calling this directly.
bool has_undefined_form(const Arena &arena, NodeId id);

// A node's children, read straight out of the pool. Every access goes through the pool object,
// which does not move, rather than through its buffer, which does when the pool grows. So a view
// taken before more nodes are built is still correct after, and the copy-the-children-first dance
// the old vector needed is gone along with the allocation it cost.
class ChildView {
  public:
    class iterator {
      public:
        iterator(const std::vector<NodeId> *pool, size_t index) : pool_(pool), index_(index) {}
        NodeId operator*() const { return (*pool_)[index_]; }
        iterator &operator++() {
            ++index_;
            return *this;
        }
        bool operator==(const iterator &o) const { return index_ == o.index_; }
        bool operator!=(const iterator &o) const { return index_ != o.index_; }

      private:
        const std::vector<NodeId> *pool_;
        size_t index_;
    };

    ChildView() : pool_(0), offset_(0), count_(0) {}
    ChildView(const std::vector<NodeId> *pool, uint32_t offset, uint32_t count)
        : pool_(pool), offset_(offset), count_(count) {}

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    NodeId operator[](size_t i) const { return (*pool_)[offset_ + i]; }
    iterator begin() const { return iterator(pool_, offset_); }
    iterator end() const { return iterator(pool_, offset_ + count_); }

  private:
    const std::vector<NodeId> *pool_;
    uint32_t offset_;
    uint32_t count_;
};

class NodeStore {
  public:
    const Node &operator[](size_t i) const { return chunks_[i / kChunk][i % kChunk]; }
    size_t size() const { return size_; }
    void push_back(const Node &n) {
        if (size_ % kChunk == 0)
            chunks_.push_back(std::unique_ptr<Node[]>(new Node[kChunk]));
        chunks_[size_ / kChunk][size_ % kChunk] = n;
        ++size_;
    }

  private:
    static const size_t kChunk = 256;
    std::vector<std::unique_ptr<Node[]> > chunks_;
    size_t size_ = 0;
};

class Arena {
  public:
    explicit Arena(const Limits &limits = Limits()) : limits_(limits) {}

    NodeId integer(const std::string &digits);
    NodeId decimal(const std::string &text);
    NodeId symbol(const std::string &name);
    NodeId unary(Kind kind, NodeId a);
    NodeId binary(Kind kind, NodeId a, NodeId b);
    NodeId nary(Kind kind, const std::vector<NodeId> &args);
    NodeId call(const std::string &name, const std::vector<NodeId> &args);
    NodeId list(const std::vector<NodeId> &items);

    // A failed arena answers every request with kNoNode, and kNoNode read as an index reached past
    // the end of the chunk table and dereferenced whatever was there. Refused once here rather than
    // in each of the hundred call sites, because a rule added later would need the hundred and
    // first check. The status is left alone: the arena that produced the kNoNode has already
    // recorded why, and a bad id from a healthy arena is a logic fault rather than a resource one.
    const Node &at(NodeId id) const {
        if (id >= nodes_.size())
            return invalid_node();
        return nodes_[id];
    }
    ChildView children(const Node &n) const {
        return ChildView(&children_, n.child_offset, n.child_count);
    }
    ChildView children(NodeId id) const { return children(at(id)); }
    const std::string &text(NodeId id) const {
        if (id >= nodes_.size())
            return no_text();
        return strings_[nodes_[id].text];
    }
    // Test each reachable node at most once, including the root.
    template <class Predicate>
    bool any_node(NodeId id, Predicate matches) const {
        if (id == kNoNode || id >= node_count())
            return false;
        if (matches(id))
            return true;
        std::vector<NodeId> pending;
        for (NodeId child : children(id))
            pending.push_back(child);
        std::unordered_set<NodeId> visited;
        while (!pending.empty()) {
            const NodeId current = pending.back();
            pending.pop_back();
            if (!visited.insert(current).second)
                continue;
            if (matches(current))
                return true;
            for (NodeId child : children(current))
                pending.push_back(child);
        }
        return false;
    }

    size_t node_count() const { return nodes_.size(); }
    size_t child_slot_count() const { return children_.size(); }
    Status status() const { return status_; }
    const Limits &limits() const { return limits_; }

    bool failed() const { return status_ != Status::Ok; }
    void fail(Status s) {
        if (status_ == Status::Ok)
            status_ = s;
    }

    // MATH-009 needs to tell a backend's float from a decimal the user typed, and the two are the
    // same Kind. Hash consing makes them the same node too when they are spelled alike, so this can
    // only ever be per identity rather than per occurrence. Marking is one way on purpose: a
    // collision spreads approximate to a literal that was merely measured, which claims less
    // exactness than the value has and never more.
    void mark_approximate(NodeId id);
    bool is_approximate(NodeId id) const;

  private:
    // One shared inert node and one shared empty string, so a refused read hands back something
    // with a lifetime rather than a reference into nothing.
    static const Node &invalid_node() {
        // Value initialised rather than listed field by field, so a field added to Node later does
        // not leave this one uninitialised.
        static const Node node = [] {
            Node n = Node();
            n.kind = Kind::Invalid;
            return n;
        }();
        return node;
    }
    static const std::string &no_text() {
        static const std::string empty;
        return empty;
    }

    uint32_t intern(const std::string &s);
    // The children are passed alongside rather than already in the node, because hash consing
    // decides whether they are needed at all: an expression already held costs no pool slots.
    NodeId add_node(Node n, const std::vector<NodeId> &args);

    Limits limits_;
    Status status_ = Status::Ok;
    NodeStore nodes_;
    std::vector<NodeId> children_;
    std::vector<std::string> strings_;
    std::unordered_map<std::string, uint32_t> string_ids_;
    std::unordered_map<std::string, NodeId> interned_;
    // Grown only when something is marked, so an arena that never sees a backend float pays a word.
    std::vector<bool> approximate_;
};

}  // namespace nps

#endif
