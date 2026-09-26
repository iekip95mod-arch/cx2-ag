#include "nps/core/parser.h"

namespace nps {
namespace {

enum class Tok : uint8_t {
    End,
    Number,
    Name,
    Plus,
    Minus,
    Star,
    Slash,
    Caret,
    Superscript,
    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    Equals,
    Assign,
    Approx,
    Identity,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Bad,
};

struct Token {
    Tok kind = Tok::End;
    size_t start = 0;
    size_t end = 0;
    bool has_dot = false;
};

class Lexer {
  public:
    explicit Lexer(const std::string &src) : src_(src) {}

    Token next() {
        skip_space();
        Token t;
        t.start = pos_;
        if (pos_ >= src_.size()) {
            t.kind = Tok::End;
            t.end = pos_;
            return t;
        }

        char c = src_[pos_];
        if (is_digit(c) || (c == '.' && pos_ + 1 < src_.size() && is_digit(src_[pos_ + 1]))) {
            scan_number(&t);
            return t;
        }
        if (is_name_start(c)) {
            scan_name(&t);
            return t;
        }
        if (pos_ + 1 < src_.size() && src_.compare(pos_, 2, "\xCF\x80") == 0) {
            pos_ += 2;
            t.kind = Tok::Name;
            t.end = pos_;
            return t;
        }
        if (src_.compare(pos_, 3, "\xE2\x88\x9E") == 0) {
            pos_ += 3;
            t.kind = Tok::Name;
            t.end = pos_;
            return t;
        }
        if (const size_t bytes = minus_bytes()) {
            pos_ += bytes;
            t.kind = Tok::Minus;
            t.end = pos_;
            return t;
        }
        if (src_.compare(pos_, 3, "\xE2\x88\x9A") == 0) {
            pos_ += 3;
            // The radical is a function name only in front of its bracket, so it never becomes a symbol.
            t.kind = pos_ < src_.size() && src_[pos_] == '(' ? Tok::Name : Tok::Bad;
            t.end = pos_;
            return t;
        }
        // TI MathPrint spellings of operators the ASCII grammar already has.
        struct Glyph {
            const char *bytes;
            size_t size;
            Tok kind;
        };
        static const Glyph kGlyphs[] = {
            {"\xC3\x97", 2, Tok::Star},          {"\xC2\xB7", 2, Tok::Star},
            {"\xE2\x8B\x85", 3, Tok::Star},     {"\xC3\xB7", 2, Tok::Slash},
            {"\xE2\x89\xA4", 3, Tok::LessEqual}, {"\xE2\x89\xA5", 3, Tok::GreaterEqual},
        };
        for (const Glyph &glyph : kGlyphs) {
            if (src_.compare(pos_, glyph.size, glyph.bytes) == 0) {
                pos_ += glyph.size;
                t.kind = glyph.kind;
                t.end = pos_;
                return t;
            }
        }
        if (superscript_bytes(pos_, nullptr)) {
            t.kind = Tok::Superscript;
            char digit = 0;
            while (const size_t bytes = superscript_bytes(pos_, &digit))
                pos_ += bytes;
            t.end = pos_;
            return t;
        }

        ++pos_;
        switch (c) {
            case '+': t.kind = Tok::Plus; break;
            case '*': t.kind = Tok::Star; break;
            case '/': t.kind = Tok::Slash; break;
            case '^': t.kind = Tok::Caret; break;
            case '(': t.kind = Tok::LParen; break;
            case ')': t.kind = Tok::RParen; break;
            case '[': t.kind = Tok::LBracket; break;
            case ']': t.kind = Tok::RBracket; break;
            case ',': t.kind = Tok::Comma; break;
            // A second '=' makes identity, so a bare '=' has to look ahead now where it did not
            // before. ':' and '~' were both Bad until here, so neither spelling takes anything a
            // user could already type.
            case '=':
                if (pos_ < src_.size() && src_[pos_] == '=') {
                    ++pos_;
                    t.kind = Tok::Identity;
                } else {
                    t.kind = Tok::Equals;
                }
                break;
            case ':':
                if (pos_ < src_.size() && src_[pos_] == '=') {
                    ++pos_;
                    t.kind = Tok::Assign;
                } else {
                    t.kind = Tok::Bad;
                }
                break;
            case '~':
                if (pos_ < src_.size() && src_[pos_] == '=') {
                    ++pos_;
                    t.kind = Tok::Approx;
                } else {
                    t.kind = Tok::Bad;
                }
                break;
            case '<':
                if (pos_ < src_.size() && src_[pos_] == '=') {
                    ++pos_;
                    t.kind = Tok::LessEqual;
                } else {
                    t.kind = Tok::Less;
                }
                break;
            case '>':
                if (pos_ < src_.size() && src_[pos_] == '=') {
                    ++pos_;
                    t.kind = Tok::GreaterEqual;
                } else {
                    t.kind = Tok::Greater;
                }
                break;
            default: t.kind = Tok::Bad; break;
        }
        t.end = pos_;
        return t;
    }

