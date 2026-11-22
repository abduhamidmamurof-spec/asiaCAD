// AsiaCAD AutoLISP: chizma bilan bog'liq funksiyalar (command, get*, ent*, ss*, tbl*, getvar/setvar).
// Hammasi LispHost orqali — yadro Document'ni bilmaydi.
#include <asiacad/lisp/lisp.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>

namespace asiacad::lisp {

namespace {
constexpr double kPi = 3.14159265358979323846;
[[noreturn]] void fail(const std::string& m) { throw LispError(m); }
using A = std::vector<V>;
void argc(const A& a, std::size_t mn, std::size_t mx) {
    if (a.size() < mn) fail("too few arguments");
    if (a.size() > mx) fail("too many arguments");
}
LispHost& hostOf(Lisp& L) {
    if (!L.host()) fail("no drawing (host) available");
    return *L.host();
}
std::string upper(std::string s) { for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }

// (command ...) argumentlarini buyruq satri bo'laklariga aylantirish
std::string tokenOf(Lisp& L, const V& v) {
    if (!v) return "";  // nil — Enter
    switch (v->kind) {
    case Kind::Str: return v->s;
    case Kind::Int: return std::to_string(v->i);
    case Kind::Real: return std::format("{:.10g}", v->r);
    case Kind::Sym: return v->s == "T" ? "" : v->s;
    case Kind::Ename: return std::to_string(v->ename);
    case Kind::PickSet: {
        std::string s;
        for (auto id : *v->ss) s += (s.empty() ? "" : ",") + std::to_string(id);
        return s;
    }
    case Kind::Cons: {
        double x, y, z;
        if (L.pointOf(v, x, y, z)) return std::format("{:.10g},{:.10g},{:.10g}", x, y, z);
        // (ename pt) — entsel natijasi
        if (v->car && v->car->kind == Kind::Ename) return std::to_string(v->car->ename);
        return L.toString(v, false);
    }
    default: return L.toString(v, false);
    }
}

// get* uchun kirish: initget bitlari — 1 nil taqiqlangan, 2 nol, 4 manfiy
struct InputCtx {
    Lisp& L;
    std::string prompt;
    bool get(std::string& s) {
        auto& h = hostOf(L);
        if (!prompt.empty()) L.out() << prompt;
        const bool ok = h.nextInput(s, prompt);
        if (ok && !prompt.empty()) L.out() << s << "\n";
        else if (!prompt.empty()) L.out() << "\n";
        return ok;
    }
};
V keywordMatch(Lisp& L, const std::string& s) {
    const std::string u = upper(s);
    for (const auto& kw : L.initKeywords) {
        // AutoCAD: katta harfli qismi — qisqartma ("eXit" -> X)
        std::string abbr;
        for (char c : kw) if (std::isupper(static_cast<unsigned char>(c))) abbr += c;
        if (upper(kw) == u || (!abbr.empty() && abbr == u) || (u.size() >= 1 && upper(kw).rfind(u, 0) == 0)) return Lisp::mkStr(kw);
    }
    return nullptr;
}
V promptArg(const A& a, std::size_t idx) { return idx < a.size() ? a[idx] : nullptr; }
std::string promptStr(const V& v) { return v && v->kind == Kind::Str ? v->s : std::string(); }

// get* umumiy: o'qib, tahlil qiladi; kalit so'zlar initget bilan
template <class Parse>
V getGeneric(Lisp& L, const std::string& prompt, Parse parse) {
    InputCtx in{L, prompt};
    const int bits = L.initBits;
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::string s;
        if (!in.get(s) || s.empty()) {
            L.initBits = 0; L.initKeywords.clear();
            if (bits & 1) fail("input required (initget 1)");
            return nullptr;
        }
        if (V kw = keywordMatch(L, s)) { L.initBits = 0; L.initKeywords.clear(); return kw; }
        V v = parse(s);
        if (v) {
            if (Lisp::isNum(v)) {
                const double x = Lisp::num(v, "");
                if ((bits & 2) && x == 0) continue;
                if ((bits & 4) && x < 0) continue;
            }
            L.initBits = 0; L.initKeywords.clear();
            return v;
        }
    }
    L.initBits = 0; L.initKeywords.clear();
    fail("invalid input");
}
bool parsePt(Lisp& L, const std::string& s, const V& base, double& x, double& y, double& z) {
    std::string t = s;
    bool rel = false;
    if (!t.empty() && t[0] == '@') { rel = true; t.erase(0, 1); }
    double bx = 0, by = 0, bz = 0;
    if (rel && base) L.pointOf(base, bx, by, bz);
    // qutb: @d<ang
    if (auto lt = t.find('<'); lt != std::string::npos) {
        const double d = std::stod(t.substr(0, lt)), ang = std::stod(t.substr(lt + 1)) * kPi / 180.0;
        x = bx + d * std::cos(ang); y = by + d * std::sin(ang); z = bz;
        return true;
    }
    if (!L.pointOf(Lisp::mkStr(t), x, y, z)) return false;
    x += bx; y += by; z += bz;
    return true;
}
}  // namespace

