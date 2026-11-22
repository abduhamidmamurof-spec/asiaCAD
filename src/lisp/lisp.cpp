// AsiaCAD AutoLISP yadrosi: o'quvchi, baholovchi, tayyor funksiyalar.
#include <asiacad/lisp/lisp.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>

namespace asiacad::lisp {

namespace {
constexpr double kPi = 3.14159265358979323846;

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
[[noreturn]] void fail(const std::string& m) { throw LispError(m); }

// AutoLISP wcmatch: # (raqam), @ (harf), . (harf-raqam emas), * ? ~ [..] ` , (bir nechta naqsh)
bool wcOne(const char* p, const char* pe, const char* s, const char* se) {
    while (p < pe) {
        const char c = *p;
        if (c == '*') {
            ++p;
            for (const char* k = s; k <= se; ++k)
                if (wcOne(p, pe, k, se)) return true;
            return false;
        }
        if (s >= se) return false;
        const unsigned char ch = static_cast<unsigned char>(*s);
        if (c == '?') { ++p; ++s; continue; }
        if (c == '#') { if (!std::isdigit(ch)) return false; ++p; ++s; continue; }
        if (c == '@') { if (!std::isalpha(ch)) return false; ++p; ++s; continue; }
        if (c == '.') { if (std::isalnum(ch)) return false; ++p; ++s; continue; }
        if (c == '[') {
            const char* q = p + 1;
            bool neg = false;
            if (q < pe && *q == '~') { neg = true; ++q; }
            bool hit = false;
            const char* st = q;
            while (q < pe && (*q != ']' || q == st)) {
                if (q + 2 < pe && q[1] == '-' && q[2] != ']') {
                    if (ch >= static_cast<unsigned char>(q[0]) && ch <= static_cast<unsigned char>(q[2])) hit = true;
                    q += 3;
                } else {
                    if (ch == static_cast<unsigned char>(*q)) hit = true;
                    ++q;
                }
            }
            if (hit == neg) return false;
            p = q < pe ? q + 1 : q;
            ++s;
            continue;
        }
        if (c == '`' && p + 1 < pe) { if (*s != p[1]) return false; p += 2; ++s; continue; }
        if (c != *s) return false;
        ++p; ++s;
    }
    return s == se;
}
bool wcmatch(const std::string& s, const std::string& pat) {
    // vergul bilan ajratilgan naqshlar; ~ boshida — inkor
    std::size_t b = 0;
    for (;;) {
        std::size_t e = b;
        while (e < pat.size() && !(pat[e] == ',' && (e == 0 || pat[e - 1] != '`'))) ++e;
        std::string one = pat.substr(b, e - b);
        bool neg = false;
        if (!one.empty() && one[0] == '~') { neg = true; one.erase(0, 1); }
        const bool m = wcOne(one.data(), one.data() + one.size(), s.data(), s.data() + s.size());
        if (m != neg) return true;
        if (e >= pat.size()) return false;
        b = e + 1;
    }
}

std::string fmtReal(double r) {
    if (std::isnan(r)) return "nan";
    std::string s = std::format("{:.6g}", r);
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}
}  // namespace

bool lispWcmatch(const std::string& s, const std::string& pat) { return wcmatch(s, pat); }

// ------------------------------------------------------------ konstruktorlar
V Lisp::mkInt(long long i) { auto v = std::make_shared<Value>(Kind::Int); v->i = i; return v; }
V Lisp::mkReal(double r) { auto v = std::make_shared<Value>(Kind::Real); v->r = r; return v; }
V Lisp::mkStr(std::string s) { auto v = std::make_shared<Value>(Kind::Str); v->s = std::move(s); return v; }
V Lisp::cons(V a, V d) { auto v = std::make_shared<Value>(Kind::Cons); v->car = std::move(a); v->cdr = std::move(d); return v; }
V Lisp::list(std::initializer_list<V> xs) { return fromVector(std::vector<V>(xs)); }
V Lisp::fromVector(const std::vector<V>& xs, V tail) {
    V r = std::move(tail);
    for (auto it = xs.rbegin(); it != xs.rend(); ++it) r = cons(*it, r);
    return r;
}
std::vector<V> Lisp::toVector(const V& l) {
    std::vector<V> r;
    for (V p = l; p; p = p->cdr) {
        if (p->kind != Kind::Cons) fail("bad list: " + std::string("dotted"));
        r.push_back(p->car);
    }
    return r;
}
V Lisp::mkEname(std::uint64_t id) const { auto v = std::make_shared<Value>(Kind::Ename); v->ename = id; return v; }
V Lisp::mkPickSet(std::vector<std::uint64_t> ids) const {
    auto v = std::make_shared<Value>(Kind::PickSet);
    v->ss = std::make_shared<std::vector<std::uint64_t>>(std::move(ids));
    return v;
}
V Lisp::mkPoint(double x, double y, double z) const { return list({mkReal(x), mkReal(y), mkReal(z)}); }

double Lisp::num(const V& v, const char* fn) {
    if (v && v->kind == Kind::Int) return static_cast<double>(v->i);
    if (v && v->kind == Kind::Real) return v->r;
    (void)fn;
    fail("bad argument type: numberp: " + (v ? std::string(v->kind == Kind::Str ? "\"" + v->s + "\"" : v->kind == Kind::Sym ? v->s : "...") : "nil"));
}
std::string Lisp::str(const V& v, const char* fn) {
    if (v && v->kind == Kind::Str) return v->s;
    (void)fn;
    fail(std::string("bad argument type: stringp ") + (v ? (v->kind == Kind::Sym ? v->s : isNum(v) ? (v->kind == Kind::Int ? std::to_string(v->i) : fmtReal(v->r)) : "...") : "nil"));
}

bool Lisp::eqV(const V& a, const V& b) {
    if (a == b) return true;
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
    case Kind::Int: return a->i == b->i;
    case Kind::Sym: return a->s == b->s;
    case Kind::Ename: return a->ename == b->ename;
    default: return false;
    }
}
bool Lisp::equalV(const V& a, const V& b, double fuzz) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (isNum(a) && isNum(b)) {
        if (fuzz == 0 && a->kind == Kind::Int && b->kind == Kind::Int) return a->i == b->i;
        return std::abs(num(a, "") - num(b, "")) <= fuzz;
    }
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case Kind::Str: case Kind::Sym: return a->s == b->s;
    case Kind::Ename: return a->ename == b->ename;
    case Kind::Cons: return equalV(a->car, b->car, fuzz) && equalV(a->cdr, b->cdr, fuzz);
    case Kind::PickSet: return a->ss == b->ss;
    default: return false;
    }
}