  private:
    static bool is_digit(char c) { return c >= '0' && c <= '9'; }
    static bool is_name_start(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    static bool is_name_part(char c) { return is_name_start(c) || is_digit(c); }

    // The width of the superscript digit or minus at this position, zero when there is none.
    size_t superscript_bytes(size_t at, char *out) const {
        static const char *const kDigits[] = {"\xE2\x81\xB0", "\xC2\xB9", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xB4",
                                              "\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7", "\xE2\x81\xB8", "\xE2\x81\xB9"};
        for (int d = 0; d < 10; ++d) {
            const size_t size = kDigits[d][0] == '\xC2' ? 2 : 3;
            if (src_.compare(at, size, kDigits[d]) == 0) {
                if (out)
                    *out = static_cast<char>('0' + d);
                return size;
            }
        }
        if (src_.compare(at, 3, "\xE2\x81\xBB") == 0) {
            if (out)
                *out = '-';
            return 3;
        }
        return 0;
    }

  public:
    // The ASCII exponent a run of superscript characters spells, or empty when it spells none.
    std::string superscript_text(size_t start, size_t end) const {
        std::string text;
        char c = 0;
        for (size_t at = start; at < end;) {
            const size_t bytes = superscript_bytes(at, &c);
            if (!bytes)
                return std::string();
            text += c;
            at += bytes;
        }
        if (text.empty() || text.find('-', 1) != std::string::npos || text == "-")
            return std::string();
        return text;
    }

  private:
    size_t minus_bytes() const {
        if (src_.compare(pos_, 3, "\xE2\x88\x92") == 0) return 3;
        return pos_ < src_.size() && src_[pos_] == '-' ? 1 : 0;
    }

    void skip_space() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    void scan_number(Token *t) {
        t->kind = Tok::Number;
        while (pos_ < src_.size() && is_digit(src_[pos_]))
            ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            t->has_dot = true;
            ++pos_;
            while (pos_ < src_.size() && is_digit(src_[pos_]))
                ++pos_;
        }
        // An exponent is part of the literal, not a power. 1e3 is one token; x^3 is three.
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            size_t save = pos_;
            ++pos_;
            if (pos_ < src_.size() && src_[pos_] == '+')
                ++pos_;
            else
                pos_ += minus_bytes();
            if (pos_ < src_.size() && is_digit(src_[pos_])) {
                t->has_dot = true;
                while (pos_ < src_.size() && is_digit(src_[pos_]))
                    ++pos_;
            } else {
                pos_ = save;
            }
        }
        t->end = pos_;
    }

    void scan_name(Token *t) {
        t->kind = Tok::Name;
        while (pos_ < src_.size() && is_name_part(src_[pos_]))
            ++pos_;
        t->end = pos_;
    }

    const std::string &src_;
    size_t pos_ = 0;
};

// Parens and unary signs add stack frames but no nodes, so cap live recursion where add_node cannot.
struct DepthGuard {
    size_t &depth;
    explicit DepthGuard(size_t &d) : depth(d) { ++depth; }
    ~DepthGuard() { --depth; }
};

class Parser {
  public:
    Parser(Arena &arena, const std::string &src) : arena_(arena), src_(src), lexer_(src) {
        advance();
    }

    ParseResult run() {
        ParseResult r;
        NodeId root = relation();
        if (fail_.status != Status::Ok) {
            return fail_;
        }
        // Before the trailing token check, not after. A limit that fires mid-expression unwinds the
        // descent and leaves tokens unread, so checking for leftovers first reports every resource
        // refusal as a syntax error and hides which limit was hit.
        if (arena_.failed()) {
            r.status = arena_.status();
            r.offset = tok_.start;
            r.message = status_name(r.status);
            return r;
        }
        if (tok_.kind != Tok::End) {
            return error(Status::SyntaxError, "unexpected input after the expression");
        }
        r.root = root;
        return r;
    }

  private:
    void advance() { tok_ = lexer_.next(); }

    std::string spelling(const Token &t) const {
        std::string text = src_.substr(t.start, t.end - t.start);
        if (t.kind == Tok::Number) {
            const size_t minus = text.find("\xE2\x88\x92");
            if (minus != std::string::npos) text.replace(minus, 3, "-");
        }
        return normalize_identifier(text);
    }

