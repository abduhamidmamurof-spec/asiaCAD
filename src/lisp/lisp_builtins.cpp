// AsiaCAD AutoLISP: tayyor funksiyalar (AutoLISP + Visual LISP vl-*).
#include <asiacad/lisp/lisp.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <numeric>
#include <sstream>

namespace asiacad::lisp {

bool lispWcmatch(const std::string& s, const std::string& pat);  // lisp.cpp

namespace {
constexpr double kPi = 3.14159265358979323846;
[[noreturn]] void fail(const std::string& m) { throw LispError(m); }
using A = std::vector<V>;
void argc(const A& a, std::size_t mn, std::size_t mx) {
    if (a.size() < mn) fail("too few arguments");
    if (a.size() > mx) fail("too many arguments");
}
std::string upper(std::string s) { for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
bool isInt(const V& v) { return v && v->kind == Kind::Int; }
V boolV(Lisp& L, bool b) { return b ? L.T() : nullptr; }
double numA(const V& v) { return Lisp::num(v, ""); }

// AutoLISP aritmetikasi: hammasi butun bo'lsa — butun
V arith(Lisp&, A& a, char op) {
    if (a.empty()) return Lisp::mkInt(op == '*' ? 1 : 0);
    bool allInt = true;
    for (auto& v : a) {
        if (!Lisp::isNum(v)) Lisp::num(v, "");
        if (v->kind != Kind::Int) allInt = false;
    }
    if (allInt) {
        long long r = a[0]->i;
        if (a.size() == 1) {
            if (op == '-') return Lisp::mkInt(-r);
            if (op == '/') return Lisp::mkInt(r);
            return Lisp::mkInt(r);
        }
        for (std::size_t k = 1; k < a.size(); ++k) {
            const long long b = a[k]->i;
            if (op == '+') r += b;
            else if (op == '-') r -= b;
            else if (op == '*') r *= b;
            else { if (b == 0) fail("divide by zero"); r /= b; }
        }
        return Lisp::mkInt(r);
    }
    double r = numA(a[0]);
    if (a.size() == 1) return Lisp::mkReal(op == '-' ? -r : r);
    for (std::size_t k = 1; k < a.size(); ++k) {
        const double b = numA(a[k]);
        if (op == '+') r += b;
        else if (op == '-') r -= b;
        else if (op == '*') r *= b;
        else { if (b == 0) fail("divide by zero"); r /= b; }
    }
    return Lisp::mkReal(r);
}

int cmpV(const V& a, const V& b) {
    if (a && b && a->kind == Kind::Str && b->kind == Kind::Str) return a->s < b->s ? -1 : a->s > b->s ? 1 : 0;
    const double x = numA(a), y = numA(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

std::string rtos(double v, int mode, int prec) {
    if (prec < 0) prec = 4;
    if (prec > 15) prec = 15;
    switch (mode) {
    case 1: return std::format("{:.{}E}", v, prec);
    case 3: case 4: {  // muhandislik / arxitektura (fut-dyuym) — dyuymda
        const double feet = std::floor(std::abs(v) / 12.0);
        const double inch = std::abs(v) - feet * 12.0;
        return std::string(v < 0 ? "-" : "") + std::format("{:g}'-{:.{}f}\"", feet, inch, mode == 3 ? prec : 0);
    }
    case 5: return std::format("{:.{}f}", v, prec);
    default: return std::format("{:.{}f}", v, prec);
    }
}
std::string angtos(double rad, int mode, int prec) {
    if (prec < 0) prec = 0;
    double a = std::fmod(rad, 2 * kPi);
    if (a < 0) a += 2 * kPi;
    const double deg = a * 180.0 / kPi;
    switch (mode) {
    case 1: {
        int d = static_cast<int>(deg);
        double mf = (deg - d) * 60.0;
        int m = static_cast<int>(mf);
        double s = (mf - m) * 60.0;
        if (prec <= 2) return std::format("{}d{}'", d, static_cast<int>(std::round(mf)));
        return std::format("{}d{}'{:.0f}\"", d, m, s);
    }
    case 2: return std::format("{:.{}f}g", a * 200.0 / kPi, prec);
    case 3: return std::format("{:.{}f}r", a, prec);
    default: return std::format("{:.{}f}", deg, prec);
    }
}
double parseAngle(const std::string& s0) {
    std::string s = s0;
    if (s.empty()) fail("bad angle");
    const char last = static_cast<char>(std::tolower(static_cast<unsigned char>(s.back())));
    if (last == 'r') return std::stod(s.substr(0, s.size() - 1));
    if (last == 'g') return std::stod(s.substr(0, s.size() - 1)) * kPi / 200.0;
    if (auto d = s.find('d'); d != std::string::npos) {
        double deg = std::stod(s.substr(0, d));
        double m = 0, sec = 0;
        auto q = s.find('\'', d);
        if (q != std::string::npos) m = std::stod(s.substr(d + 1, q - d - 1));
        auto dq = s.find('"', q == std::string::npos ? d : q);
        if (q != std::string::npos && dq != std::string::npos) sec = std::stod(s.substr(q + 1, dq - q - 1));
        return (deg + m / 60.0 + sec / 3600.0) * kPi / 180.0;
    }
    return std::stod(s) * kPi / 180.0;
}

// carlar: CAR, CDR, CADR, CDDR ... (2-4 harf)
V cxr(const V& l, const std::string& path) {
    V p = l;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if (!p) return nullptr;
        if (p->kind != Kind::Cons) fail("bad argument type: consp " + std::string("..."));
        p = *it == 'A' ? p->car : p->cdr;
    }
    return p;
}

std::string sval(Lisp& L, const V& v) { return L.toString(v, false); }

struct FileHolder {
    std::FILE* f;
};
}  // namespace

void Lisp::installBuiltins() {
    Lisp& L0 = *this;
    (void)L0;
    // ---------------- arifmetika
    defineBuiltin("+", [](Lisp& L, A& a) { return arith(L, a, '+'); });
    defineBuiltin("-", [](Lisp& L, A& a) { return arith(L, a, '-'); });
    defineBuiltin("*", [](Lisp& L, A& a) { return arith(L, a, '*'); });
    defineBuiltin("/", [](Lisp& L, A& a) { return arith(L, a, '/'); });
    defineBuiltin("1+", [](Lisp&, A& a) { argc(a, 1, 1); return isInt(a[0]) ? mkInt(a[0]->i + 1) : mkReal(numA(a[0]) + 1); });
    defineBuiltin("1-", [](Lisp&, A& a) { argc(a, 1, 1); return isInt(a[0]) ? mkInt(a[0]->i - 1) : mkReal(numA(a[0]) - 1); });
    defineBuiltin("ABS", [](Lisp&, A& a) { argc(a, 1, 1); return isInt(a[0]) ? mkInt(std::llabs(a[0]->i)) : mkReal(std::abs(numA(a[0]))); });
    auto minmax = [](bool mx) {
        return [mx](Lisp&, A& a) -> V {
            if (a.empty()) return mkInt(0);
            bool allInt = true;
            double best = numA(a[0]);
            for (auto& v : a) {
                const double x = numA(v);
                if (v->kind != Kind::Int) allInt = false;
                if (mx ? x > best : x < best) best = x;
            }
            return allInt ? mkInt(static_cast<long long>(best)) : mkReal(best);
        };
    };
    defineBuiltin("MAX", minmax(true));
    defineBuiltin("MIN", minmax(false));
    defineBuiltin("REM", [](Lisp&, A& a) -> V {
        argc(a, 1, 100);
        bool allInt = true;
        for (auto& v : a) if (!isInt(v)) { numA(v); allInt = false; }
        if (allInt) {
            long long r = a[0]->i;
            for (std::size_t k = 1; k < a.size(); ++k) { if (a[k]->i == 0) fail("divide by zero"); r %= a[k]->i; }
            return mkInt(r);
        }
        double r = numA(a[0]);
        for (std::size_t k = 1; k < a.size(); ++k) r = std::fmod(r, numA(a[k]));
        return mkReal(r);
    });
    defineBuiltin("GCD", [](Lisp&, A& a) { argc(a, 2, 2); return mkInt(std::gcd(static_cast<long long>(numA(a[0])), static_cast<long long>(numA(a[1])))); });
    defineBuiltin("SQRT", [](Lisp&, A& a) { argc(a, 1, 1); const double x = numA(a[0]); if (x < 0) fail("function undefined for argument: " + std::format("{}", x)); return mkReal(std::sqrt(x)); });
    defineBuiltin("EXPT", [](Lisp&, A& a) -> V {
        argc(a, 2, 2);
        if (isInt(a[0]) && isInt(a[1]) && a[1]->i >= 0) {
            long long r = 1;
            for (long long k = 0; k < a[1]->i; ++k) r *= a[0]->i;
            return mkInt(r);
        }
        return mkReal(std::pow(numA(a[0]), numA(a[1])));
    });
    defineBuiltin("EXP", [](Lisp&, A& a) { argc(a, 1, 1); return mkReal(std::exp(numA(a[0]))); });
    defineBuiltin("LOG", [](Lisp&, A& a) { argc(a, 1, 1); const double x = numA(a[0]); if (x <= 0) fail("function undefined for argument"); return mkReal(std::log(x)); });
    defineBuiltin("SIN", [](Lisp&, A& a) { argc(a, 1, 1); return mkReal(std::sin(numA(a[0]))); });
    defineBuiltin("COS", [](Lisp&, A& a) { argc(a, 1, 1); return mkReal(std::cos(numA(a[0]))); });
    defineBuiltin("ATAN", [](Lisp&, A& a) { argc(a, 1, 2); return mkReal(a.size() == 2 ? std::atan2(numA(a[0]), numA(a[1])) : std::atan(numA(a[0]))); });
    defineBuiltin("FIX", [](Lisp&, A& a) { argc(a, 1, 1); return mkInt(static_cast<long long>(std::trunc(numA(a[0])))); });
    defineBuiltin("FLOAT", [](Lisp&, A& a) { argc(a, 1, 1); return mkReal(numA(a[0])); });
    defineBuiltin("~", [](Lisp&, A& a) { argc(a, 1, 1); return mkInt(~static_cast<long long>(numA(a[0]))); });
    defineBuiltin("LOGAND", [](Lisp&, A& a) { long long r = -1; for (auto& v : a) r &= static_cast<long long>(numA(v)); return mkInt(a.empty() ? 0 : r); });
    defineBuiltin("LOGIOR", [](Lisp&, A& a) { long long r = 0; for (auto& v : a) r |= static_cast<long long>(numA(v)); return mkInt(r); });
    defineBuiltin("LSH", [](Lisp&, A& a) {
        argc(a, 1, 2);
        const long long x = static_cast<long long>(numA(a[0])), k = a.size() > 1 ? static_cast<long long>(numA(a[1])) : 0;
        return mkInt(k >= 0 ? (x << k) : static_cast<long long>(static_cast<unsigned long long>(x) >> -k));
    });
    defineBuiltin("BOOLE", [](Lisp&, A& a) {
        argc(a, 2, 100);
        const int op = static_cast<int>(numA(a[0]));
        long long r = static_cast<long long>(numA(a[1]));
        for (std::size_t k = 2; k < a.size(); ++k) {
            const long long b = static_cast<long long>(numA(a[k]));
            long long n = 0;
            for (int bit = 0; bit < 63; ++bit) {
                const int x = (r >> bit) & 1, y = (b >> bit) & 1;
                const int idx = x * 2 + y;  // 1-AND(8)... AutoLISP: 1=AND, 6=XOR, 7=OR, 8=NOR
                const int tbl = ((op >> (3 - idx)) & 1);
                n |= static_cast<long long>(tbl) << bit;
            }
            r = n;
        }
        return mkInt(r);
    });
    defineBuiltin("MINUSP", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, numA(a[0]) < 0); });
    defineBuiltin("ZEROP", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, numA(a[0]) == 0); });
    defineBuiltin("NUMBERP", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, isNum(a[0])); });

    // ---------------- taqqoslash
    auto chain = [](auto pred) {
        return [pred](Lisp& L, A& a) -> V {
            argc(a, 1, 1000);
            for (std::size_t k = 0; k + 1 < a.size(); ++k)
                if (!pred(cmpV(a[k], a[k + 1]))) return nullptr;
            return L.T();
        };
    };
    defineBuiltin("=", chain([](int c) { return c == 0; }));
    defineBuiltin("<", chain([](int c) { return c < 0; }));
    defineBuiltin(">", chain([](int c) { return c > 0; }));
    defineBuiltin("<=", chain([](int c) { return c <= 0; }));
    defineBuiltin(">=", chain([](int c) { return c >= 0; }));
    defineBuiltin("/=", [](Lisp& L, A& a) -> V {
        argc(a, 1, 1000);
        for (std::size_t k = 0; k + 1 < a.size(); ++k)
            if (cmpV(a[k], a[k + 1]) == 0) return nullptr;
        return L.T();
    });
    defineBuiltin("EQ", [](Lisp& L, A& a) { argc(a, 2, 2); return boolV(L, eqV(a[0], a[1])); });
    defineBuiltin("EQUAL", [](Lisp& L, A& a) { argc(a, 2, 3); return boolV(L, equalV(a[0], a[1], a.size() > 2 ? numA(a[2]) : 0)); });
    defineBuiltin("NOT", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, !a[0]); });
    defineBuiltin("NULL", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, !a[0]); });
    defineBuiltin("ATOM", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, !a[0] || a[0]->kind != Kind::Cons); });
    defineBuiltin("LISTP", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, !a[0] || a[0]->kind == Kind::Cons); });
    defineBuiltin("VL-CONSP", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, a[0] && a[0]->kind == Kind::Cons); });
    defineBuiltin("VL-SYMBOLP", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, a[0] && a[0]->kind == Kind::Sym); });
    defineBuiltin("BOUNDP", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, a[0] && a[0]->kind == Kind::Sym && L.getGlobal(a[0]->s)); });
    defineBuiltin("TYPE", [](Lisp& L, A& a) -> V {
        argc(a, 1, 1);
        if (!a[0]) return nullptr;
        static const char* names[] = {"INT", "REAL", "STR", "SYM", "LIST", "ENAME", "PICKSET", "SUBR", "USUBR", "FILE", "VARIANT"};
        return L.sym(names[static_cast<int>(a[0]->kind)]);
    });

    // ---------------- ro'yxatlar
    for (const char* p : {"A", "D", "AA", "AD", "DA", "DD", "AAA", "AAD", "ADA", "ADD", "DAA", "DAD", "DDA", "DDD", "ADDD", "DDDD"}) {
        const std::string path = p;
        defineBuiltin("C" + path + "R", [path](Lisp&, A& a) { argc(a, 1, 1); return cxr(a[0], path); });
    }
    defineBuiltin("CONS", [](Lisp&, A& a) { argc(a, 2, 2); return cons(a[0], a[1]); });
    defineBuiltin("LIST", [](Lisp&, A& a) { return fromVector(a); });
    defineBuiltin("VL-LIST*", [](Lisp&, A& a) -> V {
        argc(a, 1, 1000);
        std::vector<V> h(a.begin(), a.end() - 1);
        return fromVector(h, a.back());
    });
    defineBuiltin("APPEND", [](Lisp&, A& a) -> V {
        std::vector<V> all;
        for (auto& l : a) for (auto& x : toVector(l)) all.push_back(x);
        return fromVector(all);
    });
    defineBuiltin("REVERSE", [](Lisp&, A& a) { argc(a, 1, 1); auto v = toVector(a[0]); std::reverse(v.begin(), v.end()); return fromVector(v); });
    defineBuiltin("LENGTH", [](Lisp&, A& a) { argc(a, 1, 1); return mkInt(static_cast<long long>(toVector(a[0]).size())); });
    defineBuiltin("VL-LIST-LENGTH", [](Lisp&, A& a) -> V {
        argc(a, 1, 1);
        long long n = 0;
        V p = a[0];
        for (; p && p->kind == Kind::Cons; p = p->cdr) ++n;
        return p ? nullptr : mkInt(n);
    });
    defineBuiltin("NTH", [](Lisp&, A& a) -> V {
        argc(a, 2, 2);
        long long k = static_cast<long long>(numA(a[0]));
        V p = a[1];
        for (; k > 0 && p; --k) p = p->cdr;
        return p && k == 0 ? p->car : nullptr;
    });
    defineBuiltin("LAST", [](Lisp&, A& a) -> V { argc(a, 1, 1); auto v = toVector(a[0]); return v.empty() ? nullptr : v.back(); });
    defineBuiltin("MEMBER", [](Lisp&, A& a) -> V {
        argc(a, 2, 2);
        for (V p = a[1]; p; p = p->cdr) if (equalV(p->car, a[0])) return p;
        return nullptr;
    });
    defineBuiltin("ASSOC", [](Lisp&, A& a) -> V {
        argc(a, 2, 2);
        for (V p = a[1]; p && p->kind == Kind::Cons; p = p->cdr)
            if (p->car && p->car->kind == Kind::Cons && equalV(p->car->car, a[0])) return p->car;
        return nullptr;
    });
    defineBuiltin("SUBST", [](Lisp&, A& a) -> V {
        argc(a, 3, 3);
        std::function<V(const V&)> rec = [&](const V& x) -> V {
            if (equalV(x, a[1])) return a[0];
            if (x && x->kind == Kind::Cons) return cons(rec(x->car), rec(x->cdr));
            return x;
        };
        return rec(a[2]);
    });
    defineBuiltin("APPLY", [](Lisp& L, A& a) { argc(a, 2, 2); auto args = toVector(a[1]); return L.apply(a[0], args); });
    defineBuiltin("MAPCAR", [](Lisp& L, A& a) -> V {
        argc(a, 2, 100);
        std::vector<std::vector<V>> ls;
        std::size_t n = SIZE_MAX;
        for (std::size_t k = 1; k < a.size(); ++k) { ls.push_back(toVector(a[k])); n = std::min(n, ls.back().size()); }
        std::vector<V> out;
        for (std::size_t i = 0; i < n; ++i) {
            std::vector<V> args;
            for (auto& l : ls) args.push_back(l[i]);
            out.push_back(L.apply(a[0], args));
        }
        return fromVector(out);
    });
    auto pred1 = [](Lisp& L, const V& f, const V& x) { std::vector<V> args{x}; return L.apply(f, args) != nullptr; };
    defineBuiltin("VL-REMOVE", [](Lisp&, A& a) { argc(a, 2, 2); std::vector<V> o; for (auto& x : toVector(a[1])) if (!equalV(x, a[0])) o.push_back(x); return fromVector(o); });
    defineBuiltin("VL-REMOVE-IF", [pred1](Lisp& L, A& a) { argc(a, 2, 2); std::vector<V> o; for (auto& x : toVector(a[1])) if (!pred1(L, a[0], x)) o.push_back(x); return fromVector(o); });
    defineBuiltin("VL-REMOVE-IF-NOT", [pred1](Lisp& L, A& a) { argc(a, 2, 2); std::vector<V> o; for (auto& x : toVector(a[1])) if (pred1(L, a[0], x)) o.push_back(x); return fromVector(o); });
    defineBuiltin("VL-MEMBER-IF", [pred1](Lisp& L, A& a) -> V { argc(a, 2, 2); for (V p = a[1]; p; p = p->cdr) if (pred1(L, a[0], p->car)) return p; return nullptr; });
    defineBuiltin("VL-MEMBER-IF-NOT", [pred1](Lisp& L, A& a) -> V { argc(a, 2, 2); for (V p = a[1]; p; p = p->cdr) if (!pred1(L, a[0], p->car)) return p; return nullptr; });
    defineBuiltin("VL-POSITION", [](Lisp&, A& a) -> V {
        argc(a, 2, 2);
        long long k = 0;
        for (V p = a[1]; p; p = p->cdr, ++k) if (equalV(p->car, a[0])) return mkInt(k);
        return nullptr;
    });
    defineBuiltin("VL-SOME", [](Lisp& L, A& a) -> V {
        argc(a, 2, 100);
        std::vector<std::vector<V>> ls;
        std::size_t n = SIZE_MAX;
        for (std::size_t k = 1; k < a.size(); ++k) { ls.push_back(toVector(a[k])); n = std::min(n, ls.back().size()); }
        for (std::size_t i = 0; i < n; ++i) {
            std::vector<V> args;
            for (auto& l : ls) args.push_back(l[i]);
            if (V r = L.apply(a[0], args)) return r;
        }
        return nullptr;
    });
    defineBuiltin("VL-EVERY", [](Lisp& L, A& a) -> V {
        argc(a, 2, 100);
        std::vector<std::vector<V>> ls;
        std::size_t n = SIZE_MAX;
        for (std::size_t k = 1; k < a.size(); ++k) { ls.push_back(toVector(a[k])); n = std::min(n, ls.back().size()); }
        for (std::size_t i = 0; i < n; ++i) {
            std::vector<V> args;
            for (auto& l : ls) args.push_back(l[i]);
            if (!L.apply(a[0], args)) return nullptr;
        }
        return L.T();
    });
    defineBuiltin("VL-SORT", [](Lisp& L, A& a) {
        argc(a, 2, 2);
        auto v = toVector(a[0]);
        std::stable_sort(v.begin(), v.end(), [&](const V& x, const V& y) { std::vector<V> args{x, y}; return L.apply(a[1], args) != nullptr; });
        // vl-sort takrorlangan (equal) elementlarni olib tashlaydi
        std::vector<V> o;
        for (auto& x : v) if (o.empty() || !equalV(o.back(), x) || !isNum(x)) o.push_back(x);
        return fromVector(o);
    });
    defineBuiltin("VL-SORT-I", [](Lisp& L, A& a) {
        argc(a, 2, 2);
        auto v = toVector(a[0]);
        std::vector<long long> idx(v.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::stable_sort(idx.begin(), idx.end(), [&](long long x, long long y) { std::vector<V> args{v[x], v[y]}; return L.apply(a[1], args) != nullptr; });
        std::vector<V> o;
        for (auto k : idx) o.push_back(mkInt(k));
        return fromVector(o);
    });
    defineBuiltin("ACAD_STRLSORT", [](Lisp&, A& a) {
        argc(a, 1, 1);
        auto v = toVector(a[0]);
        std::stable_sort(v.begin(), v.end(), [](const V& x, const V& y) { return str(x, "") < str(y, ""); });
        return fromVector(v);
    });

    // ---------------- matnlar
    defineBuiltin("STRCAT", [](Lisp&, A& a) { std::string s; for (auto& v : a) s += str(v, "strcat"); return mkStr(s); });
    defineBuiltin("STRLEN", [](Lisp&, A& a) { long long n = 0; for (auto& v : a) n += static_cast<long long>(str(v, "strlen").size()); return mkInt(n); });
    defineBuiltin("SUBSTR", [](Lisp&, A& a) -> V {
        argc(a, 2, 3);
        const std::string s = str(a[0], "substr");
        long long st = static_cast<long long>(numA(a[1])) - 1;
        if (st < 0) fail("bad argument value: positive " + std::to_string(st + 1));
        if (st >= static_cast<long long>(s.size())) return mkStr("");
        const long long ln = a.size() > 2 ? static_cast<long long>(numA(a[2])) : static_cast<long long>(s.size());
        return mkStr(s.substr(static_cast<std::size_t>(st), static_cast<std::size_t>(std::max(0LL, ln))));
    });
    defineBuiltin("STRCASE", [](Lisp&, A& a) { argc(a, 1, 2); const std::string s = str(a[0], "strcase"); return mkStr(a.size() > 1 && a[1] ? lower(s) : upper(s)); });
    defineBuiltin("ATOI", [](Lisp&, A& a) { argc(a, 1, 1); return mkInt(std::atoll(str(a[0], "atoi").c_str())); });
    defineBuiltin("ATOF", [](Lisp&, A& a) { argc(a, 1, 1); return mkReal(std::atof(str(a[0], "atof").c_str())); });
    defineBuiltin("ITOA", [](Lisp&, A& a) { argc(a, 1, 1); if (!isInt(a[0])) fail("bad argument type: fixnump"); return mkStr(std::to_string(a[0]->i)); });
    defineBuiltin("RTOS", [](Lisp& L, A& a) {
        argc(a, 1, 3);
        int prec = 4;
        if (L.host()) if (V p = L.host()->getVar(L, "LUPREC"); isInt(p)) prec = static_cast<int>(p->i);
        return mkStr(rtos(numA(a[0]), a.size() > 1 && a[1] ? static_cast<int>(numA(a[1])) : 2, a.size() > 2 && a[2] ? static_cast<int>(numA(a[2])) : prec));
    });
    defineBuiltin("ANGTOS", [](Lisp&, A& a) {
        argc(a, 1, 3);
        return mkStr(angtos(numA(a[0]), a.size() > 1 && a[1] ? static_cast<int>(numA(a[1])) : 0, a.size() > 2 && a[2] ? static_cast<int>(numA(a[2])) : 0));
    });
    defineBuiltin("DISTOF", [](Lisp&, A& a) -> V {
        argc(a, 1, 2);
        const std::string s = str(a[0], "distof");
        char* end = nullptr;
        const double d = std::strtod(s.c_str(), &end);
        if (end == s.c_str()) return nullptr;
        return mkReal(d);
    });
    defineBuiltin("ANGTOF", [](Lisp&, A& a) -> V {
        argc(a, 1, 2);
        try { return mkReal(parseAngle(str(a[0], "angtof"))); } catch (const std::exception&) { return nullptr; }
    });
    defineBuiltin("CHR", [](Lisp&, A& a) { argc(a, 1, 1); return mkStr(std::string(1, static_cast<char>(numA(a[0])))); });
    defineBuiltin("ASCII", [](Lisp&, A& a) { argc(a, 1, 1); const std::string s = str(a[0], "ascii"); return mkInt(s.empty() ? 0 : static_cast<unsigned char>(s[0])); });
    defineBuiltin("READ", [](Lisp& L, A& a) -> V { argc(a, 0, 1); if (a.empty()) return nullptr; auto xs = L.read(str(a[0], "read")); return xs.empty() ? nullptr : xs[0]; });
    defineBuiltin("WCMATCH", [](Lisp& L, A& a) {
        argc(a, 2, 2);
        return boolV(L, lispWcmatch(str(a[0], "wcmatch"), str(a[1], "wcmatch")));
    });
    defineBuiltin("VL-STRING-SEARCH", [](Lisp&, A& a) -> V {
        argc(a, 2, 3);
        const std::string pat = str(a[0], ""), s = str(a[1], "");
        const auto p = s.find(pat, a.size() > 2 ? static_cast<std::size_t>(numA(a[2])) : 0);
        return p == std::string::npos ? nullptr : mkInt(static_cast<long long>(p));
    });
    defineBuiltin("VL-STRING-POSITION", [](Lisp&, A& a) -> V {
        argc(a, 2, 4);
        const char c = static_cast<char>(numA(a[0]));
        const std::string s = str(a[1], "");
        const bool fromEnd = a.size() > 3 && a[3];
        std::size_t p = fromEnd ? s.rfind(c) : s.find(c, a.size() > 2 && a[2] ? static_cast<std::size_t>(numA(a[2])) : 0);
        return p == std::string::npos ? nullptr : mkInt(static_cast<long long>(p));
    });
    defineBuiltin("VL-STRING-SUBST", [](Lisp&, A& a) {
        argc(a, 3, 4);
        const std::string nw = str(a[0], ""), pat = str(a[1], "");
        std::string s = str(a[2], "");
        const auto p = s.find(pat, a.size() > 3 ? static_cast<std::size_t>(numA(a[3])) : 0);
        if (p != std::string::npos && !pat.empty()) s.replace(p, pat.size(), nw);
        return mkStr(s);
    });
    auto trimmer = [](int side) {
        return [side](Lisp&, A& a) {
            argc(a, 2, 2);
            const std::string set = str(a[0], "");
            std::string s = str(a[1], "");
            if (side & 1) { auto b = s.find_first_not_of(set); s = b == std::string::npos ? "" : s.substr(b); }
            if (side & 2) { auto e = s.find_last_not_of(set); s = e == std::string::npos ? "" : s.substr(0, e + 1); }
            return mkStr(s);
        };
    };
    defineBuiltin("VL-STRING-TRIM", trimmer(3));
    defineBuiltin("VL-STRING-LEFT-TRIM", trimmer(1));
    defineBuiltin("VL-STRING-RIGHT-TRIM", trimmer(2));
    defineBuiltin("VL-STRING-TRANSLATE", [](Lisp&, A& a) {
        argc(a, 3, 3);
        const std::string from = str(a[0], ""), to = str(a[1], "");
        std::string s = str(a[2], "");
        for (auto& c : s)
            if (auto p = from.find(c); p != std::string::npos && p < to.size()) c = to[p];
        return mkStr(s);
    });
    defineBuiltin("VL-STRING-ELT", [](Lisp&, A& a) {
        argc(a, 2, 2);
        const std::string s = str(a[0], "");
        const auto k = static_cast<std::size_t>(numA(a[1]));
        if (k >= s.size()) fail("bad argument value");
        return mkInt(static_cast<unsigned char>(s[k]));
    });
    defineBuiltin("VL-STRING->LIST", [](Lisp&, A& a) { argc(a, 1, 1); std::vector<V> o; for (unsigned char c : str(a[0], "")) o.push_back(mkInt(c)); return fromVector(o); });
    defineBuiltin("VL-LIST->STRING", [](Lisp&, A& a) { argc(a, 1, 1); std::string s; for (auto& v : toVector(a[0])) s += static_cast<char>(numA(v)); return mkStr(s); });
    defineBuiltin("VL-PRIN1-TO-STRING", [](Lisp& L, A& a) { argc(a, 1, 1); return mkStr(L.toString(a[0], true)); });
    defineBuiltin("VL-PRINC-TO-STRING", [](Lisp& L, A& a) { argc(a, 1, 1); return mkStr(L.toString(a[0], false)); });
    defineBuiltin("VL-SYMBOL-NAME", [](Lisp&, A& a) { argc(a, 1, 1); if (!a[0] || a[0]->kind != Kind::Sym) fail("bad argument type: symbolp"); return mkStr(a[0]->s); });
    defineBuiltin("VL-SYMBOL-VALUE", [](Lisp& L, A& a) { argc(a, 1, 1); if (!a[0] || a[0]->kind != Kind::Sym) fail("bad argument type: symbolp"); return L.getGlobal(a[0]->s); });
    defineBuiltin("VL-FILENAME-BASE", [](Lisp&, A& a) {
        argc(a, 1, 1);
        std::string s = str(a[0], "");
        if (auto p = s.find_last_of("/\\"); p != std::string::npos) s = s.substr(p + 1);
        if (auto d = s.rfind('.'); d != std::string::npos) s = s.substr(0, d);
        return mkStr(s);
    });
    defineBuiltin("VL-FILENAME-DIRECTORY", [](Lisp&, A& a) {
        argc(a, 1, 1);
        const std::string s = str(a[0], "");
        const auto p = s.find_last_of("/\\");
        return mkStr(p == std::string::npos ? "" : s.substr(0, p));
    });
    defineBuiltin("VL-FILENAME-EXTENSION", [](Lisp&, A& a) -> V {
        argc(a, 1, 1);
        const std::string s = str(a[0], "");
        const auto d = s.rfind('.');
        const auto sl = s.find_last_of("/\\");
        if (d == std::string::npos || (sl != std::string::npos && d < sl)) return nullptr;
        return mkStr(s.substr(d));
    });

    // ---------------- baholash, belgilar
    defineBuiltin("EVAL", [](Lisp& L, A& a) { argc(a, 1, 1); return L.eval(a[0]); });
    defineBuiltin("SET", [](Lisp& L, A& a) {
        argc(a, 2, 2);
        if (!a[0] || a[0]->kind != Kind::Sym) fail("bad argument type: symbolp");
        L.setGlobal(a[0]->s, a[1]);
        return a[1];
    });
    defineBuiltin("ATOMS-FAMILY", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("VL-CATCH-ALL-ERROR-P", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, a[0] && a[0]->kind == Kind::Variant && a[0]->i == 1); });
    defineBuiltin("VL-CATCH-ALL-ERROR-MESSAGE", [](Lisp&, A& a) -> V { argc(a, 1, 1); return a[0] && a[0]->kind == Kind::Variant ? mkStr(a[0]->s) : nullptr; });
    defineBuiltin("EXIT", [](Lisp&, A&) -> V { throw LispExit(); });
    defineBuiltin("QUIT", [](Lisp&, A&) -> V { throw LispExit(); });
    defineBuiltin("VL-EXIT-WITH-ERROR", [](Lisp&, A& a) -> V { argc(a, 1, 1); fail(str(a[0], "")); });
    defineBuiltin("VL-EXIT-WITH-VALUE", [](Lisp&, A& a) -> V { argc(a, 1, 1); return a[0]; });
    defineBuiltin("VL-LOAD-COM", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("VL-DOC-SET", [](Lisp& L, A& a) { argc(a, 2, 2); if (a[0] && a[0]->kind == Kind::Sym) L.setGlobal(a[0]->s, a[1]); return a[1]; });
    defineBuiltin("VL-DOC-REF", [](Lisp& L, A& a) -> V { argc(a, 1, 1); return a[0] && a[0]->kind == Kind::Sym ? L.getGlobal(a[0]->s) : nullptr; });
    defineBuiltin("VL-BB-SET", [](Lisp& L, A& a) { argc(a, 2, 2); if (a[0] && a[0]->kind == Kind::Sym) L.setGlobal(a[0]->s, a[1]); return a[1]; });
    defineBuiltin("VL-BB-REF", [](Lisp& L, A& a) -> V { argc(a, 1, 1); return a[0] && a[0]->kind == Kind::Sym ? L.getGlobal(a[0]->s) : nullptr; });
    defineBuiltin("VL-PROPAGATE", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("VER", [](Lisp&, A&) { return mkStr("Visual LISP 2026 (AsiaCAD)"); });
    defineBuiltin("LOAD", [](Lisp& L, A& a) -> V {
        argc(a, 1, 2);
        try {
            return L.loadFile(str(a[0], "load"));
        } catch (const LispError&) {
            if (a.size() > 1) return a[1];
            throw;
        }
    });
    defineBuiltin("AUTOLOAD", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("AUTOARXLOAD", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("ARX", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("GC", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("MEM", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("TRACE", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("UNTRACE", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("SETFUNHELP", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("VL-ARX-IMPORT", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("VL-VBALOAD", [](Lisp&, A&) -> V { return nullptr; });

    // ---------------- chiqarish, fayllar
    defineBuiltin("PRINC", [](Lisp& L, A& a) -> V {
        argc(a, 0, 2);
        if (a.empty()) return L.sym("");  // (princ) — jim chiqish
        const std::string s = L.toString(a[0], false);
        if (a.size() > 1 && a[1] && a[1]->kind == Kind::File) { std::fputs(s.c_str(), a[1]->file.get()); }
        else L.out() << s;
        return a[0];
    });
    defineBuiltin("PRIN1", [](Lisp& L, A& a) -> V {
        argc(a, 0, 2);
        if (a.empty()) return L.sym("");
        const std::string s = L.toString(a[0], true);
        if (a.size() > 1 && a[1] && a[1]->kind == Kind::File) std::fputs(s.c_str(), a[1]->file.get());
        else L.out() << s;
        return a[0];
    });
    defineBuiltin("PRINT", [](Lisp& L, A& a) -> V {
        argc(a, 0, 2);
        if (a.empty()) return L.sym("");
        const std::string s = "\n" + L.toString(a[0], true) + " ";
        if (a.size() > 1 && a[1] && a[1]->kind == Kind::File) std::fputs(s.c_str(), a[1]->file.get());
        else L.out() << s;
        return a[0];
    });
    defineBuiltin("PROMPT", [](Lisp& L, A& a) -> V { argc(a, 1, 1); L.out() << str(a[0], "prompt"); return nullptr; });
    defineBuiltin("TERPRI", [](Lisp& L, A&) -> V { L.out() << "\n"; return nullptr; });
    defineBuiltin("ALERT", [](Lisp& L, A& a) -> V {
        argc(a, 1, 1);
        if (L.host()) L.host()->alert(str(a[0], "alert"));
        else L.out() << str(a[0], "alert") << "\n";
        return nullptr;
    });
    defineBuiltin("OPEN", [](Lisp& L, A& a) -> V {
        argc(a, 2, 3);
        std::string mode = lower(str(a[1], "open"));
        std::FILE* f = std::fopen(str(a[0], "open").c_str(), mode == "w" ? "wb" : mode == "a" ? "ab" : "rb");
        if (!f) return nullptr;
        auto v = std::make_shared<Value>(Kind::File);
        v->file = std::shared_ptr<std::FILE>(f, [](std::FILE* p) { if (p) std::fclose(p); });
        (void)L;
        return v;
    });
    defineBuiltin("CLOSE", [](Lisp&, A& a) -> V {
        argc(a, 1, 1);
        if (a[0] && a[0]->kind == Kind::File && a[0]->file) { std::fflush(a[0]->file.get()); a[0]->file.reset(); }
        return nullptr;
    });
    defineBuiltin("READ-LINE", [](Lisp&, A& a) -> V {
        argc(a, 0, 1);
        if (a.empty() || !a[0] || a[0]->kind != Kind::File || !a[0]->file) return nullptr;
        std::string s;
        int c;
        bool any = false;
        while ((c = std::fgetc(a[0]->file.get())) != EOF) {
            any = true;
            if (c == '\n') break;
            if (c != '\r') s += static_cast<char>(c);
        }
        return any ? mkStr(s) : nullptr;
    });
    defineBuiltin("READ-CHAR", [](Lisp&, A& a) -> V {
        argc(a, 0, 1);
        if (a.empty() || !a[0] || a[0]->kind != Kind::File || !a[0]->file) return nullptr;
        const int c = std::fgetc(a[0]->file.get());
        return c == EOF ? nullptr : mkInt(c);
    });
    defineBuiltin("WRITE-LINE", [](Lisp& L, A& a) -> V {
        argc(a, 1, 2);
        const std::string s = str(a[0], "write-line");
        if (a.size() > 1 && a[1] && a[1]->kind == Kind::File && a[1]->file) { std::fputs((s + "\n").c_str(), a[1]->file.get()); }
        else L.out() << s << "\n";
        return a[0];
    });
    defineBuiltin("WRITE-CHAR", [](Lisp& L, A& a) -> V {
        argc(a, 1, 2);
        const char c = static_cast<char>(numA(a[0]));
        if (a.size() > 1 && a[1] && a[1]->kind == Kind::File && a[1]->file) std::fputc(c, a[1]->file.get());
        else L.out() << c;
        return a[0];
    });
    defineBuiltin("FINDFILE", [](Lisp& L, A& a) -> V {
        argc(a, 1, 1);
        const std::string n = str(a[0], "findfile");
        if (L.host()) { auto f = L.host()->findFile(n); return f.empty() ? nullptr : mkStr(f); }
        std::ifstream t(n);
        return t ? mkStr(n) : nullptr;
    });
    defineBuiltin("VL-FILE-DELETE", [](Lisp& L, A& a) { argc(a, 1, 1); return boolV(L, std::remove(str(a[0], "").c_str()) == 0); });
    defineBuiltin("VL-FILE-RENAME", [](Lisp& L, A& a) { argc(a, 2, 2); return boolV(L, std::rename(str(a[0], "").c_str(), str(a[1], "").c_str()) == 0); });
    defineBuiltin("VL-FILE-COPY", [](Lisp&, A& a) -> V {
        argc(a, 2, 3);
        std::ifstream in(str(a[0], ""), std::ios::binary);
        if (!in) return nullptr;
        std::ofstream o(str(a[1], ""), std::ios::binary | (a.size() > 2 && a[2] ? std::ios::app : std::ios::trunc));
        o << in.rdbuf();
        return mkInt(static_cast<long long>(o.tellp()));
    });
    defineBuiltin("GETENV", [](Lisp&, A& a) -> V { argc(a, 1, 1); const char* e = std::getenv(str(a[0], "").c_str()); return e ? mkStr(e) : nullptr; });
    defineBuiltin("SETENV", [](Lisp&, A& a) { argc(a, 2, 2); return a[1]; });

    // ---------------- geometriya
    defineBuiltin("POLAR", [](Lisp& L, A& a) {
        argc(a, 3, 3);
        double x, y, z;
        if (!L.pointOf(a[0], x, y, z)) fail("bad argument type: 2D/3D point");
        const double ang = numA(a[1]), d = numA(a[2]);
        const bool is3 = toVector(a[0]).size() > 2;
        return is3 ? L.mkPoint(x + d * std::cos(ang), y + d * std::sin(ang), z) : list({mkReal(x + d * std::cos(ang)), mkReal(y + d * std::sin(ang))});
    });
    defineBuiltin("DISTANCE", [](Lisp& L, A& a) {
        argc(a, 2, 2);
        double x1, y1, z1, x2, y2, z2;
        if (!L.pointOf(a[0], x1, y1, z1) || !L.pointOf(a[1], x2, y2, z2)) fail("bad argument type: 2D/3D point");
        const bool flat = toVector(a[0]).size() < 3 || toVector(a[1]).size() < 3;
        return mkReal(std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1) + (flat ? 0 : (z2 - z1) * (z2 - z1))));
    });
    defineBuiltin("ANGLE", [](Lisp& L, A& a) {
        argc(a, 2, 2);
        double x1, y1, z1, x2, y2, z2;
        if (!L.pointOf(a[0], x1, y1, z1) || !L.pointOf(a[1], x2, y2, z2)) fail("bad argument type: 2D/3D point");
        double r = std::atan2(y2 - y1, x2 - x1);
        if (r < 0) r += 2 * kPi;
        return mkReal(r);
    });
    defineBuiltin("INTERS", [](Lisp& L, A& a) -> V {
        argc(a, 4, 5);
        double p[4][3];
        for (int k = 0; k < 4; ++k)
            if (!L.pointOf(a[k], p[k][0], p[k][1], p[k][2])) fail("bad argument type: 2D/3D point");
        const bool onSeg = a.size() < 5 || a[4];
        const double d1x = p[1][0] - p[0][0], d1y = p[1][1] - p[0][1], d2x = p[3][0] - p[2][0], d2y = p[3][1] - p[2][1];
        const double den = d1x * d2y - d1y * d2x;
        if (std::abs(den) < 1e-12) return nullptr;
        const double t = ((p[2][0] - p[0][0]) * d2y - (p[2][1] - p[0][1]) * d2x) / den;
        const double u = ((p[2][0] - p[0][0]) * d1y - (p[2][1] - p[0][1]) * d1x) / den;
        if (onSeg && (t < -1e-12 || t > 1 + 1e-12 || u < -1e-12 || u > 1 + 1e-12)) return nullptr;
        return L.mkPoint(p[0][0] + t * d1x, p[0][1] + t * d1y, p[0][2] + t * (p[1][2] - p[0][2]));
    });
    defineBuiltin("TRANS", [](Lisp&, A& a) { argc(a, 3, 4); return a[0]; });  // WCS = UCS (AsiaCAD'da UCS yo'q)
    defineBuiltin("OSNAP", [](Lisp&, A& a) { argc(a, 2, 2); return a[0]; });
    defineBuiltin("TEXTBOX", [](Lisp& L, A& a) -> V {
        argc(a, 1, 1);
        std::string s;
        double h = 2.5;
        for (auto& g : toVector(a[0])) {
            if (!g || g->kind != Kind::Cons || !isInt(g->car)) continue;
            if (g->car->i == 1 && g->cdr && g->cdr->kind == Kind::Str) s = g->cdr->s;
            if (g->car->i == 40 && isNum(g->cdr)) h = numA(g->cdr);
        }
        return list({L.mkPoint(0, 0, 0), L.mkPoint(h * 0.8 * static_cast<double>(s.size()), h, 0)});
    });
}

}  // namespace asiacad::lisp