bool Lisp::pointOf(const V& v, double& x, double& y, double& z) const {
    if (!v) return false;
    if (v->kind == Kind::Str) {
        double c[3] = {0, 0, 0};
        int n = 0;
        std::stringstream ss(v->s);
        std::string part;
        while (n < 3 && std::getline(ss, part, ',')) {
            char* end = nullptr;
            c[n] = std::strtod(part.c_str(), &end);
            if (end == part.c_str()) return false;
            ++n;
        }
        if (n < 2) return false;
        x = c[0]; y = c[1]; z = c[2];
        return true;
    }
    if (v->kind != Kind::Cons) return false;
    std::vector<double> c;
    for (V p = v; p && p->kind == Kind::Cons && c.size() < 4; p = p->cdr) {
        if (!isNum(p->car)) return false;
        c.push_back(num(p->car, ""));
    }
    if (c.size() < 2 || c.size() > 3) return false;
    x = c[0]; y = c[1]; z = c.size() > 2 ? c[2] : 0;
    return true;
}

std::string Lisp::toString(const V& v, bool readable) const {
    if (!v) return "nil";
    switch (v->kind) {
    case Kind::Int: return std::to_string(v->i);
    case Kind::Real: return fmtReal(v->r);
    case Kind::Str: {
        if (!readable) return v->s;
        std::string r = "\"";
        for (char c : v->s) {
            if (c == '"') r += "\\\"";
            else if (c == '\\') r += "\\\\";
            else if (c == '\n') r += "\\n";
            else if (c == '\t') r += "\\t";
            else r += c;
        }
        return r + "\"";
    }
    case Kind::Sym: return v->s;
    case Kind::Ename: return std::format("<Entity name: {:x}>", v->ename);
    case Kind::PickSet: return std::format("<Selection set: {}>", v->ss ? v->ss->size() : 0);
    case Kind::Subr: return "#<SUBR @" + v->s + ">";
    case Kind::Lambda: return "#<USUBR @" + (v->s.empty() ? std::string("LAMBDA") : v->s) + ">";
    case Kind::File: return "#<file>";
    case Kind::Variant: return "#<variant>";
    case Kind::Cons: {
        if (v->car && v->car->kind == Kind::Sym && v->car->s == "QUOTE" && v->cdr && v->cdr->kind == Kind::Cons && !v->cdr->cdr)
            return "'" + toString(v->cdr->car, readable);
        std::string r = "(";
        V p = v;
        bool first = true;
        while (p && p->kind == Kind::Cons) {
            if (!first) r += " ";
            r += toString(p->car, readable);
            first = false;
            p = p->cdr;
        }
        if (p) r += " . " + toString(p, readable);
        return r + ")";
    }
    }
    return "?";
}