    ParseResult error(Status s, const std::string &msg) {
        if (fail_.status == Status::Ok) {
            fail_.status = s;
            fail_.offset = tok_.start;
            fail_.message = msg;
        }
        return fail_;
    }

    bool stop() const { return fail_.status != Status::Ok || arena_.failed(); }

    NodeId negated(NodeId operand) {
        const Node &node = arena_.at(operand);
        if (node.kind == Kind::Integer && !node.small_valid) {
            const std::string &text = arena_.text(operand);
            if (!text.empty() && text[0] == '-')
                return arena_.integer(text.substr(1));
            return arena_.integer("-" + text);
        }
        return arena_.unary(Kind::Neg, operand);
    }

    NodeId relation() {
        NodeId left = sum();
        if (stop())
            return kNoNode;

        Kind kind;
        switch (tok_.kind) {
            case Tok::Equals: kind = Kind::Equals; break;
            case Tok::Assign: kind = Kind::Assign; break;
            case Tok::Approx: kind = Kind::Approx; break;
            case Tok::Identity: kind = Kind::Identity; break;
            case Tok::Less: kind = Kind::Less; break;
            case Tok::LessEqual: kind = Kind::LessEqual; break;
            case Tok::Greater: kind = Kind::Greater; break;
            case Tok::GreaterEqual: kind = Kind::GreaterEqual; break;
            default: return left;
        }
        advance();
        NodeId right = sum();
        if (stop())
            return kNoNode;
        // Chained relations are rejected rather than reassociated. a = b = c has two readings and
        // guessing one of them puts an unstated assumption into a derivation.
        switch (tok_.kind) {
            case Tok::Equals:
            case Tok::Assign:
            case Tok::Approx:
            case Tok::Identity:
            case Tok::Less:
            case Tok::LessEqual:
            case Tok::Greater:
            case Tok::GreaterEqual:
                error(Status::SyntaxError, "chained relations are not accepted, use one at a time");
                return kNoNode;
            default: break;
        }
        return arena_.binary(kind, left, right);
    }

    NodeId sum() {
        NodeId left = term();
        if (stop())
            return kNoNode;
        while (tok_.kind == Tok::Plus || tok_.kind == Tok::Minus) {
            bool subtract = tok_.kind == Tok::Minus;
            advance();
            NodeId right = term();
            if (stop())
                return kNoNode;
            if (subtract)
                right = negated(right);
            left = arena_.binary(Kind::Add, left, right);
        }
        return left;
    }

    NodeId term() {
        NodeId left = unary();
        if (stop())
            return kNoNode;
        while (true) {
            if (tok_.kind == Tok::Star || tok_.kind == Tok::Slash) {
                bool divide = tok_.kind == Tok::Slash;
                advance();
                NodeId right = unary();
                if (stop())
                    return kNoNode;
                if (divide) {
                    NodeId minus_one = arena_.unary(Kind::Neg, arena_.integer("1"));
                    right = arena_.binary(Kind::Pow, right, minus_one);
                }
                left = arena_.binary(Kind::Mul, left, right);
                continue;
            }
            // Juxtaposition: 2x and 3sin(x) multiply. A name directly after a name does not, so
            // "x y" stays an error rather than quietly becoming a product of two unknowns.
            // A number after a number is that same error, so "2 3" and "1.2.3" are refused.
            if (tok_.kind == Tok::LParen || (tok_.kind == Tok::Number && !last_was_number_)) {
                NodeId right = unary();
                if (stop())
                    return kNoNode;
                left = arena_.binary(Kind::Mul, left, right);
                continue;
            }
            if (tok_.kind == Tok::Name && last_was_number_) {
                NodeId right = unary();
                if (stop())
                    return kNoNode;
                left = arena_.binary(Kind::Mul, left, right);
                continue;
            }
            return left;
        }
    }

    NodeId unary() {
        DepthGuard guard(depth_);
        // 3x because the reparsed printed form is fully parenthesised (a Neg is two parens), so a within-limit tree can need about twice max_depth of recursion.
        if (depth_ > 3 * arena_.limits().max_depth) {
            error(Status::DepthExceeded, "the expression is nested deeper than the accepted limit");
            return kNoNode;
        }
        if (tok_.kind == Tok::Minus) {
            advance();
            NodeId operand = unary();
            if (stop())
                return kNoNode;
            return negated(operand);
        }
        if (tok_.kind == Tok::Plus) {
            advance();
            return unary();
        }
        return power();
    }