void Lisp::installHostBuiltins() {
    // ---------------- buyruqlar
    auto cmd = [](Lisp& L, A& a) -> V {
        std::vector<std::string> toks;
        for (auto& v : a) {
            if (v && v->kind == Kind::Str && (upper(v->s) == "PAUSE" || v->s == "\\")) {  // pause — foydalanuvchi kiritishi
                std::string s;
                if (hostOf(L).nextInput(s, "")) toks.push_back(s);
                continue;
            }
            toks.push_back(tokenOf(L, v));
        }
        hostOf(L).command(toks);
        return nullptr;
    };
    defineBuiltin("COMMAND", cmd);
    defineBuiltin("COMMAND-S", cmd);
    defineBuiltin("VL-CMDF", [cmd](Lisp& L, A& a) { cmd(L, a); return L.T(); });

    // ---------------- kiritish
    defineBuiltin("INITGET", [](Lisp& L, A& a) -> V {
        argc(a, 0, 2);
        L.initBits = 0;
        L.initKeywords.clear();
        std::size_t k = 0;
        if (k < a.size() && isNum(a[k])) L.initBits = static_cast<int>(num(a[k++], ""));
        if (k < a.size() && a[k] && a[k]->kind == Kind::Str) {
            std::string kws = a[k]->s;
            if (auto u = kws.find('_'); u != std::string::npos) kws = kws.substr(0, u);  // global kalitlar
            std::size_t b = 0;
            while (b < kws.size()) {
                auto e = kws.find(' ', b);
                if (e == std::string::npos) e = kws.size();
                if (e > b) L.initKeywords.push_back(kws.substr(b, e - b));
                b = e + 1;
            }
        }
        return nullptr;
    });
    defineBuiltin("GETPOINT", [](Lisp& L, A& a) -> V {
        argc(a, 0, 2);
        V base = a.size() > 0 && a[0] && a[0]->kind == Kind::Cons ? a[0] : nullptr;
        const std::string pr = promptStr(base ? promptArg(a, 1) : promptArg(a, 0));
        return getGeneric(L, pr, [&](const std::string& s) -> V {
            double x, y, z;
            return parsePt(L, s, base, x, y, z) ? L.mkPoint(x, y, z) : nullptr;
        });
    });
    defineBuiltin("GETCORNER", [](Lisp& L, A& a) -> V {
        argc(a, 1, 2);
        V base = a[0];
        return getGeneric(L, promptStr(promptArg(a, 1)), [&](const std::string& s) -> V {
            double x, y, z;
            return parsePt(L, s, base, x, y, z) ? L.mkPoint(x, y, z) : nullptr;
        });
    });
    defineBuiltin("GETDIST", [](Lisp& L, A& a) -> V {
        argc(a, 0, 2);
        V base = a.size() > 0 && a[0] && a[0]->kind == Kind::Cons ? a[0] : nullptr;
        return getGeneric(L, promptStr(base ? promptArg(a, 1) : promptArg(a, 0)), [&](const std::string& s) -> V {
            char* end = nullptr;
            const double d = std::strtod(s.c_str(), &end);
            if (end && *end == 0 && end != s.c_str()) return mkReal(d);
            double x, y, z, bx, by, bz;
            if (base && parsePt(L, s, base, x, y, z) && L.pointOf(base, bx, by, bz)) return mkReal(std::hypot(x - bx, y - by));
            return nullptr;
        });
    });
    auto getAng = [](Lisp& L, A& a) -> V {
        argc(a, 0, 2);
        V base = a.size() > 0 && a[0] && a[0]->kind == Kind::Cons ? a[0] : nullptr;
        return getGeneric(L, promptStr(base ? promptArg(a, 1) : promptArg(a, 0)), [&](const std::string& s) -> V {
            double x, y, z, bx, by, bz;
            if (base && s.find(',') != std::string::npos && parsePt(L, s, base, x, y, z) && L.pointOf(base, bx, by, bz)) {
                double r = std::atan2(y - by, x - bx);
                if (r < 0) r += 2 * kPi;
                return mkReal(r);
            }
            char* end = nullptr;
            const double d = std::strtod(s.c_str(), &end);
            if (end == s.c_str()) return nullptr;
            if (*end == 'r' || *end == 'R') return mkReal(d);
            return mkReal(d * kPi / 180.0);
        });
    };
    defineBuiltin("GETANGLE", getAng);
    defineBuiltin("GETORIENT", getAng);
    defineBuiltin("GETREAL", [](Lisp& L, A& a) -> V {
        argc(a, 0, 1);
        return getGeneric(L, promptStr(promptArg(a, 0)), [](const std::string& s) -> V {
            char* end = nullptr;
            const double d = std::strtod(s.c_str(), &end);
            return end && *end == 0 && end != s.c_str() ? mkReal(d) : nullptr;
        });
    });
    defineBuiltin("GETINT", [](Lisp& L, A& a) -> V {
        argc(a, 0, 1);
        return getGeneric(L, promptStr(promptArg(a, 0)), [](const std::string& s) -> V {
            char* end = nullptr;
            const long long d = std::strtoll(s.c_str(), &end, 10);
            return end && *end == 0 && end != s.c_str() ? mkInt(d) : nullptr;
        });
    });
    defineBuiltin("GETSTRING", [](Lisp& L, A& a) -> V {
        argc(a, 0, 2);
        const std::string pr = promptStr(a.size() > 1 ? a[1] : (a.size() == 1 && a[0] && a[0]->kind == Kind::Str ? a[0] : nullptr));
        InputCtx in{L, pr};
        std::string s;
        in.get(s);
        return mkStr(s);
    });
    defineBuiltin("GETKWORD", [](Lisp& L, A& a) -> V {
        argc(a, 0, 1);
        return getGeneric(L, promptStr(promptArg(a, 0)), [](const std::string&) -> V { return nullptr; });
    });
    defineBuiltin("GETFILED", [](Lisp& L, A& a) -> V {
        argc(a, 4, 4);
        InputCtx in{L, promptStr(a[0])};
        std::string s;
        return in.get(s) && !s.empty() ? mkStr(s) : nullptr;
    });
    defineBuiltin("ENTSEL", [](Lisp& L, A& a) -> V {
        argc(a, 0, 1);
        InputCtx in{L, promptStr(promptArg(a, 0))};
        std::string s;
        if (!in.get(s) || s.empty()) return nullptr;
        if (s[0] == '#') s.erase(0, 1);
        char* end = nullptr;
        const unsigned long long id = std::strtoull(s.c_str(), &end, 10);
        if (end == s.c_str()) return nullptr;
        return list({L.mkEname(id), L.mkPoint(0, 0, 0)});
    });
    defineBuiltin("NENTSEL", [](Lisp& L, A& a) -> V {
        auto f = L.getGlobal("ENTSEL");
        return L.apply(f, a);
    });

    // ---------------- tizim o'zgaruvchilari
    defineBuiltin("GETVAR", [](Lisp& L, A& a) { argc(a, 1, 1); return hostOf(L).getVar(L, upper(str(a[0], "getvar"))); });
    defineBuiltin("SETVAR", [](Lisp& L, A& a) {
        argc(a, 2, 2);
        const std::string n = upper(str(a[0], "setvar"));
        if (!hostOf(L).setVar(L, n, a[1])) fail("setvar rejected: " + n);
        return a[1];
    });

    // ---------------- obyektlar
    defineBuiltin("ENTLAST", [](Lisp& L, A&) -> V { const auto id = hostOf(L).entLast(); return id ? L.mkEname(id) : nullptr; });
    defineBuiltin("ENTNEXT", [](Lisp& L, A& a) -> V {
        argc(a, 0, 1);
        const std::uint64_t after = a.empty() || !a[0] ? 0 : a[0]->ename;
        const auto id = hostOf(L).entNext(after);
        return id ? L.mkEname(id) : nullptr;
    });
    defineBuiltin("ENTGET", [](Lisp& L, A& a) -> V {
        argc(a, 1, 2);
        if (!a[0] || a[0]->kind != Kind::Ename) fail("bad argument type: lentityp " + L.toString(a[0], true));
        return hostOf(L).entGet(L, a[0]->ename);
    });
    auto mk = [](Lisp& L, A& a) -> V {
        argc(a, 0, 1);
        if (a.empty()) return nullptr;
        const auto id = hostOf(L).entMake(L, a[0]);
        return id ? L.mkEname(id) : nullptr;
    };
    defineBuiltin("ENTMAKE", [mk](Lisp& L, A& a) -> V { return mk(L, a) ? (a.empty() ? nullptr : a[0]) : nullptr; });
    defineBuiltin("ENTMAKEX", mk);
    defineBuiltin("ENTMOD", [](Lisp& L, A& a) -> V { argc(a, 1, 1); return hostOf(L).entMod(L, a[0]) ? a[0] : nullptr; });
    defineBuiltin("ENTDEL", [](Lisp& L, A& a) -> V {
        argc(a, 1, 1);
        if (!a[0] || a[0]->kind != Kind::Ename) fail("bad argument type: lentityp");
        return hostOf(L).entDel(a[0]->ename) ? a[0] : nullptr;
    });
    defineBuiltin("ENTUPD", [](Lisp&, A& a) -> V { argc(a, 1, 1); return a[0]; });
    defineBuiltin("HANDENT", [](Lisp& L, A& a) -> V {
        argc(a, 1, 1);
        const unsigned long long id = std::strtoull(str(a[0], "handent").c_str(), nullptr, 16);
        return id && hostOf(L).entGet(L, id) ? L.mkEname(id) : nullptr;
    });
    defineBuiltin("REDRAW", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("GRDRAW", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("GRVECS", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("GRTEXT", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("GRCLEAR", [](Lisp&, A&) -> V { return nullptr; });

    // ---------------- tanlov to'plamlari
    defineBuiltin("SSGET", [](Lisp& L, A& a) -> V {
        argc(a, 0, 4);
        std::string mode;
        std::vector<V> pts;
        V filter;
        for (auto& v : a) {
            if (!v) continue;
            if (v->kind == Kind::Str && mode.empty() && pts.empty()) mode = upper(v->s);
            else if (v->kind == Kind::Cons && v->car && v->car->kind == Kind::Cons && Lisp::isNum(v->car->car) && !Lisp::isNum(v->car->cdr ? v->car->cdr : nullptr) && v->car->cdr && v->car->cdr->kind != Kind::Cons)
                filter = v;  // ((0 . "LINE") (8 . "A"))
            else if (v->kind == Kind::Cons && v->car && v->car->kind == Kind::Cons && v->car->car && v->car->car->kind == Kind::Int &&
                     (v->car->cdr && v->car->cdr->kind == Kind::Str))
                filter = v;
            else if (v->kind == Kind::Cons) pts.push_back(v);
        }
        if (!mode.empty() && mode[0] == '_') mode.erase(0, 1);
        if (mode.size() > 1 && mode[0] == ':') mode = mode.substr(1);  // ":S" va hk.
        auto ids = hostOf(L).ssGet(L, mode, pts, filter);
        return ids.empty() ? nullptr : L.mkPickSet(std::move(ids));
    });
    defineBuiltin("SSADD", [](Lisp& L, A& a) -> V {
        argc(a, 0, 2);
        if (a.empty()) return L.mkPickSet({});
        if (a.size() == 1) return L.mkPickSet({a[0]->ename});
        if (!a[1] || a[1]->kind != Kind::PickSet) fail("bad argument type: lselsetp");
        if (std::find(a[1]->ss->begin(), a[1]->ss->end(), a[0]->ename) == a[1]->ss->end()) a[1]->ss->push_back(a[0]->ename);
        return a[1];
    });
    defineBuiltin("SSDEL", [](Lisp&, A& a) -> V {
        argc(a, 2, 2);
        if (!a[1] || a[1]->kind != Kind::PickSet) fail("bad argument type: lselsetp");
        auto& v = *a[1]->ss;
        auto it = std::find(v.begin(), v.end(), a[0]->ename);
        if (it == v.end()) return nullptr;
        v.erase(it);
        return a[1];
    });
    defineBuiltin("SSLENGTH", [](Lisp&, A& a) -> V {
        argc(a, 1, 1);
        if (!a[0] || a[0]->kind != Kind::PickSet) fail("bad argument type: lselsetp " + std::string(a[0] ? "..." : "nil"));
        return mkInt(static_cast<long long>(a[0]->ss->size()));
    });
    defineBuiltin("SSNAME", [](Lisp& L, A& a) -> V {
        argc(a, 2, 2);
        if (!a[0] || a[0]->kind != Kind::PickSet) fail("bad argument type: lselsetp");
        const long long k = static_cast<long long>(num(a[1], ""));
        if (k < 0 || k >= static_cast<long long>(a[0]->ss->size())) return nullptr;
        return L.mkEname((*a[0]->ss)[static_cast<std::size_t>(k)]);
    });
    defineBuiltin("SSMEMB", [](Lisp&, A& a) -> V {
        argc(a, 2, 2);
        if (!a[1] || a[1]->kind != Kind::PickSet) return nullptr;
        return std::find(a[1]->ss->begin(), a[1]->ss->end(), a[0]->ename) != a[1]->ss->end() ? a[0] : nullptr;
    });
    defineBuiltin("SSSETFIRST", [](Lisp&, A&) -> V { return nullptr; });
    defineBuiltin("SSGETFIRST", [](Lisp&, A&) -> V { return list({nullptr, nullptr}); });

    // ---------------- jadvallar
    defineBuiltin("TBLSEARCH", [](Lisp& L, A& a) { argc(a, 2, 3); return hostOf(L).tblSearch(L, upper(str(a[0], "")), str(a[1], "")); });
    defineBuiltin("TBLNEXT", [](Lisp& L, A& a) { argc(a, 1, 2); return hostOf(L).tblNext(L, upper(str(a[0], "")), a.size() > 1 && a[1]); });
    defineBuiltin("TBLOBJNAME", [](Lisp& L, A& a) -> V { argc(a, 2, 2); return hostOf(L).tblSearch(L, upper(str(a[0], "")), str(a[1], "")) ? mkStr(str(a[1], "")) : nullptr; });
}

}  // namespace asiacad::lisp