// ------------------------------------------------------------ belgilar
Lisp::Lisp(std::ostream& out, LispHost* host) : out_(out), host_(host) {
    t_ = sym("T");
    globals_["T"] = t_;
    globals_["PI"] = mkReal(kPi);
    installBuiltins();
    installHostBuiltins();
}
V Lisp::sym(const std::string& name) {
    const std::string u = upper(name);
    auto it = symbols_.find(u);
    if (it != symbols_.end()) return it->second;
    auto v = std::make_shared<Value>(Kind::Sym);
    v->s = u;
    symbols_[u] = v;
    return v;
}
V Lisp::getGlobal(const std::string& name) const {
    auto it = globals_.find(upper(name));
    return it == globals_.end() ? nullptr : it->second;
}
void Lisp::setGlobal(const std::string& name, V v) {
    const std::string u = upper(name);
    if (u == "NIL" || u == "T") fail("attempt to set constant: " + u);
    globals_[u] = std::move(v);
}
void Lisp::defineBuiltin(const std::string& name, Builtin fn) {
    auto v = std::make_shared<Value>(Kind::Subr);
    v->s = upper(name);
    v->fn = std::move(fn);
    globals_[upper(name)] = v;
}
std::vector<std::string> Lisp::commands() const {
    std::vector<std::string> r;
    for (const auto& [k, v] : globals_)
        if (k.size() > 2 && k.compare(0, 2, "C:") == 0 && v && (v->kind == Kind::Lambda || v->kind == Kind::Subr)) r.push_back(k.substr(2));
    return r;
}
bool Lisp::hasCommand(const std::string& name) const {
    auto v = getGlobal("C:" + name);
    return v && (v->kind == Kind::Lambda || v->kind == Kind::Subr);
}