    NodeId power() {
        NodeId base = atom();
        if (stop())
            return kNoNode;
        if (tok_.kind == Tok::Superscript) {
            const std::string text = lexer_.superscript_text(tok_.start, tok_.end);
            if (text.empty()) {
                error(Status::SyntaxError, "a superscript needs at least one digit");
                return kNoNode;
            }
            advance();
            last_was_number_ = false;
            const bool negative = text[0] == '-';
            NodeId exponent = arena_.integer(negative ? text.substr(1) : text);
            if (negative)
                exponent = arena_.unary(Kind::Neg, exponent);
            return arena_.binary(Kind::Pow, base, exponent);
        }
        if (tok_.kind != Tok::Caret)
            return base;
        advance();
        // Right associative, and the exponent takes a unary minus, so x^-1 parses.
        NodeId exponent = unary();
        if (stop())
            return kNoNode;
        last_was_number_ = false;
        return arena_.binary(Kind::Pow, base, exponent);
    }

    NodeId atom() {
        switch (tok_.kind) {
            case Tok::Number: {
                Token t = tok_;
                advance();
                last_was_number_ = true;
                std::string text = spelling(t);
                return t.has_dot ? arena_.decimal(text) : arena_.integer(text);
            }
            case Tok::Name: {
                Token t = tok_;
                std::string name = spelling(t);
                advance();
                if (tok_.kind == Tok::LParen) {
                    advance();
                    std::vector<NodeId> args;
                    if (tok_.kind != Tok::RParen) {
                        while (true) {
                            NodeId arg = relation();
                            if (stop())
                                return kNoNode;
                            args.push_back(arg);
                            if (tok_.kind != Tok::Comma)
                                break;
                            advance();
                        }
                    }
                    if (tok_.kind != Tok::RParen) {
                        error(Status::SyntaxError, "expected a closing parenthesis");
                        return kNoNode;
                    }
                    advance();
                    last_was_number_ = false;
                    return arena_.call(name, args);
                }
                last_was_number_ = false;
                return arena_.symbol(name);
            }
            case Tok::LBracket: {
                advance();
                std::vector<NodeId> items;
                if (tok_.kind != Tok::RBracket) {
                    while (true) {
                        const NodeId item = relation();
                        if (stop())
                            return kNoNode;
                        items.push_back(item);
                        if (tok_.kind == Tok::LBracket && arena_.at(item).kind == Kind::List)
                            continue;
                        if (tok_.kind != Tok::Comma)
                            break;
                        advance();
                    }
                }
                if (tok_.kind != Tok::RBracket) {
                    error(Status::SyntaxError, "expected a closing square bracket");
                    return kNoNode;
                }
                advance();
                last_was_number_ = false;
                return arena_.list(items);
            }
            case Tok::LParen: {
                advance();
                NodeId inner = relation();
                if (stop())
                    return kNoNode;
                if (tok_.kind != Tok::RParen) {
                    error(Status::SyntaxError, "expected a closing parenthesis");
                    return kNoNode;
                }
                advance();
                last_was_number_ = false;
                return inner;
            }
            case Tok::End:
                error(Status::SyntaxError, "the expression ends where a value was expected");
                return kNoNode;
            default:
                error(Status::SyntaxError, "expected a number, a name or a parenthesis");
                return kNoNode;
        }
    }

    Arena &arena_;
    const std::string &src_;
    Lexer lexer_;
    Token tok_;
    ParseResult fail_;
    bool last_was_number_ = false;
    size_t depth_ = 0;
};

}  // namespace

bool is_identifier(const std::string &input, size_t max_input_bytes) {
    if (input.size() > max_input_bytes) return false;
    Lexer lexer(input);
    const Token token = lexer.next();
    return token.kind == Tok::Name && token.start == 0 && token.end == input.size();
}

std::string normalize_identifier(const std::string &input) {
    if (input == "\xE2\x88\x9E") return "infinity";
    if (input == "\xE2\x88\x9A") return "sqrt";
    return input == "\xCF\x80" ? "pi" : input;
}

// A refused input is the input's failure, not the arena's: the arena holds nothing it should not, so
// it stays usable, and only its own limits in add_node mark it failed.
ParseResult parse(Arena &arena, const std::string &input) {
    ParseResult r;
    if (input.size() > arena.limits().max_input_bytes) {
        r.status = Status::InputTooLong;
        r.offset = arena.limits().max_input_bytes;
        r.message = "input is longer than the accepted limit";
        return r;
    }
    Parser p(arena, input);
    return p.run();
}

}  // namespace nps