// ------------------------------------------------------------ o'quvchi
std::vector<V> Lisp::read(const std::string& src) {
    std::size_t i = 0;
    const std::size_t n = src.size();
    auto skipWs = [&]() {
        for (;;) {
            while (i < n && std::isspace(static_cast<unsigned char>(src[i]))) ++i;
            if (i < n && src[i] == ';') {
                if (i + 1 < n && src[i + 1] == '|') {  // ;| ... |;
                    auto e = src.find("|;", i + 2);
                    i = e == std::string::npos ? n : e + 2;
                } else {
                    while (i < n && src[i] != '\n') ++i;
                }
                continue;
            }
            break;
        }
    };
    std::function<V()> readOne = [&]() -> V {
        skipWs();
        if (i >= n) fail("malformed list on input");
        const char c = src[i];
        if (c == '(') {
            ++i;
            std::vector<V> items;
            V tail;
            for (;;) {
                skipWs();
                if (i >= n) fail("malformed list on input");
                if (src[i] == ')') { ++i; break; }
                if (src[i] == '.' && i + 1 < n && (std::isspace(static_cast<unsigned char>(src[i + 1])) || src[i + 1] == '(' || src[i + 1] == '"')) {
                    ++i;
                    tail = readOne();
                    skipWs();
                    if (i < n && src[i] == ')') ++i;
                    else fail("malformed list on input");
                    break;
                }
                items.push_back(readOne());
            }
            return fromVector(items, tail);
        }
        if (c == ')') { ++i; fail("extra right paren on input"); }
        if (c == '\'') { ++i; return list({sym("QUOTE"), readOne()}); }
        if (c == '"') {
            ++i;
            std::string s;
            while (i < n && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < n) {
                    const char e = src[++i];
                    if (e == 'n') s += '\n';
                    else if (e == 't') s += '\t';
                    else if (e == 'r') s += '\r';
                    else if (e == 'e') s += '\x1b';
                    else if (std::isdigit(static_cast<unsigned char>(e)) && i + 2 < n) {  // \nnn sakkizlik
                        s += static_cast<char>(std::stoi(src.substr(i, 3), nullptr, 8));
                        i += 2;
                    } else s += e;
                    ++i;
                } else s += src[i++];
            }
            if (i >= n) fail("malformed string on input");
            ++i;
            return mkStr(s);
        }
        std::size_t b = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(src[i])) && src[i] != '(' && src[i] != ')' && src[i] != '\'' && src[i] != '"' && src[i] != ';') ++i;
        const std::string tok = src.substr(b, i - b);
        // son?
        {
            char* end = nullptr;
            const bool looksNum = !tok.empty() && (std::isdigit(static_cast<unsigned char>(tok[0])) ||
                                                   ((tok[0] == '-' || tok[0] == '+' || tok[0] == '.') && tok.size() > 1));
            if (looksNum) {
                const bool real = tok.find_first_of(".eE") != std::string::npos;
                if (!real) {
                    long long v = std::strtoll(tok.c_str(), &end, 10);
                    if (end && *end == 0) return mkInt(v);
                }
                double d = std::strtod(tok.c_str(), &end);
                if (end && *end == 0) return mkReal(d);
            }
        }
        const std::string u = upper(tok);
        if (u == "NIL") return nullptr;
        return sym(u);
    };
    std::vector<V> r;
    for (;;) {
        skipWs();
        if (i >= n) break;
        r.push_back(readOne());
    }
    return r;
}

// ------------------------------------------------------------ baholovchi
V Lisp::progn(const V& body) {
    V r;
    for (V p = body; p; p = p->cdr) r = eval(p->car);
    return r;
}

namespace {
struct DepthGuard {
    int& d;
    explicit DepthGuard(int& x) : d(x) {
        if (++d > 4000) { --d; fail("stack overflow (recursion too deep)"); }
    }
    ~DepthGuard() { --d; }
};
V nth(const V& l, int k) {
    V p = l;
    for (int i = 0; i < k && p; ++i) p = p->cdr;
    return p ? p->car : nullptr;
}
}  // namespace

V Lisp::eval(const V& x) {
    if (!x) return nullptr;
    switch (x->kind) {
    case Kind::Sym: {
        auto it = globals_.find(x->s);
        return it == globals_.end() ? nullptr : it->second;
    }
    case Kind::Cons: return evalList(x);
    default: return x;
    }
}

V Lisp::evalList(const V& x) {
    DepthGuard g(depth_);
    const V head = x->car;
    if (head && head->kind == Kind::Sym) {
        const std::string& h = head->s;
        const V rest = x->cdr;
        if (h == "QUOTE" || h == "FUNCTION") return rest ? rest->car : nullptr;
        if (h == "SETQ") {
            V r;
            for (V p = rest; p; p = p->cdr ? p->cdr->cdr : nullptr) {
                if (!p->car || p->car->kind != Kind::Sym) fail("bad argument type: symbolp " + toString(p->car, true));
                r = p->cdr ? eval(p->cdr->car) : nullptr;
                setGlobal(p->car->s, r);
                if (!p->cdr) break;
            }
            return r;
        }
        if (h == "IF") {
            if (!rest) fail("too few arguments");
            return truthy(eval(rest->car)) ? eval(nth(rest, 1)) : eval(nth(rest, 2));
        }
        if (h == "COND") {
            for (V p = rest; p; p = p->cdr) {
                const V cl = p->car;
                if (!cl) continue;
                V t = eval(cl->car);
                if (truthy(t)) return cl->cdr ? progn(cl->cdr) : t;
            }
            return nullptr;
        }
        if (h == "WHILE") {
            V r;
            long long guard = 0;
            while (truthy(eval(rest ? rest->car : nullptr))) {
                r = progn(rest->cdr);
                if (++guard > 50'000'000) fail("infinite loop");
            }
            return r;
        }
        if (h == "REPEAT") {
            const long long k = static_cast<long long>(num(eval(rest ? rest->car : nullptr), "repeat"));
            V r;
            for (long long j = 0; j < k; ++j) r = progn(rest->cdr);
            return r;
        }
        if (h == "FOREACH" || h == "VLAX-FOR") {
            const V var = rest ? rest->car : nullptr;
            if (!var || var->kind != Kind::Sym) fail("bad argument type: symbolp");
            V lst = eval(nth(rest, 1));
            if (h == "VLAX-FOR") {  // ActiveX to'plami elementlari (host beradi)
                if (!collectionItems) fail("vlax-for: no collections");
                lst = fromVector(collectionItems(lst));
            }
            const V body = rest->cdr ? rest->cdr->cdr : nullptr;
            auto it = globals_.find(var->s);
            const bool had = it != globals_.end();
            const V old = had ? it->second : nullptr;
            V r;
            try {
                for (V p = lst; p && p->kind == Kind::Cons; p = p->cdr) {
                    globals_[var->s] = p->car;
                    r = progn(body);
                }
            } catch (...) {
                if (had) globals_[var->s] = old; else globals_.erase(var->s);
                throw;
            }
            if (had) globals_[var->s] = old; else globals_.erase(var->s);
            return r;
        }
        if (h == "PROGN") return progn(rest);
        if (h == "AND") {
            for (V p = rest; p; p = p->cdr)
                if (!truthy(eval(p->car))) return nullptr;
            return t_;
        }
        if (h == "OR") {
            for (V p = rest; p; p = p->cdr)
                if (truthy(eval(p->car))) return t_;
            return nullptr;
        }
        if (h == "LAMBDA") {
            auto lam = std::make_shared<Value>(Kind::Lambda);
            lam->params = rest ? rest->car : nullptr;
            lam->body = rest ? rest->cdr : nullptr;
            return lam;
        }
        if (h == "DEFUN" || h == "DEFUN-Q") {
            const V name = rest ? rest->car : nullptr;
            if (!name || name->kind != Kind::Sym) fail("bad argument type: symbolp");
            auto lam = std::make_shared<Value>(Kind::Lambda);
            lam->s = name->s;
            lam->params = nth(rest, 1);
            lam->body = rest->cdr ? rest->cdr->cdr : nullptr;
            globals_[name->s] = lam;
            return name;
        }
        if (h == "VL-CATCH-ALL-APPLY") {
            V f = eval(nth(rest, 0));
            auto args = toVector(eval(nth(rest, 1)));
            try {
                return apply(f, args);
            } catch (const LispExit&) {
                throw;
            } catch (const LispError& e) {
                auto ev = std::make_shared<Value>(Kind::Variant);
                ev->s = e.what();
                ev->i = 1;  // catch-all-apply-error
                return ev;
            }
        }
    }
    // funksiya chaqiruvi
    V f;
    if (head && head->kind == Kind::Sym) {
        auto it = globals_.find(head->s);
        if ((it == globals_.end() || !it->second) && resolver) {  // dinamik nomlar (vla-get-XXX ...)
            if (Builtin b = resolver(head->s)) { defineBuiltin(head->s, b); it = globals_.find(head->s); }
        }
        if (it == globals_.end() || !it->second || (it->second->kind != Kind::Subr && it->second->kind != Kind::Lambda))
            fail("no function definition: " + head->s +
                 (head->s.rfind("VLA", 0) == 0 || head->s.rfind("VLAX", 0) == 0 || head->s.rfind("VLR", 0) == 0 ? " (ActiveX)" :
                  head->s.rfind("DCL", 0) == 0 || head->s == "LOAD_DIALOG" || head->s == "NEW_DIALOG" || head->s == "START_DIALOG" ? " (DCL)" : ""));
        f = it->second;
    } else {
        f = eval(head);
    }
    std::vector<V> args;
    for (V p = x->cdr; p; p = p->cdr) {
        if (p->kind != Kind::Cons) fail("malformed list");
        args.push_back(eval(p->car));
    }
    return apply(f, args);
}

V Lisp::apply(const V& fv, std::vector<V>& args) {
    V f = fv;
    if (f && f->kind == Kind::Sym) {
        auto it = globals_.find(f->s);
        if ((it == globals_.end() || !it->second) && resolver)
            if (Builtin b = resolver(f->s)) { defineBuiltin(f->s, b); it = globals_.find(f->s); }
        if (it == globals_.end() || !it->second) fail("no function definition: " + f->s);
        f = it->second;
    }
    if (f && f->kind == Kind::Cons && f->car && f->car->kind == Kind::Sym && f->car->s == "LAMBDA") f = eval(f);
    if (!f) fail("no function definition: nil");
    if (f->kind == Kind::Subr) return f->fn(*this, args);
    if (f->kind == Kind::Lambda) return callLambda(f, args);
    fail("bad function: " + toString(f, true));
}

V Lisp::callLambda(const V& lam, std::vector<V>& args) {
    DepthGuard g(depth_);
    std::vector<std::string> names, locals;
    bool loc = false;
    for (V p = lam->params; p && p->kind == Kind::Cons; p = p->cdr) {
        if (!p->car || p->car->kind != Kind::Sym) fail("bad argument list");
        if (p->car->s == "/") { loc = true; continue; }
        (loc ? locals : names).push_back(p->car->s);
    }
    if (args.size() < names.size()) fail("too few arguments");
    if (args.size() > names.size()) fail("too many arguments");
    // dinamik bog'lash: eski qiymatlar saqlanadi
    std::vector<std::pair<std::string, std::optional<V>>> saved;
    saved.reserve(names.size() + locals.size());
    auto bind = [&](const std::string& nm, V v) {
        auto it = globals_.find(nm);
        saved.emplace_back(nm, it == globals_.end() ? std::optional<V>{} : std::optional<V>{it->second});
        globals_[nm] = std::move(v);
    };
    for (std::size_t k = 0; k < names.size(); ++k) bind(names[k], args[k]);
    for (const auto& l : locals) bind(l, nullptr);
    auto restore = [&]() {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
            if (it->second) globals_[it->first] = *it->second;
            else globals_.erase(it->first);
        }
    };
    V r;
    try {
        r = progn(lam->body);
    } catch (const LispError& e) {
        // AutoCAD kabi: *error* xato joyida (mahalliy bog'lanishlar hali amalda) chaqiriladi
        if (!errHandled_) {
            V eh = getGlobal("*ERROR*");
            if (eh && (eh->kind == Kind::Lambda || eh->kind == Kind::Subr)) {
                errHandled_ = true;
                std::vector<V> a{mkStr(e.what())};
                try { apply(eh, a); } catch (const LispError&) {}
            }
        }
        restore();
        throw;
    } catch (...) {
        restore();
        throw;
    }
    restore();
    return r;
}

V Lisp::evalString(const std::string& src) {
    errHandled_ = false;
    V r;
    for (const auto& x : read(src)) r = eval(x);
    return r;
}

V Lisp::loadFile(const std::string& path) {
    std::string p = path;
    if (host_) {
        if (auto f = host_->findFile(path); !f.empty()) p = f;
        else if (auto f2 = host_->findFile(path + ".lsp"); !f2.empty()) p = f2;
    }
    std::ifstream in(p, std::ios::binary);
    if (!in && p.size() < 4) in.open(p + ".lsp", std::ios::binary);
    if (!in) fail("LOAD failed: \"" + path + "\"");
    std::stringstream ss;
    ss << in.rdbuf();
    std::string src = ss.str();
    if (src.size() >= 3 && static_cast<unsigned char>(src[0]) == 0xEF) src.erase(0, 3);  // UTF-8 BOM
    return evalString(src);
}

V Lisp::callCommand(const std::string& name) {
    V f = getGlobal("C:" + name);
    std::vector<V> none;
    errHandled_ = false;
    try {
        return apply(f, none);
    } catch (const LispError& e) {
        if (takeErrorHandled()) return nullptr;  // *error* allaqachon ishladi
        // AutoCAD kabi: *error* chaqiriladi, so'ng xabar
        V eh = getGlobal("*ERROR*");
        if (eh && (eh->kind == Kind::Lambda || eh->kind == Kind::Subr)) {
            std::vector<V> a{mkStr(e.what())};
            try { apply(eh, a); } catch (const LispError&) {}
            return nullptr;
        }
        throw;
    }
}

}  // namespace asiacad::lisp
