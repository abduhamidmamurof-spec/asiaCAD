// AutoLISP ko'prigi: APPLOAD / LISP buyruqlari, "(" bilan boshlangan qatorlar, C:XXX buyruqlari.
// LISP chizmani faqat Interpreter buyruqlari va undo qilinadigan ICommand lar orqali o'zgartiradi.
#include "interpreter.h"

#include <asiacad/core/error.h>
#include <asiacad/core/grips.h>
#include <asiacad/core/snap.h>
#include <asiacad/lisp/dcl.h>
#include <asiacad/lisp/lisp.h>

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <ctime>
#include <deque>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <sstream>

namespace asiacad::cli {

using lisp::Kind;
using lisp::Lisp;
using lisp::LispError;
using lisp::V;
namespace fs = std::filesystem;

namespace {
constexpr double kPi = 3.14159265358979323846;
std::string upperS(std::string s) { for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
bool wc(const std::string& s, const std::string& pat);  // pastda

std::string dxfName(const IEntity& e) {
    switch (e.type()) {
    case EntityType::Point: return "POINT";
    case EntityType::Line: return "LINE";
    case EntityType::Circle: return "CIRCLE";
    case EntityType::Arc: return "ARC";
    case EntityType::Polyline: return "LWPOLYLINE";
    case EntityType::Text: return "TEXT";
    case EntityType::Insert: return "INSERT";
    case EntityType::Hatch: return "HATCH";
    case EntityType::Dimension: return "DIMENSION";
    case EntityType::Mesh: return "MESH";
    }
    return "?";
}
V pt3(const Vec3& p) { return Lisp::list({Lisp::mkReal(p.x), Lisp::mkReal(p.y), Lisp::mkReal(p.z)}); }
V pair(long long code, V v) { return Lisp::cons(Lisp::mkInt(code), std::move(v)); }
V grp(long long code, const Vec3& p) { return Lisp::cons(Lisp::mkInt(code), pt3(p)); }

// DXF ro'yxatidan guruh qiymati
V dxfGet(const V& l, long long code) {
    for (V p = l; p && p->kind == Kind::Cons; p = p->cdr)
        if (p->car && p->car->kind == Kind::Cons && p->car->car && p->car->car->kind == Kind::Int && p->car->car->i == code) return p->car->cdr;
    return nullptr;
}
std::vector<V> dxfAll(const V& l, long long code) {
    std::vector<V> o;
    for (V p = l; p && p->kind == Kind::Cons; p = p->cdr)
        if (p->car && p->car->kind == Kind::Cons && p->car->car && p->car->car->kind == Kind::Int && p->car->car->i == code) o.push_back(p->car->cdr);
    return o;
}
bool dxfPt(Lisp& L, const V& l, long long code, Vec3& out) {
    V v = dxfGet(l, code);
    if (!v) return false;
    double x, y, z;
    if (!L.pointOf(v, x, y, z)) return false;
    out = {x, y, z};
    return true;
}
double dxfNum(const V& l, long long code, double def) {
    V v = dxfGet(l, code);
    return Lisp::isNum(v) ? Lisp::num(v, "") : def;
}
std::string dxfStr(const V& l, long long code, const std::string& def = {}) {
    V v = dxfGet(l, code);
    return v && v->kind == Kind::Str ? v->s : def;
}
// MTEXT formatlash kodlarini olib tashlash (\P — yangi qator, {\fArial;...} ...)
std::string plainMtext(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '{' || c == '}') continue;
        if (c == '\\' && i + 1 < s.size()) {
            const char n = s[i + 1];
            if (n == 'P' || n == 'p') { o += ' '; ++i; continue; }
            if (n == '\\' || n == '{' || n == '}') { o += n; ++i; continue; }
            const auto semi = s.find(';', i);
            if (std::string("fFhHcCwWqQaATtsSpH").find(n) != std::string::npos && semi != std::string::npos) { i = semi; continue; }
            ++i;
            continue;
        }
        o += c;
    }
    return o;
}
}  // namespace

// ------------------------------------------------------------------ host
// LISP chiqishi: oxirgi belgi kuzatiladi (keyingi xabar yangi qatordan boshlansin)
struct TrackBuf final : std::streambuf {
    std::ostream& to;
    char last = '\n';
    explicit TrackBuf(std::ostream& o) : to(o) {}
    int overflow(int c) override { if (c != EOF) { last = static_cast<char>(c); to.put(static_cast<char>(c)); } return c; }
    std::streamsize xsputn(const char* s, std::streamsize n) override { if (n > 0) { last = s[n - 1]; to.write(s, n); } return n; }
};

struct LispCtx final : lisp::LispHost {
    Interpreter& in;
    TrackBuf buf;
    std::ostream os;
    std::unique_ptr<Lisp> L;
    lisp::DclRuntime dcl{*L};
    void newline() { if (buf.last != '\n') { in.out_ << "\n"; buf.last = '\n'; } }
    std::deque<std::string> input;
    std::map<std::string, V> vars;     // qo'llab-quvvatlanmaydigan, lekin saqlanadigan tizim o'zgaruvchilari
    std::vector<std::uint64_t> prevSel;
    std::map<std::string, std::size_t> tblPos;
    std::string pending;               // ko'p qatorli ifoda (qavslar yopilmagan)
    std::vector<std::string> loaded;   // yuklangan fayllar
    std::vector<std::string> searchDirs;
    int depth = 0;

    explicit LispCtx(Interpreter& i) : in(i), buf(i.out_), os(&buf), L(std::make_unique<Lisp>(os, this)) { installVla(); dcl.install(); }
    void installVla();  // ActiveX (vla-*, vlax-*, vlax-curve-*) — pastda
    // ActiveX obyektlari: chizma obyekti — o'z id si; maxsus obyektlar — yuqori id lar
    static constexpr std::uint64_t kApp = 0xFFFFFFFFFFFFFF01ull, kDoc = 0xFFFFFFFFFFFFFF02ull, kMs = 0xFFFFFFFFFFFFFF03ull,
                                   kLayers = 0xFFFFFFFFFFFFFF05ull, kBlocks = 0xFFFFFFFFFFFFFF06ull, kLayerBase = 0xFE00000000000000ull;
    std::vector<std::string> layerObjs;  // kLayerBase + k
    V layerObj(const std::string& n) {
        auto it = std::find(layerObjs.begin(), layerObjs.end(), n);
        std::size_t k = static_cast<std::size_t>(it - layerObjs.begin());
        if (it == layerObjs.end()) layerObjs.push_back(n);
        return L->mkEname(kLayerBase + k);
    }

    // --- command: AutoCAD argumentlar ketma-ketligini AsiaCAD qatorlariga aylantirish
    static std::string quote(const std::string& t) {
        if (t.find_first_of(" \t") == std::string::npos && !t.empty()) return t;
        return "\"" + t + "\"";
    }
    void runLine(const std::vector<std::string>& toks) {
        if (toks.empty()) return;
        newline();
        std::string line;
        for (const auto& t : toks) line += (line.empty() ? "" : " ") + quote(t);
        in.run(line);
    }
    static bool isPoint(const std::string& t) { return t.find(',') != std::string::npos && !t.empty() && (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '-' || t[0] == '.' || t[0] == '@'); }
    static Vec3 ptOf(const std::string& t) {
        Vec3 p{};
        std::stringstream ss(t);
        std::string part;
        double* f[3] = {&p.x, &p.y, &p.z};
        for (int k = 0; k < 3 && std::getline(ss, part, ','); ++k) *f[k] = std::atof(part.c_str());
        return p;
    }
    static std::string ptStr(const Vec3& p) { return std::format("{:.10g},{:.10g},{:.10g}", p.x, p.y, p.z); }

    void command(const std::vector<std::string>& raw) override {
        // "_C" -> "C"; obyektga bog'lanish (osnap) bekor qilish so'zlari ("_non" ...) olib tashlanadi
        static const std::set<std::string> osn{"NON", "NONE", "END", "ENDP", "MID", "CEN", "INT", "PER", "NEA", "QUA", "TAN", "INS", "NOD", "APP", "EXT"};
        std::vector<std::string> toks;
        for (std::size_t k = 0; k < raw.size(); ++k) {
            std::string t = raw[k];
            if (t.size() > 1 && t[0] == '_' && std::isalpha(static_cast<unsigned char>(t[1]))) t.erase(0, 1);
            if (osn.count(upperS(t)) && k + 1 < raw.size() && isPoint(raw[k + 1])) continue;
            toks.push_back(t);
        }
        std::size_t i = 0;
        auto at = [&](std::size_t k) -> std::string { return k < toks.size() ? toks[k] : std::string(); };
        auto selection = [&]() {  // tanlov: "" gacha (ename/ss/L/ALL)
            std::string s;
            while (i < toks.size() && !toks[i].empty()) {
                std::string t = toks[i++];
                const std::string u = upperS(t);
                if (u == "L" || u == "LAST") t = "LAST";
                else if (u == "ALL") t = "ALL";
                else if (u == "P" || u == "PREVIOUS") {
                    t.clear();
                    for (auto id : prevSel) t += (t.empty() ? "" : ",") + std::to_string(id);
                }
                if (!t.empty()) s += (s.empty() ? "" : ",") + t;
            }
            if (i < toks.size()) ++i;  // "" — tanlov tugadi
            return s;
        };
        while (i < toks.size()) {
            std::string name = toks[i++];
            if (name.empty()) continue;
            while (!name.empty() && (name[0] == '_' || name[0] == '.' || name[0] == '-' || name[0] == '+')) name.erase(0, 1);
            std::string cn = in.loc_.resolveCommand(name);
            const std::string U = upperS(name);
            if (cn.empty()) {
                if (U == "RECTANGLE") cn = "RECTANG";
                else if (U == "DTEXT") cn = "TEXT";
                else if (U == "LWPOLYLINE" || U == "3DPOLY") cn = "PLINE";
            }
            auto restUntilEmpty = [&]() {
                std::vector<std::string> o;
                while (i < toks.size() && !toks[i].empty()) o.push_back(toks[i++]);
                if (i < toks.size()) ++i;
                return o;
            };
            // ekran / ko'rinish buyruqlari — ta'sirsiz
            static const std::set<std::string> noop{"ZOOM", "PAN", "REGEN", "REDRAW", "REGENALL", "OSNAP", "UCS", "VIEW", "CMDECHO", "AUDIT"};
            if (noop.count(U) || noop.count(cn)) { restUntilEmpty(); continue; }
            if (cn == "UNDO" && i < toks.size() && !toks[i].empty() && !std::isdigit(static_cast<unsigned char>(toks[i][0]))) {
                ++i;  // UNDO BEgin / End / Mark / Back — LISP buyrug'i baribir bitta undo qadami
                if (i < toks.size() && toks[i].empty()) ++i;
                continue;
            }
            if (cn == "LINE" || cn == "PLINE") {
                std::vector<std::string> pts;
                bool close = false;
                while (i < toks.size() && !toks[i].empty()) {
                    const std::string t = toks[i++], u = upperS(t);
                    if (u == "C" || u == "CLOSE" || u == "CL") { close = true; break; }
                    if (u == "W" || u == "H" || u == "WIDTH" || u == "HALFWIDTH") { i += 2; continue; }  // kenglik — o'tkaziladi
                    if (u == "A" || u == "L" || u == "U") continue;
                    pts.push_back(t);
                }
                if (i < toks.size() && toks[i].empty()) ++i;
                if (pts.size() < 2) continue;
                std::vector<std::string> line{cn};
                line.insert(line.end(), pts.begin(), pts.end());
                if (close) {
                    if (cn == "PLINE") line.push_back(in.loc_.keywordName("CLOSE"));
                    else line.push_back(pts.front());
                }
                runLine(line);
            } else if (cn == "CIRCLE") {
                std::string c = at(i++);
                std::string r = at(i++);
                if (upperS(r) == "D") r = std::format("{:.10g}", std::atof(at(i++).c_str()) / 2);
                if (isPoint(r)) r = std::format("{:.10g}", (ptOf(r) - ptOf(c)).length());
                runLine({cn, c, r});
            } else if (cn == "TEXT") {
                std::string p = at(i);
                while (!isPoint(p) && i < toks.size()) {  // J / S variantlari
                    const std::string u = upperS(p);
                    i += (u == "J" || u == "S" || u == "JUSTIFY" || u == "STYLE") ? 2 : 1;
                    p = at(i);
                }
                ++i;
                std::string h = at(i), rot, txt;
                if (i < toks.size() && Lisp::isNum(Lisp::mkStr(h)) == false && std::strtod(h.c_str(), nullptr) == 0 && h.find_first_not_of("0123456789.-") != std::string::npos) {
                    txt = h; ++i;  // balandlik berilmagan (uslubda bor)
                    h = std::format("{:.10g}", in.dimHeight_);
                } else {
                    ++i;
                    rot = at(i++);
                    txt = at(i++);
                }
                std::vector<std::string> line{cn, p, h, txt};
                if (!rot.empty() && std::atof(rot.c_str()) != 0) line.push_back(rot);
                runLine(line);
            } else if (cn == "LAYER") {
                while (i < toks.size() && !toks[i].empty()) {
                    const std::string op = upperS(toks[i++]);
                    const std::string a1 = at(i);
                    if (op == "M" || op == "MAKE" || op == "S" || op == "SET" || op == "N" || op == "NEW") {
                        ++i;
                        if (op == "N" || op == "NEW") {
                            std::stringstream ss(a1); std::string n;
                            while (std::getline(ss, n, ',')) if (!in.doc_.layer(n)) in.stack_.execute(std::make_unique<SetLayerCommand>(Layer{n}));
                        } else {
                            if (op == "M" || op == "MAKE") runLine({"LAYER", a1});
                            if (in.doc_.layer(a1)) runLine({"CLAYER", a1});
                        }
                    } else if (op == "C" || op == "COLOR") {
                        const std::string col = at(i++), names = at(i++);
                        std::stringstream ss(names.empty() ? in.layer_ : names); std::string n;
                        while (std::getline(ss, n, ',')) runLine({"LAYER", n, col});
                    } else if (op == "ON" || op == "OFF" || op == "LO" || op == "LOCK" || op == "U" || op == "UNLOCK" || op == "F" || op == "FREEZE" || op == "T" || op == "THAW") {
                        const std::string names = at(i++);
                        const std::string kw = op == "LO" ? "LOCK" : op == "U" ? "UNLOCK" : op == "F" ? "FREEZE" : op == "T" ? "THAW" : op;
                        std::stringstream ss(names); std::string n;
                        while (std::getline(ss, n, ',')) {
                            if (n == "*") { for (auto& [ln, l] : in.doc_.layers()) runLine({"LAYER", l.name, in.loc_.keywordName(kw)}); }
                            else runLine({"LAYER", n, in.loc_.keywordName(kw)});
                        }
                    } else if (op == "L" || op == "LTYPE" || op == "LW" || op == "LWEIGHT" || op == "P" || op == "PLOT" || op == "TR") {
                        i += 2;  // chiziq turi / qalinlik — hozircha o'tkaziladi
                    }
                }
                if (i < toks.size()) ++i;
            } else if (cn == "ERASE" || cn == "EXPLODE") {
                const std::string s = selection();
                if (!s.empty()) runLine({cn, s});
            } else if (cn == "MOVE" || cn == "COPY") {
                const std::string s = selection();
                std::string b = at(i++), d = at(i++);
                if (cn == "COPY" && (upperS(b) == "M" || upperS(b) == "O")) { b = d; d = at(i++); }
                std::string delta;
                if (d.empty()) delta = b;  // ko'chirish vektori
                else { const Vec3 v = ptOf(d) - ptOf(b); delta = std::format("{:.10g},{:.10g}", v.x, v.y); }
                if (cn == "COPY") while (i < toks.size() && !toks[i].empty()) ++i;  // ko'p nusxa rejimi
                if (cn == "COPY" && i < toks.size() && toks[i].empty()) ++i;
                if (!s.empty()) runLine({cn, s, delta});
            } else if (cn == "ROTATE" || cn == "SCALE") {
                const std::string s = selection();
                const std::string b = at(i++), v = at(i++);
                if (!s.empty()) runLine({cn, s, b, v});
            } else if (cn == "MIRROR") {
                const std::string s = selection();
                const std::string p1 = at(i++), p2 = at(i++), yn = upperS(at(i));
                if (yn == "Y" || yn == "N" || yn == "YES" || yn == "NO") ++i;
                std::vector<std::string> line{cn, s, p1, p2};
                if (yn != "Y" && yn != "YES") line.push_back(in.loc_.keywordName("COPY"));
                if (!s.empty()) runLine(line);
            } else if (cn == "INSERT") {
                const std::string b = at(i++), p = at(i++), sx = at(i++);
                std::string sy = at(i), rot;
                if (i < toks.size()) { ++i; rot = at(i++); }
                (void)sy;
                std::vector<std::string> line{cn, b, p};
                line.push_back(sx.empty() ? "1" : sx);
                if (!rot.empty()) line.push_back(rot);
                runLine(line);
                while (i < toks.size() && !toks[i].empty()) ++i;  // atributlar
                if (i < toks.size()) ++i;
            } else if (cn == "OFFSET") {
                const std::string d = at(i++), e = at(i++), side = at(i++);
                runLine({cn, d, e, side});
                while (i < toks.size() && !toks[i].empty()) ++i;
                if (i < toks.size()) ++i;
            } else if (cn == "CHPROP" || cn == "CHANGE") {
                const std::string s = selection();
                std::vector<std::string> line{"CHPROP", s};
                while (i < toks.size() && !toks[i].empty()) {
                    const std::string op = upperS(toks[i++]);
                    const std::string v = at(i++);
                    if (op == "LA" || op == "LAYER") line.push_back(in.loc_.keywordName("LAYER") + "=" + v);
                    else if (op == "C" || op == "COLOR") line.push_back(in.loc_.keywordName("COLOR") + "=" + (upperS(v) == "BYLAYER" ? "256" : v));
                }
                if (i < toks.size()) ++i;
                if (!s.empty() && line.size() > 2) runLine(line);
            } else if (cn.empty()) {
                const auto rest = restUntilEmpty();
                warn("command: " + name);
            } else {
                // umumiy: buyruq va "" gacha argumentlar
                std::vector<std::string> line{cn};
                for (auto& t : restUntilEmpty()) line.push_back(t);
                runLine(line);
            }
        }
    }
    void warn(const std::string& w) {
        if (std::find(L->warnings.begin(), L->warnings.end(), w) == L->warnings.end()) L->warnings.push_back(w);
    }

    bool nextInput(std::string& s, const std::string& prompt) override {
        if (input.empty()) {
            if (!in.lispInput) return false;
            std::string pr = prompt;
            while (!pr.empty() && (pr[0] == '\n' || pr[0] == ' ')) pr.erase(0, 1);
            return in.lispInput(s, pr);
        }
        s = input.front();
        input.pop_front();
        return true;
    }
    void alert(const std::string& msg) override { in.out_ << "** " << msg << "\n"; }

    std::uint64_t entLast() override {
        const auto ids = in.doc_.ids();
        return ids.empty() ? 0 : ids.back();
    }
    std::uint64_t entNext(std::uint64_t after) override {
        const auto ids = in.doc_.ids();
        if (after == 0) return ids.empty() ? 0 : ids.front();
        auto it = std::find(ids.begin(), ids.end(), static_cast<EntityId>(after));
        if (it == ids.end() || ++it == ids.end()) return 0;
        return *it;
    }

    V entGet(Lisp& Lp, std::uint64_t id) override {
        const IEntity* e = in.doc_.find(static_cast<EntityId>(id));
        if (!e) return nullptr;
        std::vector<V> g{pair(-1, Lp.mkEname(id)), pair(0, Lisp::mkStr(dxfName(*e))), pair(5, Lisp::mkStr(std::format("{:X}", id))),
                         pair(410, Lisp::mkStr("Model")), pair(8, Lisp::mkStr(e->layer()))};
        if (e->color() != kColorByLayer) g.push_back(pair(62, Lisp::mkInt(e->color())));
        switch (e->type()) {
        case EntityType::Point: g.push_back(grp(10, static_cast<const PointEntity*>(e)->position())); break;
        case EntityType::Line: {
            auto* l = static_cast<const LineEntity*>(e);
            g.push_back(grp(10, l->startPoint()));
            g.push_back(grp(11, l->endPoint()));
            break;
        }
        case EntityType::Circle: {
            auto* c = static_cast<const CircleEntity*>(e);
            g.push_back(grp(10, c->center()));
            g.push_back(pair(40, Lisp::mkReal(c->radius())));
            break;
        }
        case EntityType::Arc: {
            auto* a = static_cast<const ArcEntity*>(e);
            g.push_back(grp(10, a->center()));
            g.push_back(pair(40, Lisp::mkReal(a->radius())));
            g.push_back(pair(50, Lisp::mkReal(a->startAngle())));
            g.push_back(pair(51, Lisp::mkReal(a->endAngle())));
            break;
        }
        case EntityType::Polyline: {
            auto* p = static_cast<const PolylineEntity*>(e);
            const auto& vs = p->vertices();
            const auto& bs = p->bulges();
            g.push_back(pair(100, Lisp::mkStr("AcDbPolyline")));
            g.push_back(pair(90, Lisp::mkInt(static_cast<long long>(vs.size()))));
            g.push_back(pair(70, Lisp::mkInt(p->isClosed() ? 1 : 0)));
            g.push_back(pair(38, Lisp::mkReal(vs.empty() ? 0 : vs.front().z)));
            for (std::size_t k = 0; k < vs.size(); ++k) {
                g.push_back(Lisp::cons(Lisp::mkInt(10), Lisp::list({Lisp::mkReal(vs[k].x), Lisp::mkReal(vs[k].y)})));
                g.push_back(pair(42, Lisp::mkReal(k < bs.size() ? bs[k] : 0.0)));
            }
            break;
        }
        case EntityType::Text: {
            auto* t = static_cast<const TextEntity*>(e);
            g.push_back(grp(10, t->insertion()));
            g.push_back(pair(40, Lisp::mkReal(t->height())));
            g.push_back(pair(1, Lisp::mkStr(t->text())));
            g.push_back(pair(50, Lisp::mkReal(t->rotation())));
            g.push_back(pair(7, Lisp::mkStr("Standard")));
            g.push_back(pair(72, Lisp::mkInt(static_cast<int>(t->hAlign()))));
            g.push_back(pair(73, Lisp::mkInt(static_cast<int>(t->vAlign()))));
            g.push_back(grp(11, t->insertion()));
            break;
        }
        case EntityType::Insert: {
            auto* r = static_cast<const InsertEntity*>(e);
            double sx = 1, sy = 1, rot = 0;
            r->decompose(sx, sy, rot);
            g.push_back(pair(2, Lisp::mkStr(r->block() ? r->block()->name : "")));
            g.push_back(grp(10, r->insertionPoint()));
            g.push_back(pair(41, Lisp::mkReal(sx)));
            g.push_back(pair(42, Lisp::mkReal(sy)));
            g.push_back(pair(43, Lisp::mkReal(r->matrix().sz)));
            g.push_back(pair(50, Lisp::mkReal(rot)));
            if (!r->attributes().empty()) g.push_back(pair(66, Lisp::mkInt(1)));
            break;
        }
        case EntityType::Hatch: g.push_back(pair(2, Lisp::mkStr(static_cast<const HatchEntity*>(e)->pattern()))); break;
        case EntityType::Dimension: g.push_back(pair(1, Lisp::mkStr(static_cast<const DimensionEntity*>(e)->text()))); break;
        default: break;
        }
        return Lisp::fromVector(g);
    }

    // DXF ro'yxatidan obyekt (entmake / entmod)
    std::unique_ptr<IEntity> build(Lisp& Lp, const V& dxf, const std::string& type) {
        Vec3 p10{}, p11{};
        const bool h10 = dxfPt(Lp, dxf, 10, p10), h11 = dxfPt(Lp, dxf, 11, p11);
        if (type == "POINT" && h10) return std::make_unique<PointEntity>(p10);
        if (type == "LINE" && h10 && h11) return std::make_unique<LineEntity>(p10, p11);
        if (type == "CIRCLE" && h10) return std::make_unique<CircleEntity>(p10, dxfNum(dxf, 40, 1));
        if (type == "ARC" && h10) return std::make_unique<ArcEntity>(p10, dxfNum(dxf, 40, 1), dxfNum(dxf, 50, 0), dxfNum(dxf, 51, kPi));
        if (type == "LWPOLYLINE") {
            const double elev = dxfNum(dxf, 38, 0);
            std::vector<Vec3> pts;
            std::vector<double> bul;
            bool after10 = false;
            for (V q = dxf; q && q->kind == Kind::Cons; q = q->cdr) {
                const V g = q->car;
                if (!g || g->kind != Kind::Cons || !g->car || g->car->kind != Kind::Int) continue;
                if (g->car->i == 10) {
                    double x, y, z;
                    if (Lp.pointOf(g->cdr, x, y, z)) { pts.push_back({x, y, elev}); bul.push_back(0); after10 = true; }
                } else if (g->car->i == 42 && after10 && Lisp::isNum(g->cdr)) bul.back() = Lisp::num(g->cdr, "");
            }
            if (pts.size() < 2) return nullptr;
            return std::make_unique<PolylineEntity>(std::move(pts), std::move(bul), (static_cast<int>(dxfNum(dxf, 70, 0)) & 1) != 0);
        }
        if ((type == "TEXT" || type == "MTEXT" || type == "ATTDEF") && h10) {
            std::string txt = dxfStr(dxf, 1);
            if (type == "MTEXT") {
                for (auto& v : dxfAll(dxf, 3)) if (v && v->kind == Kind::Str) txt = v->s + txt;
                txt = plainMtext(txt);
            }
            const int h72 = static_cast<int>(dxfNum(dxf, 72, 0)), v73 = static_cast<int>(dxfNum(dxf, 73, 0));
            const bool aligned = (h72 != 0 || v73 != 0) && h11;
            const auto ha = h72 == 1 || h72 == 4 ? TextHAlign::Center : h72 == 2 ? TextHAlign::Right : TextHAlign::Left;
            const auto va = v73 == 1 ? TextVAlign::Bottom : v73 == 2 || h72 == 4 ? TextVAlign::Middle : v73 == 3 ? TextVAlign::Top : TextVAlign::Baseline;
            double rot = dxfNum(dxf, 50, 0);
            if (type == "MTEXT") {
                Vec3 dir;
                if (dxfPt(Lp, dxf, 11, dir) && (dir.x != 0 || dir.y != 0)) rot = std::atan2(dir.y, dir.x);
                return std::make_unique<TextEntity>(p10, dxfNum(dxf, 40, in.dimHeight_), txt, rot);
            }
            return std::make_unique<TextEntity>(aligned ? p11 : p10, dxfNum(dxf, 40, in.dimHeight_), txt, rot, ha, va);
        }
        if (type == "INSERT" && h10) {
            const BlockPtr def = in.doc_.block(dxfStr(dxf, 2));
            if (!def) throw LispError("block not found: " + dxfStr(dxf, 2));
            const double sx = dxfNum(dxf, 41, 1), sy = dxfNum(dxf, 42, sx), sz = dxfNum(dxf, 43, 1);
            return std::make_unique<InsertEntity>(def, InsertEntity::placement(def->base, p10, sx, sy, sz, dxfNum(dxf, 50, 0)));
        }
        return nullptr;
    }
    void applyProps(IEntity& e, const V& dxf, const IEntity* old) {
        std::string lay = dxfStr(dxf, 8, old ? old->layer() : in.layer_);
        if (lay.empty()) lay = "0";
        if (!in.doc_.layer(lay)) in.stack_.execute(std::make_unique<SetLayerCommand>(Layer{lay}));
        e.setLayer(lay);
        V c = dxfGet(dxf, 62);
        e.setColor(c && Lisp::isNum(c) ? static_cast<int>(Lisp::num(c, "")) : old ? old->color() : in.color_);
        if (old) for (auto& [k, v] : old->xdata()) e.xset(k, v);
    }
    std::uint64_t entMake(Lisp& Lp, const V& dxf) override {
        const std::string type = upperS(dxfStr(dxf, 0));
        if (type == "LAYER") {
            const std::string n = dxfStr(dxf, 2);
            if (n.empty()) return 0;
            Layer l{n};
            l.color = std::abs(static_cast<int>(dxfNum(dxf, 62, 7)));
            l.visible = dxfNum(dxf, 62, 7) >= 0;
            const int fl = static_cast<int>(dxfNum(dxf, 70, 0));
            l.frozen = fl & 1;
            l.locked = fl & 4;
            in.stack_.execute(std::make_unique<SetLayerCommand>(l));
            return 1;
        }
        if (type == "STYLE" || type == "LTYPE" || type == "DIMSTYLE" || type == "APPID" || type == "SEQEND" || type == "VERTEX" || type == "ATTRIB" ||
            type == "BLOCK" || type == "ENDBLK") {
            warn("entmake: " + type);
            return 1;  // jadval yozuvlari — qabul qilinadi (ta'sirsiz)
        }
        auto e = build(Lp, dxf, type);
        if (!e) { warn("entmake: " + type); return 0; }
        applyProps(*e, dxf, nullptr);
        auto& c = static_cast<AddEntityCommand&>(in.stack_.execute(std::make_unique<AddEntityCommand>(std::move(e), "ENTMAKE")));
        in.last_ = {c.entityId()};
        return c.entityId();
    }
    bool entMod(Lisp& Lp, const V& dxf) override {
        V en = dxfGet(dxf, -1);
        if (!en || en->kind != Kind::Ename) return false;
        const auto id = static_cast<EntityId>(en->ename);
        const IEntity* old = in.doc_.find(id);
        if (!old) return false;
        auto e = build(Lp, dxf, dxfName(*old));
        if (!e) {  // geometriyasi qurilmaydigan turlar: faqat qatlam/rang
            V c = dxfGet(dxf, 62);
            std::optional<std::string> lay;
            if (auto l = dxfStr(dxf, 8); !l.empty() && l != old->layer()) lay = l;
            std::optional<int> col;
            if (c && Lisp::isNum(c)) col = static_cast<int>(Lisp::num(c, ""));
            if (lay || col) in.stack_.execute(std::make_unique<ChangePropsCommand>(std::vector<EntityId>{id}, lay, col));
            return true;
        }
        applyProps(*e, dxf, old);
        if (old->type() == EntityType::Insert && e->type() == EntityType::Insert) {
            // atributlar saqlanadi
        }
        in.stack_.execute(std::make_unique<ReplaceEntityCommand>(id, std::move(e), "ENTMOD"));
        return true;
    }
    bool entDel(std::uint64_t id) override {
        if (!in.doc_.contains(static_cast<EntityId>(id))) return false;
        in.stack_.execute(std::make_unique<DeleteCommand>(std::vector<EntityId>{static_cast<EntityId>(id)}));
        return true;
    }

    // --- ssget filtri: (0 . "LINE,ARC") (8 . "A*") (62 . 1) (2 . "BLK") (1 . "T*"), -4 mantiqiy operatorlar
    bool matchGroup(Lisp& Lp, const IEntity& e, const V& g, V& cache) {
        if (!g || g->kind != Kind::Cons || !g->car || g->car->kind != Kind::Int) return true;
        const long long code = g->car->i;
        const V want = g->cdr;
        if (!cache) cache = entGet(Lp, e.id());
        V have = dxfGet(cache, code);
        if (code == 62 && !have) have = Lisp::mkInt(256);
        if (!have) return false;
        if (want && want->kind == Kind::Str) return have->kind == Kind::Str && wc(upperS(have->s), upperS(want->s));
        return Lisp::equalV(have, want, 1e-9);
    }
    bool matchSeq(Lisp& Lp, const IEntity& e, std::vector<V>& gs, std::size_t& k, const std::string& op, V& cache) {
        bool acc = op == "AND" || op == "NOT";
        bool first = true;
        int count = 0;
        while (k < gs.size()) {
            const V g = gs[k];
            bool r;
            if (g && g->kind == Kind::Cons && g->car && g->car->kind == Kind::Int && g->car->i == -4 && g->cdr && g->cdr->kind == Kind::Str) {
                const std::string s = upperS(g->cdr->s);
                ++k;
                if (s.size() > 1 && s.back() == '>') break;  // yopilish
                if (s.size() > 1 && s[0] == '<') r = matchSeq(Lp, e, gs, k, s.substr(1), cache);
                else continue;  // taqqoslash operatorlari (=, <, >) — e'tiborsiz
            } else {
                r = matchGroup(Lp, e, g, cache);
                ++k;
            }
            ++count;
            if (op == "OR") acc = first ? r : (acc || r);
            else if (op == "XOR") acc = first ? r : (acc != r);
            else acc = acc && r;
            first = false;
        }
        if (op == "NOT") return !acc;
        if (count == 0) return op != "OR";
        return acc;
    }
    bool matches(Lisp& Lp, const IEntity& e, const V& filter) {
        if (!filter) return true;
        auto gs = Lisp::toVector(filter);
        std::size_t k = 0;
        V cache;
        return matchSeq(Lp, e, gs, k, "AND", cache);
    }
    std::vector<std::uint64_t> ssGet(Lisp& Lp, const std::string& mode, const std::vector<V>& pts, const V& filter) override {
        std::vector<EntityId> cand;
        auto bboxOf = [&](const std::vector<V>& ps) {
            BBox b;
            for (auto& p : ps) {
                double x, y, z;
                if (Lp.pointOf(p, x, y, z)) b.expand({x, y, 0});
                else for (auto& q : Lisp::toVector(p)) if (Lp.pointOf(q, x, y, z)) b.expand({x, y, 0});
            }
            return b;
        };
        if (mode == "X" || mode == "A") cand = in.doc_.ids();
        else if (mode == "L") { const auto l = entLast(); if (l) cand = {static_cast<EntityId>(l)}; }
        else if (mode == "P") for (auto id : prevSel) cand.push_back(static_cast<EntityId>(id));
        else if ((mode == "W" || mode == "C" || mode == "WP" || mode == "CP") && !pts.empty()) cand = selectWindow(in.doc_, bboxOf(pts), mode[0] == 'C');
        else if (mode == "F" && !pts.empty()) cand = selectWindow(in.doc_, bboxOf(pts), true);
        else if (mode.empty() && pts.size() == 1) {
            double x, y, z;
            if (Lp.pointOf(pts[0], x, y, z)) if (auto id = pickEntity(in.doc_, {x, y, 0}, in.aperture_); id != kInvalidId) cand = {id};
        } else if (mode.empty() && pts.size() >= 2) cand = selectWindow(in.doc_, bboxOf(pts), true);
        else if (mode == "I") cand = in.last_;
        else {  // foydalanuvchi tanlovi: navbatdagi kiritish (ALL, LAST, 1,2,5, W:x1,y1,x2,y2 ...)
            std::string s;
            if (nextInput(s, "")) cand = in.parseSelection(s);
        }
        std::vector<std::uint64_t> out;
        for (EntityId id : cand)
            if (const IEntity* e = in.doc_.find(id); e && matches(Lp, *e, filter)) out.push_back(id);
        if (mode == "S" || mode == ":S") if (out.size() > 1) out.resize(1);
        if (!out.empty()) prevSel = out;
        return out;
    }

    V layerRec(const Layer& l) {
        return Lisp::list({pair(0, Lisp::mkStr("LAYER")), pair(2, Lisp::mkStr(l.name)),
                           pair(70, Lisp::mkInt((l.frozen ? 1 : 0) | (l.locked ? 4 : 0))), pair(62, Lisp::mkInt(l.visible ? l.color : -l.color)),
                           pair(6, Lisp::mkStr("Continuous"))});
    }
    V tblSearch(Lisp&, const std::string& table, const std::string& name) override {
        if (table == "LAYER") {
            for (auto& [k, l] : in.doc_.layers()) if (upperS(l.name) == upperS(name)) return layerRec(l);
            return nullptr;
        }
        if (table == "BLOCK") {
            const BlockPtr b = in.doc_.block(name);
            if (!b) return nullptr;
            return Lisp::list({pair(0, Lisp::mkStr("BLOCK")), pair(2, Lisp::mkStr(b->name)), pair(70, Lisp::mkInt(0)), grp(10, b->base)});
        }
        if (table == "STYLE" && upperS(name) == "STANDARD")
            return Lisp::list({pair(0, Lisp::mkStr("STYLE")), pair(2, Lisp::mkStr("Standard")), pair(40, Lisp::mkReal(0)), pair(3, Lisp::mkStr("txt"))});
        if (table == "LTYPE" && (upperS(name) == "CONTINUOUS" || upperS(name) == "BYLAYER" || upperS(name) == "BYBLOCK"))
            return Lisp::list({pair(0, Lisp::mkStr("LTYPE")), pair(2, Lisp::mkStr(name))});
        if (table == "DIMSTYLE" && upperS(name) == "STANDARD") return Lisp::list({pair(0, Lisp::mkStr("DIMSTYLE")), pair(2, Lisp::mkStr("Standard"))});
        return nullptr;
    }
    V tblNext(Lisp&, const std::string& table, bool rewind) override {
        auto& pos = tblPos[table];
        if (rewind) pos = 0;
        std::size_t k = 0;
        if (table == "LAYER") {
            for (auto& [n, l] : in.doc_.layers()) if (k++ == pos) { ++pos; return layerRec(l); }
        } else if (table == "BLOCK") {
            for (auto& [n, b] : in.doc_.blocks())
                if (k++ == pos) { ++pos; return Lisp::list({pair(0, Lisp::mkStr("BLOCK")), pair(2, Lisp::mkStr(b->name)), grp(10, b->base)}); }
        } else if (table == "STYLE" || table == "LTYPE" || table == "DIMSTYLE") {
            if (pos == 0) { ++pos; return Lisp::list({pair(0, Lisp::mkStr(table)), pair(2, Lisp::mkStr(table == "LTYPE" ? "Continuous" : "Standard"))}); }
        }
        return nullptr;
    }

    V getVar(Lisp&, const std::string& n) override {
        if (n == "CLAYER") return Lisp::mkStr(in.layer_);
        if (n == "CECOLOR") return Lisp::mkStr(in.color_ == kColorByLayer ? "BYLAYER" : std::to_string(in.color_));
        if (n == "TEXTSIZE" || n == "DIMTXT") return Lisp::mkReal(in.dimHeight_);
        if (n == "FILLETRAD") return Lisp::mkReal(in.filletR_);
        if (n == "PICKBOX" || n == "APERTURE") return Lisp::mkInt(static_cast<long long>(in.aperture_));
        if (n == "DWGNAME") return Lisp::mkStr(in.currentFile_.empty() ? "Drawing1.dwg" : fs::path(in.currentFile_).filename().string());
        if (n == "DWGPREFIX") {
            std::string d = in.currentFile_.empty() ? fs::current_path().string() : fs::absolute(in.currentFile_).parent_path().string();
            if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
            return Lisp::mkStr(d);
        }
        if (n == "CDATE" || n == "DATE") {
            const std::time_t t = std::time(nullptr);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            if (n == "CDATE")
                return Lisp::mkReal((tm.tm_year + 1900) * 10000.0 + (tm.tm_mon + 1) * 100.0 + tm.tm_mday + tm.tm_hour / 100.0 + tm.tm_min / 10000.0 + tm.tm_sec / 1000000.0);
            return Lisp::mkReal(2440587.5 + static_cast<double>(t) / 86400.0);
        }
        if (n == "EXTMIN" || n == "EXTMAX" || n == "LIMMIN" || n == "LIMMAX") {
            const BBox b = in.doc_.extents();
            return b.valid() ? pt3(n.back() == 'N' ? b.min : b.max) : pt3({0, 0, 0});
        }
        if (n == "ACADVER") return Lisp::mkStr("25.0s (AsiaCAD)");
        if (n == "PRODUCT") return Lisp::mkStr("AsiaCAD");
        if (n == "PLATFORM") return Lisp::mkStr(
#ifdef _WIN32
            "Microsoft Windows"
#else
            "Linux"
#endif
        );
        if (n == "LOGINNAME") { const char* u = std::getenv("USER"); if (!u) u = std::getenv("USERNAME"); return Lisp::mkStr(u ? u : "user"); }
        if (n == "CTAB") return Lisp::mkStr("Model");
        if (n == "MILLISECS") return Lisp::mkInt(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if (auto it = vars.find(n); it != vars.end()) return it->second;
        static const std::map<std::string, long long> ints{{"CMDECHO", 1}, {"OSMODE", 0}, {"ORTHOMODE", 0}, {"LUPREC", 4}, {"AUPREC", 0}, {"LUNITS", 2}, {"AUNITS", 0},
                                                          {"INSUNITS", 4}, {"PDMODE", 0}, {"BLIPMODE", 0}, {"ATTREQ", 1}, {"ATTDIA", 0}, {"FILEDIA", 1},
                                                          {"EXPERT", 0}, {"HIGHLIGHT", 1}, {"TILEMODE", 1}, {"CVPORT", 2}, {"DIMZIN", 0}, {"PLINEWID", 0},
                                                          {"SNAPMODE", 0}, {"GRIDMODE", 0}, {"MEASUREMENT", 1}, {"PEDITACCEPT", 1}, {"QAFLAGS", 0}, {"UNDOCTL", 5}};
        if (auto it = ints.find(n); it != ints.end()) return Lisp::mkInt(it->second);
        if (n == "VIEWCTR") { const BBox b = in.doc_.extents(); return pt3(b.valid() ? (b.min + b.max) * 0.5 : Vec3{}); }
        if (n == "VIEWSIZE") { const BBox b = in.doc_.extents(); return Lisp::mkReal(b.valid() ? b.max.y - b.min.y : 100.0); }
        if (n == "ANGBASE" || n == "ELEVATION" || n == "THICKNESS" || n == "PDSIZE") return Lisp::mkReal(0);
        if (n == "ANGDIR") return Lisp::mkInt(0);
        if (n == "CELTYPE" || n == "CELWEIGHT") return Lisp::mkStr("BYLAYER");
        if (n == "TEXTSTYLE" || n == "DIMSTYLE") return Lisp::mkStr("Standard");
        if (n == "LTSCALE" || n == "DIMSCALE" || n == "CELTSCALE") return Lisp::mkReal(1);
        return nullptr;
    }
    bool setVar(Lisp&, const std::string& n, const V& v) override {
        if (n == "CLAYER") {
            const std::string l = Lisp::str(v, "setvar");
            if (!in.doc_.layer(l)) return false;
            in.layer_ = l;
            return true;
        }
        if (n == "CECOLOR") {
            const std::string s = v && v->kind == Kind::Str ? upperS(v->s) : std::to_string(static_cast<long long>(Lisp::num(v, "")));
            in.color_ = s == "BYLAYER" ? kColorByLayer : std::atoi(s.c_str());
            return true;
        }
        if (n == "TEXTSIZE" || n == "DIMTXT") { in.dimHeight_ = Lisp::num(v, ""); return true; }
        if (n == "FILLETRAD") { in.filletR_ = Lisp::num(v, ""); return true; }
        if (n == "PICKBOX" || n == "APERTURE") { in.aperture_ = Lisp::num(v, ""); return true; }
        static const std::set<std::string> ro{"DWGNAME", "DWGPREFIX", "CDATE", "DATE", "ACADVER", "PLATFORM", "LOGINNAME", "EXTMIN", "EXTMAX", "MILLISECS", "PRODUCT"};
        if (ro.count(n)) return false;
        vars[n] = v;
        return true;
    }
    std::string findFile(const std::string& name) override {
        std::vector<fs::path> cand{fs::path(name)};
        if (!in.currentFile_.empty()) cand.push_back(fs::absolute(in.currentFile_).parent_path() / name);
        for (auto& d : searchDirs) cand.push_back(fs::path(d) / name);
        for (auto& c : cand) {
            std::error_code ec;
            if (fs::is_regular_file(c, ec)) return c.string();
        }
        return {};
    }
};

namespace {
bool wc(const std::string& s, const std::string& pat) { return lisp::lispWcmatch(s, pat); }

// ---- egri chiziq parametri (AutoCAD vlax-curve-* ga mos): kesma — masofa, aylana/yoy — burchak, polichiziq — uch indeksi
struct CurveParam {
    const ICurve* c;
    std::vector<double> segLen;  // polichiziq
    CurveParam(const ICurve* cc) : c(cc) {
        if (auto* p = dynamic_cast<const PolylineEntity*>(c)) {
            const auto& v = p->vertices();
            const auto& b = p->bulges();
            for (std::size_t k = 0; k < p->segmentCount(); ++k) {
                const Vec3 a = v[k], e = v[(k + 1) % v.size()];
                const double ch = std::hypot(e.x - a.x, e.y - a.y);
                const double bu = k < b.size() ? b[k] : 0.0;
                if (std::abs(bu) < 1e-12 || ch < 1e-15) segLen.push_back(ch);
                else {
                    const double th = 4 * std::atan(std::abs(bu));
                    segLen.push_back(th * ch / (2 * std::sin(th / 2)));
                }
            }
        }
    }
    double startParam() const {
        if (auto* a = dynamic_cast<const ArcEntity*>(c)) return a->startAngle();
        return 0;
    }
    double endParam() const {
        if (dynamic_cast<const CircleEntity*>(c)) return 2 * kPi;
        if (auto* a = dynamic_cast<const ArcEntity*>(c)) return a->startAngle() + a->sweep();
        if (!segLen.empty() || dynamic_cast<const PolylineEntity*>(c)) return static_cast<double>(segLen.size());
        return c->length();
    }
    double distAtParam(double t) const {
        if (auto* ci = dynamic_cast<const CircleEntity*>(c)) return ci->radius() * t;
        if (auto* a = dynamic_cast<const ArcEntity*>(c)) return a->radius() * (t - a->startAngle());
        if (dynamic_cast<const PolylineEntity*>(c)) {
            double d = 0;
            std::size_t k = 0;
            for (; k < segLen.size() && k + 1 <= t; ++k) d += segLen[k];
            if (k < segLen.size()) d += (t - static_cast<double>(k)) * segLen[k];
            return d;
        }
        return t;
    }
    double paramAtDist(double d) const {
        if (auto* ci = dynamic_cast<const CircleEntity*>(c)) return d / ci->radius();
        if (auto* a = dynamic_cast<const ArcEntity*>(c)) return a->startAngle() + d / a->radius();
        if (dynamic_cast<const PolylineEntity*>(c)) {
            double acc = 0;
            for (std::size_t k = 0; k < segLen.size(); ++k) {
                if (d <= acc + segLen[k] || k + 1 == segLen.size()) return static_cast<double>(k) + (segLen[k] > 0 ? (d - acc) / segLen[k] : 0);
                acc += segLen[k];
            }
            return 0;
        }
        return d;
    }
    double distAtPoint(const Vec3& p) const {  // eng yaqin nuqtaning masofasi
        const double L = c->length();
        const int N = 2000;
        double best = 0, bd = 1e300;
        for (int k = 0; k <= N; ++k) {
            const double s = L * k / N;
            const Vec3 q = c->pointAtDistance(s);
            const double d = (q - p).length();
            if (d < bd) { bd = d; best = s; }
        }
        double lo = std::max(0.0, best - L / N), hi = std::min(L, best + L / N);
        for (int it = 0; it < 60; ++it) {
            const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
            if ((c->pointAtDistance(m1) - p).length() < (c->pointAtDistance(m2) - p).length()) hi = m2; else lo = m1;
        }
        return (lo + hi) / 2;
    }
};
}  // namespace

void LispCtx::installVla() {
    Lisp& Lr = *L;
    using A = std::vector<V>;
    auto fail = [](const std::string& m) -> V { throw LispError(m); };
    auto idOf = [](const V& v) -> std::uint64_t {
        if (!v || v->kind != Kind::Ename) throw LispError("bad argument type: VLA-OBJECT " + std::string(v ? "" : "nil"));
        return v->ename;
    };
    auto curve = [this, idOf](const V& v) -> const ICurve* {
        const IEntity* e = in.doc_.find(static_cast<EntityId>(idOf(v)));
        auto* c = dynamic_cast<const ICurve*>(e);
        if (!c) throw LispError("not a curve");
        return c;
    };
    auto ptArg = [&Lr](const V& v) {
        double x, y, z;
        if (!Lr.pointOf(v, x, y, z)) throw LispError("bad argument type: 2D/3D point");
        return Vec3{x, y, z};
    };
    // ----- obyektlar va to'plamlar
    Lr.defineBuiltin("VLAX-GET-ACAD-OBJECT", [](Lisp& L, A&) { return L.mkEname(kApp); });
    Lr.defineBuiltin("VLAX-ENAME->VLA-OBJECT", [](Lisp&, A& a) { return a.empty() ? nullptr : a[0]; });
    Lr.defineBuiltin("VLAX-VLA-OBJECT->ENAME", [](Lisp&, A& a) { return a.empty() ? nullptr : a[0]; });
    for (const char* n : {"VLAX-RELEASE-OBJECT", "VLA-UPDATE", "VLA-REGEN", "VLA-ZOOMEXTENTS", "VLA-ZOOMALL", "VLAX-REACTOR", "VLR-REMOVE", "VLR-REMOVE-ALL",
                          "VLA-STARTUNDOMARK", "VLA-ENDUNDOMARK", "VLAX-DUMP-OBJECT", "VLA-HIGHLIGHT", "VLR-OBJECT-REACTOR", "VLR-COMMAND-REACTOR",
                          "VLR-EDITOR-REACTOR", "VLR-DWG-REACTOR", "VLR-DOCMANAGER-REACTOR", "VLR-MOUSE-REACTOR", "VLR-SYSVAR-REACTOR", "VLR-PERS"})
        Lr.defineBuiltin(n, [](Lisp&, A&) -> V { return nullptr; });
    Lr.defineBuiltin("VLAX-OBJECT-RELEASED-P", [](Lisp&, A&) -> V { return nullptr; });
    Lr.defineBuiltin("VLAX-ERASED-P", [this, idOf](Lisp& L, A& a) { return in.doc_.contains(static_cast<EntityId>(idOf(a.at(0)))) ? nullptr : L.T(); });
    Lr.defineBuiltin("VLAX-3D-POINT", [ptArg](Lisp& L, A& a) {
        if (a.size() == 1) { const Vec3 p = ptArg(a[0]); return L.mkPoint(p.x, p.y, p.z); }
        return L.mkPoint(Lisp::num(a.at(0), ""), Lisp::num(a.at(1), ""), a.size() > 2 ? Lisp::num(a[2], "") : 0.0);
    });
    auto identity = [](Lisp&, A& a) { return a.empty() ? nullptr : a[0]; };
    for (const char* n : {"VLAX-VARIANT-VALUE", "VLAX-SAFEARRAY->LIST", "VLAX-MAKE-VARIANT", "VLAX-VARIANT-CHANGE-TYPE"}) Lr.defineBuiltin(n, identity);
    Lr.defineBuiltin("VLAX-MAKE-SAFEARRAY", [](Lisp&, A&) -> V { return nullptr; });
    Lr.defineBuiltin("VLAX-SAFEARRAY-FILL", [](Lisp&, A& a) { return a.size() > 1 ? a[1] : nullptr; });
    Lr.defineBuiltin("VLAX-VARIANT-TYPE", [](Lisp&, A& a) { return Lisp::mkInt(a.empty() || !a[0] ? 0 : a[0]->kind == Kind::Str ? 8 : a[0]->kind == Kind::Int ? 3 : a[0]->kind == Kind::Real ? 5 : 8204); });

    // ----- xossalar
    auto getProp = std::make_shared<std::function<V(Lisp&, std::uint64_t, std::string)>>();
    auto putProp = std::make_shared<std::function<V(Lisp&, std::uint64_t, std::string, V)>>();
    *getProp = [this](Lisp& L, std::uint64_t id, std::string pr) -> V {
        pr = upperS(pr);
        if (id == kApp) {
            if (pr == "ACTIVEDOCUMENT") return L.mkEname(kDoc);
            if (pr == "VERSION") return Lisp::mkStr("25.0s (AsiaCAD)");
            if (pr == "NAME") return Lisp::mkStr("AsiaCAD");
            if (pr == "DOCUMENTS") return L.mkEname(kDoc);
        }
        if (id == kDoc) {
            if (pr == "MODELSPACE" || pr == "PAPERSPACE" || pr == "ACTIVELAYOUT") return L.mkEname(kMs);
            if (pr == "LAYERS") return L.mkEname(kLayers);
            if (pr == "BLOCKS") return L.mkEname(kBlocks);
            if (pr == "ACTIVELAYER") return layerObj(in.layer_);
            if (pr == "NAME") return getVar(L, "DWGNAME");
            if (pr == "FULLNAME") return Lisp::mkStr(in.currentFile_);
            if (pr == "PATH") return getVar(L, "DWGPREFIX");
            if (pr == "ACTIVESPACE") return Lisp::mkInt(1);
            if (pr == "APPLICATION") return L.mkEname(kApp);
            if (pr == "UTILITY" || pr == "DATABASE" || pr == "SELECTIONSETS" || pr == "TEXTSTYLES" || pr == "DIMSTYLES" || pr == "LINETYPES") return L.mkEname(kDoc);
        }
        if (id == kMs || id == kLayers || id == kBlocks) {
            if (pr == "COUNT") return Lisp::mkInt(static_cast<long long>(id == kMs ? in.doc_.size() : id == kLayers ? in.doc_.layers().size() : in.doc_.blocks().size()));
            if (pr == "NAME") return Lisp::mkStr(id == kMs ? "*Model_Space" : id == kLayers ? "Layers" : "Blocks");
        }
        if (id >= kLayerBase && id < kApp) {
            const std::size_t k = static_cast<std::size_t>(id - kLayerBase);
            if (k >= layerObjs.size()) throw LispError("bad layer object");
            auto l = in.doc_.layer(layerObjs[k]);
            if (!l) throw LispError("layer erased");
            if (pr == "NAME") return Lisp::mkStr(l->name);
            if (pr == "COLOR") return Lisp::mkInt(l->color);
            if (pr == "LAYERON") return l->visible ? L.T() : Lisp::mkInt(0);
            if (pr == "FREEZE") return l->frozen ? L.T() : Lisp::mkInt(0);
            if (pr == "LOCK") return l->locked ? L.T() : Lisp::mkInt(0);
            if (pr == "OBJECTNAME") return Lisp::mkStr("AcDbLayerTableRecord");
            if (pr == "LINETYPE") return Lisp::mkStr("Continuous");
            throw LispError("property not available: " + pr);
        }
        const IEntity* e = in.doc_.find(static_cast<EntityId>(id));
        if (!e) throw LispError("Automation Error. Object was erased");
        static const std::map<std::string, std::string> on{{"POINT", "AcDbPoint"}, {"LINE", "AcDbLine"}, {"CIRCLE", "AcDbCircle"}, {"ARC", "AcDbArc"},
                                                          {"LWPOLYLINE", "AcDbPolyline"}, {"TEXT", "AcDbText"}, {"INSERT", "AcDbBlockReference"},
                                                          {"HATCH", "AcDbHatch"}, {"DIMENSION", "AcDbRotatedDimension"}, {"MESH", "AcDbPolyFaceMesh"}};
        const std::string t = dxfName(*e);
        V d = entGet(L, id);
        if (pr == "OBJECTNAME") return Lisp::mkStr(on.at(t));
        if (pr == "LAYER") return Lisp::mkStr(e->layer());
        if (pr == "COLOR" || pr == "TRUECOLOR") return Lisp::mkInt(e->color() == kColorByLayer ? 256 : e->color());
        if (pr == "HANDLE") return Lisp::mkStr(std::format("{:X}", id));
        if (pr == "OBJECTID" || pr == "OBJECTID32") return Lisp::mkInt(static_cast<long long>(id));
        if (pr == "LINETYPE") return Lisp::mkStr("ByLayer");
        if (pr == "LINEWEIGHT") return Lisp::mkInt(-1);
        if (pr == "LINETYPESCALE") return Lisp::mkReal(1);
        if (pr == "VISIBLE") return L.T();
        if (pr == "OWNERID") return Lisp::mkInt(0);
        if (pr == "DOCUMENT") return L.mkEname(kDoc);
        if (pr == "ENTITYNAME" || pr == "ENTITYTYPE") return Lisp::mkStr(t);
        auto* c = dynamic_cast<const ICurve*>(e);
        if (pr == "LENGTH" && c) return Lisp::mkReal(c->length());
        if (pr == "AREA") {
            if (auto* ci = dynamic_cast<const CircleEntity*>(e)) return Lisp::mkReal(ci->area());
            if (auto* p = dynamic_cast<const PolylineEntity*>(e)) return Lisp::mkReal(p->area());
            if (auto* h = dynamic_cast<const HatchEntity*>(e)) return Lisp::mkReal(h->area());
            if (auto* a = dynamic_cast<const ArcEntity*>(e)) { const double th = a->sweep(); return Lisp::mkReal(0.5 * a->radius() * a->radius() * (th - std::sin(th))); }
            return Lisp::mkReal(0);
        }
        if (pr == "CLOSED" && c) return c->isClosed() ? L.T() : Lisp::mkInt(0);
        if (pr == "STARTPOINT" && c) return pt3(c->startPoint());
        if (pr == "ENDPOINT" && c) return pt3(c->endPoint());
        if (pr == "CENTER" || (pr == "POSITION" && t == "POINT") || pr == "COORDINATE") return dxfGet(d, 10);
        if (pr == "RADIUS") return dxfGet(d, 40);
        if (pr == "DIAMETER") { V r = dxfGet(d, 40); return r ? Lisp::mkReal(2 * Lisp::num(r, "")) : nullptr; }
        if (pr == "CIRCUMFERENCE" && c) return Lisp::mkReal(c->length());
        if (pr == "STARTANGLE") return dxfGet(d, 50);
        if (pr == "ENDANGLE") return dxfGet(d, 51);
        if (pr == "TOTALANGLE") if (auto* a = dynamic_cast<const ArcEntity*>(e)) return Lisp::mkReal(a->sweep());
        if (pr == "ARCLENGTH" && c) return Lisp::mkReal(c->length());
        if (pr == "TEXTSTRING") return dxfGet(d, 1);
        if (pr == "HEIGHT") return dxfGet(d, 40);
        if (pr == "ROTATION") return dxfGet(d, 50);
        if (pr == "INSERTIONPOINT" || pr == "TEXTALIGNMENTPOINT") return dxfGet(d, 10);
        if (pr == "STYLENAME") return Lisp::mkStr("Standard");
        if (pr == "NAME" || pr == "EFFECTIVENAME") return dxfGet(d, 2);
        if (pr == "XSCALEFACTOR" || pr == "XEFFECTIVESCALEFACTOR") return dxfGet(d, 41);
        if (pr == "YSCALEFACTOR" || pr == "YEFFECTIVESCALEFACTOR") return dxfGet(d, 42);
        if (pr == "ZSCALEFACTOR" || pr == "ZEFFECTIVESCALEFACTOR") return dxfGet(d, 43);
        if (pr == "HASATTRIBUTES") return dxfGet(d, 66) ? L.T() : Lisp::mkInt(0);
        if (pr == "ISDYNAMICBLOCK") return Lisp::mkInt(0);
        if (pr == "PATTERNNAME") return dxfGet(d, 2);
        if (pr == "ELEVATION") return Lisp::mkReal(c ? c->startPoint().z : 0);
        if (pr == "THICKNESS" || pr == "CONSTANTWIDTH") return Lisp::mkReal(0);
        if (pr == "NORMAL") return pt3({0, 0, 1});
        if (pr == "COORDINATES") {
            std::vector<V> o;
            if (auto* p = dynamic_cast<const PolylineEntity*>(e)) for (auto& v : p->vertices()) { o.push_back(Lisp::mkReal(v.x)); o.push_back(Lisp::mkReal(v.y)); }
            else if (auto* l = dynamic_cast<const LineEntity*>(e)) for (auto v : {l->startPoint(), l->endPoint()}) { o.push_back(Lisp::mkReal(v.x)); o.push_back(Lisp::mkReal(v.y)); o.push_back(Lisp::mkReal(v.z)); }
            return Lisp::fromVector(o);
        }
        if (pr == "MEASUREMENT" || pr == "TEXTOVERRIDE") return dxfGet(d, 1);
        throw LispError("ActiveX Server returned the error: unknown name: " + pr);
    };
    *putProp = [this](Lisp& L, std::uint64_t id, std::string pr, V v) -> V {
        pr = upperS(pr);
        if (id == kDoc && pr == "ACTIVELAYER") {
            if (!v || v->kind != Kind::Ename || v->ename < kLayerBase) throw LispError("bad layer object");
            in.layer_ = layerObjs.at(static_cast<std::size_t>(v->ename - kLayerBase));
            return nullptr;
        }
        if (id >= kLayerBase && id < kApp) {
            auto l = in.doc_.layer(layerObjs.at(static_cast<std::size_t>(id - kLayerBase)));
            if (!l) throw LispError("layer erased");
            Layer n = *l;
            const bool on = v && !(Lisp::isNum(v) && Lisp::num(v, "") == 0);
            if (pr == "COLOR") n.color = static_cast<int>(Lisp::num(v, ""));
            else if (pr == "LAYERON") n.visible = on;
            else if (pr == "FREEZE") n.frozen = on;
            else if (pr == "LOCK") n.locked = on;
            else if (pr == "NAME" || pr == "LINETYPE" || pr == "LINEWEIGHT" || pr == "PLOTTABLE" || pr == "DESCRIPTION") return nullptr;
            else throw LispError("property not available: " + pr);
            in.stack_.execute(std::make_unique<SetLayerCommand>(n));
            return nullptr;
        }
        const IEntity* e = in.doc_.find(static_cast<EntityId>(id));
        if (!e) throw LispError("Automation Error. Object was erased");
        V d = entGet(L, id);
        auto setG = [&](long long code, V val) {
            bool found = false;
            std::vector<V> o;
            for (auto& g : Lisp::toVector(d)) {
                if (g && g->kind == Kind::Cons && g->car && g->car->kind == Kind::Int && g->car->i == code && !found) { o.push_back(Lisp::cons(Lisp::mkInt(code), val)); found = true; }
                else o.push_back(g);
            }
            if (!found) o.push_back(Lisp::cons(Lisp::mkInt(code), val));
            d = Lisp::fromVector(o);
        };
        const std::string t = dxfName(*e);
        if (pr == "LAYER") setG(8, v);
        else if (pr == "COLOR" || pr == "TRUECOLOR") setG(62, v);
        else if (pr == "STARTPOINT" || pr == "CENTER" || pr == "INSERTIONPOINT" || (pr == "POSITION" && t == "POINT")) { setG(10, v); if (t == "TEXT") setG(11, v); }
        else if (pr == "ENDPOINT" || pr == "TEXTALIGNMENTPOINT") setG(11, v);
        else if (pr == "RADIUS" || pr == "HEIGHT") setG(40, v);
        else if (pr == "DIAMETER") setG(40, Lisp::mkReal(Lisp::num(v, "") / 2));
        else if (pr == "TEXTSTRING") setG(1, v);
        else if (pr == "ROTATION" || pr == "STARTANGLE") setG(50, v);
        else if (pr == "ENDANGLE") setG(51, v);
        else if (pr == "XSCALEFACTOR") setG(41, v);
        else if (pr == "YSCALEFACTOR") setG(42, v);
        else if (pr == "CLOSED") setG(70, Lisp::mkInt(v && !(Lisp::isNum(v) && Lisp::num(v, "") == 0) ? 1 : 0));
        else if (pr == "COORDINATES" && t == "LWPOLYLINE") {
            std::vector<V> o;
            for (auto& g : Lisp::toVector(d))
                if (!(g && g->kind == Kind::Cons && g->car && g->car->kind == Kind::Int && (g->car->i == 10 || g->car->i == 42))) o.push_back(g);
            auto xs = Lisp::toVector(v);
            for (std::size_t k = 0; k + 1 < xs.size(); k += 2) o.push_back(Lisp::cons(Lisp::mkInt(10), Lisp::list({xs[k], xs[k + 1]})));
            d = Lisp::fromVector(o);
        } else if (pr == "LINETYPE" || pr == "LINEWEIGHT" || pr == "LINETYPESCALE" || pr == "VISIBLE" || pr == "STYLENAME" || pr == "ELEVATION" || pr == "CONSTANTWIDTH") return nullptr;
        else throw LispError("ActiveX Server returned the error: unknown name: " + pr);
        entMod(L, d);
        return nullptr;
    };
    Lr.defineBuiltin("VLAX-GET-PROPERTY", [getProp, idOf](Lisp& L, A& a) { return (*getProp)(L, idOf(a.at(0)), Lisp::str(a.size() > 1 && a[1] && a[1]->kind == Kind::Sym ? Lisp::mkStr(a[1]->s) : a.at(1), "")); });
    Lr.defineBuiltin("VLAX-GET", [getProp, idOf](Lisp& L, A& a) { return (*getProp)(L, idOf(a.at(0)), a.size() > 1 && a[1] && a[1]->kind == Kind::Sym ? a[1]->s : Lisp::str(a.at(1), "")); });
    auto put = [putProp, idOf](Lisp& L, A& a) { return (*putProp)(L, idOf(a.at(0)), a.size() > 1 && a[1] && a[1]->kind == Kind::Sym ? a[1]->s : Lisp::str(a.at(1), ""), a.at(2)); };
    Lr.defineBuiltin("VLAX-PUT-PROPERTY", put);
    Lr.defineBuiltin("VLAX-PUT", put);
    Lr.defineBuiltin("VLAX-PROPERTY-AVAILABLE-P", [getProp, idOf](Lisp& L, A& a) -> V {
        try { (*getProp)(L, idOf(a.at(0)), a.size() > 1 && a[1] && a[1]->kind == Kind::Sym ? a[1]->s : Lisp::str(a.at(1), "")); return L.T(); } catch (const LispError&) { return nullptr; }
    });

    // ----- metodlar
    auto addEnt = [this](Lisp& L, V dxf) -> V {
        const auto id = entMake(L, dxf);
        return id ? L.mkEname(id) : nullptr;
    };
    auto method = std::make_shared<std::function<V(Lisp&, std::string, A&)>>();
    *method = [this, addEnt, ptArg, idOf, fail](Lisp& L, std::string m, A& a) -> V {
        m = upperS(m);
        auto arg = [&](std::size_t k) { return k < a.size() ? a[k] : nullptr; };  // a[0] — obyekt
        auto P = [&](std::size_t k) { return pt3(ptArg(arg(k))); };
        auto N = [&](std::size_t k) { return Lisp::num(arg(k), ""); };
        auto ent = [&](std::initializer_list<V> gs) { return addEnt(L, Lisp::list(gs)); };
        if (m == "ADDLINE") return ent({pair(0, Lisp::mkStr("LINE")), Lisp::cons(Lisp::mkInt(10), P(1)), Lisp::cons(Lisp::mkInt(11), P(2))});
        if (m == "ADDCIRCLE") return ent({pair(0, Lisp::mkStr("CIRCLE")), Lisp::cons(Lisp::mkInt(10), P(1)), pair(40, Lisp::mkReal(N(2)))});
        if (m == "ADDARC") return ent({pair(0, Lisp::mkStr("ARC")), Lisp::cons(Lisp::mkInt(10), P(1)), pair(40, Lisp::mkReal(N(2))), pair(50, Lisp::mkReal(N(3))), pair(51, Lisp::mkReal(N(4)))});
        if (m == "ADDPOINT") return ent({pair(0, Lisp::mkStr("POINT")), Lisp::cons(Lisp::mkInt(10), P(1))});
        if (m == "ADDTEXT") return ent({pair(0, Lisp::mkStr("TEXT")), pair(1, arg(1)), Lisp::cons(Lisp::mkInt(10), P(2)), pair(40, Lisp::mkReal(N(3)))});
        if (m == "ADDMTEXT") return ent({pair(0, Lisp::mkStr("MTEXT")), Lisp::cons(Lisp::mkInt(10), P(1)), pair(1, arg(3))});
        if (m == "ADDLIGHTWEIGHTPOLYLINE" || m == "ADDPOLYLINE" || m == "ADD3DPOLY") {
            const auto xs = Lisp::toVector(arg(1));
            const std::size_t st = m == "ADDLIGHTWEIGHTPOLYLINE" ? 2 : 3;
            std::vector<V> g{pair(0, Lisp::mkStr("LWPOLYLINE"))};
            for (std::size_t k = 0; k + st - 1 < xs.size(); k += st) g.push_back(Lisp::cons(Lisp::mkInt(10), Lisp::list({xs[k], xs[k + 1]})));
            return addEnt(L, Lisp::fromVector(g));
        }
        if (m == "INSERTBLOCK")
            return ent({pair(0, Lisp::mkStr("INSERT")), pair(2, arg(2)), Lisp::cons(Lisp::mkInt(10), P(1)), pair(41, Lisp::mkReal(a.size() > 3 ? N(3) : 1)),
                        pair(42, Lisp::mkReal(a.size() > 4 ? N(4) : 1)), pair(43, Lisp::mkReal(a.size() > 5 ? N(5) : 1)), pair(50, Lisp::mkReal(a.size() > 6 ? N(6) : 0))});
        const std::uint64_t self = idOf(arg(0));
        if (m == "ITEM" || m == "ADD") {
            V key = arg(1);
            if (self == kLayers) {
                std::string n;
                if (key && key->kind == Kind::Str) n = key->s;
                else {
                    const long long k = static_cast<long long>(Lisp::num(key, ""));
                    long long j = 0;
                    for (auto& [kk, l] : in.doc_.layers()) if (j++ == k) n = l.name;
                }
                if (m == "ADD" && !in.doc_.layer(n)) in.stack_.execute(std::make_unique<SetLayerCommand>(Layer{n}));
                if (!in.doc_.layer(n)) fail("Automation Error. Key not found");
                return layerObj(n);
            }
            if (self == kMs && m == "ITEM") {
                const auto ids = in.doc_.ids();
                const auto k = static_cast<std::size_t>(Lisp::num(key, ""));
                if (k >= ids.size()) fail("Automation Error. Index out of range");
                return L.mkEname(ids[k]);
            }
            if (self == kBlocks && m == "ITEM") {
                if (!in.doc_.block(Lisp::str(key, ""))) fail("Automation Error. Key not found");
                return Lisp::mkStr(Lisp::str(key, ""));
            }
        }
        if (m == "DELETE") {
            if (self >= kLayerBase && self < kApp) { in.doc_.removeLayer(layerObjs.at(static_cast<std::size_t>(self - kLayerBase))); return nullptr; }
            entDel(self);
            return nullptr;
        }
        if (m == "MOVE" || m == "COPY" || m == "ROTATE" || m == "SCALEENTITY" || m == "MIRROR") {
            const IEntity* e = in.doc_.find(static_cast<EntityId>(self));
            if (!e) fail("Automation Error. Object was erased");
            const std::string sid = std::to_string(self);
            if (m == "COPY") {
                in.run("COPY " + sid + " 0,0");
                return L.mkEname(entLast());
            }
            const Vec3 p1 = ptArg(arg(1));
            if (m == "MOVE") { const Vec3 d = ptArg(arg(2)) - p1; in.run(std::format("MOVE {} {:.10g},{:.10g}", sid, d.x, d.y)); }
            else if (m == "ROTATE") in.run(std::format("ROTATE {} {:.10g},{:.10g} {:.10g}", sid, p1.x, p1.y, N(2) * 180 / kPi));
            else if (m == "SCALEENTITY") in.run(std::format("SCALE {} {:.10g},{:.10g} {:.10g}", sid, p1.x, p1.y, N(2)));
            else { const Vec3 p2 = ptArg(arg(2)); in.run(std::format("MIRROR {} {:.10g},{:.10g} {:.10g},{:.10g} {}", sid, p1.x, p1.y, p2.x, p2.y, in.loc_.keywordName("COPY"))); return L.mkEname(entLast()); }
            return nullptr;
        }
        if (m == "OFFSET") {
            const double d = N(1);
            in.run(std::format("OFFSET {:.10g} {} {}", std::abs(d), self, "0,0"));
            return Lisp::list({L.mkEname(entLast())});
        }
        if (m == "GETBOUNDINGBOX") {
            const IEntity* e = in.doc_.find(static_cast<EntityId>(self));
            if (!e) fail("Automation Error. Object was erased");
            const BBox b = e->bbox();
            // (vla-getboundingbox obj 'mn 'mx) — belgilarga yoziladi
            if (a.size() > 2 && a[1] && a[1]->kind == Kind::Sym && a[2] && a[2]->kind == Kind::Sym) {
                L.setGlobal(a[1]->s, pt3(b.min));
                L.setGlobal(a[2]->s, pt3(b.max));
            }
            return Lisp::list({pt3(b.min), pt3(b.max)});
        }
        if (m == "EXPLODE") { in.run("EXPLODE " + std::to_string(self)); return Lisp::list({L.mkEname(entLast())}); }
        if (m == "GETATTRIBUTES" || m == "GETCONSTANTATTRIBUTES" || m == "GETDYNAMICBLOCKPROPERTIES") return nullptr;
        if (m == "INTERSECTWITH") return nullptr;
        if (m == "SETVARIABLE") { setVar(L, upperS(Lisp::str(arg(1), "")), arg(2)); return nullptr; }
        if (m == "GETVARIABLE") return getVar(L, upperS(Lisp::str(arg(1), "")));
        if (m == "SENDCOMMAND" || m == "POSTCOMMAND") {
            std::string s = Lisp::str(arg(1), "");
            std::stringstream ss(s);
            std::string ln;
            while (std::getline(ss, ln, '\n')) if (!ln.empty()) { if (ln[0] == '(') in.runLispLine(ln); else in.run(ln); }
            return nullptr;
        }
        if (m == "REGEN" || m == "ZOOMEXTENTS" || m == "UPDATE" || m == "HIGHLIGHT" || m == "STARTUNDOMARK" || m == "ENDUNDOMARK" || m == "AUDITINFO") return nullptr;
        fail("ActiveX: method not supported: " + m);
        return nullptr;
    };
    auto invoke = [method](Lisp& L, A& a) -> V {
        if (a.size() < 2) throw LispError("too few arguments");
        const std::string m = a[1] && a[1]->kind == Kind::Sym ? a[1]->s : Lisp::str(a[1], "");
        A rest{a[0]};
        rest.insert(rest.end(), a.begin() + 2, a.end());
        return (*method)(L, m, rest);
    };
    Lr.defineBuiltin("VLAX-INVOKE", invoke);
    Lr.defineBuiltin("VLAX-INVOKE-METHOD", invoke);
    Lr.defineBuiltin("VLAX-METHOD-APPLICABLE-P", [](Lisp& L, A&) { return L.T(); });
    // vla-get-XXX / vla-put-XXX / vla-XXX — dinamik
    Lr.resolver = [getProp, putProp, method, idOf](const std::string& name) -> lisp::Builtin {
        if (name.rfind("VLA-GET-", 0) == 0) {
            const std::string pr = name.substr(8);
            return [getProp, idOf, pr](Lisp& L, A& a) { return (*getProp)(L, idOf(a.at(0)), pr); };
        }
        if (name.rfind("VLA-PUT-", 0) == 0) {
            const std::string pr = name.substr(8);
            return [putProp, idOf, pr](Lisp& L, A& a) { return (*putProp)(L, idOf(a.at(0)), pr, a.at(1)); };
        }
        if (name.rfind("VLA-", 0) == 0) {
            const std::string m = name.substr(4);
            return [method, m](Lisp& L, A& a) { return (*method)(L, m, a); };
        }
        return {};
    };
    Lr.collectionItems = [this](const V& c) {
        std::vector<V> o;
        const std::uint64_t id = c && c->kind == Kind::Ename ? c->ename : 0;
        if (id == kMs) for (auto e : in.doc_.ids()) o.push_back(L->mkEname(e));
        else if (id == kLayers) for (auto& [k, l] : in.doc_.layers()) o.push_back(layerObj(l.name));
        else if (id == kBlocks) for (auto& [k, b] : in.doc_.blocks()) o.push_back(Lisp::mkStr(b->name));
        else if (c && c->kind == Kind::PickSet) for (auto e : *c->ss) o.push_back(L->mkEname(e));
        else throw LispError("vlax-for: not a collection");
        return o;
    };
    Lr.defineBuiltin("VLA-GET-ACTIVESELECTIONSET", [](Lisp& L, A&) -> V { return L.mkPickSet({}); });

    // ----- vlax-curve-*
    Lr.defineBuiltin("VLAX-CURVE-GETSTARTPOINT", [curve](Lisp&, A& a) { return pt3(curve(a.at(0))->startPoint()); });
    Lr.defineBuiltin("VLAX-CURVE-GETENDPOINT", [curve](Lisp&, A& a) { return pt3(curve(a.at(0))->endPoint()); });
    Lr.defineBuiltin("VLAX-CURVE-GETSTARTPARAM", [curve](Lisp&, A& a) { return Lisp::mkReal(CurveParam(curve(a.at(0))).startParam()); });
    Lr.defineBuiltin("VLAX-CURVE-GETENDPARAM", [curve](Lisp&, A& a) { return Lisp::mkReal(CurveParam(curve(a.at(0))).endParam()); });
    Lr.defineBuiltin("VLAX-CURVE-ISCLOSED", [curve](Lisp& L, A& a) -> V { return curve(a.at(0))->isClosed() ? L.T() : nullptr; });
    Lr.defineBuiltin("VLAX-CURVE-ISPERIODIC", [curve](Lisp& L, A& a) -> V { const ICurve* c = curve(a.at(0)); return dynamic_cast<const CircleEntity*>(c) ? L.T() : nullptr; });
    Lr.defineBuiltin("VLAX-CURVE-ISPLANAR", [](Lisp& L, A&) { return L.T(); });
    Lr.defineBuiltin("VLAX-CURVE-GETDISTATPARAM", [curve](Lisp&, A& a) -> V {
        CurveParam cp(curve(a.at(0)));
        const double t = Lisp::num(a.at(1), "");
        if (t < cp.startParam() - 1e-9 || t > cp.endParam() + 1e-9) return nullptr;
        return Lisp::mkReal(cp.distAtParam(t));
    });
    Lr.defineBuiltin("VLAX-CURVE-GETPARAMATDIST", [curve](Lisp&, A& a) -> V {
        const ICurve* c = curve(a.at(0));
        const double d = Lisp::num(a.at(1), "");
        if (d < -1e-9 || d > c->length() + 1e-9) return nullptr;
        return Lisp::mkReal(CurveParam(c).paramAtDist(d));
    });
    Lr.defineBuiltin("VLAX-CURVE-GETPOINTATDIST", [curve](Lisp&, A& a) -> V {
        const ICurve* c = curve(a.at(0));
        const double d = Lisp::num(a.at(1), "");
        if (d < -1e-9 || d > c->length() + 1e-9) return nullptr;
        return pt3(c->pointAtDistance(d));
    });
    Lr.defineBuiltin("VLAX-CURVE-GETPOINTATPARAM", [curve](Lisp&, A& a) -> V {
        const ICurve* c = curve(a.at(0));
        CurveParam cp(c);
        const double t = Lisp::num(a.at(1), "");
        if (t < cp.startParam() - 1e-9 || t > cp.endParam() + 1e-9) return nullptr;
        return pt3(c->pointAtDistance(cp.distAtParam(t)));
    });
    Lr.defineBuiltin("VLAX-CURVE-GETDISTATPOINT", [curve, ptArg](Lisp&, A& a) -> V {
        const ICurve* c = curve(a.at(0));
        const Vec3 p = ptArg(a.at(1));
        const double d = CurveParam(c).distAtPoint(p);
        if ((c->pointAtDistance(d) - p).length() > 1e-6 * std::max(1.0, c->length())) return nullptr;
        return Lisp::mkReal(d);
    });
    Lr.defineBuiltin("VLAX-CURVE-GETPARAMATPOINT", [curve, ptArg](Lisp&, A& a) -> V {
        const ICurve* c = curve(a.at(0));
        const Vec3 p = ptArg(a.at(1));
        CurveParam cp(c);
        const double d = cp.distAtPoint(p);
        if ((c->pointAtDistance(d) - p).length() > 1e-6 * std::max(1.0, c->length())) return nullptr;
        return Lisp::mkReal(cp.paramAtDist(d));
    });
    Lr.defineBuiltin("VLAX-CURVE-GETCLOSESTPOINTTO", [curve, ptArg](Lisp&, A& a) {
        const ICurve* c = curve(a.at(0));
        return pt3(c->pointAtDistance(CurveParam(c).distAtPoint(ptArg(a.at(1)))));
    });
    Lr.defineBuiltin("VLAX-CURVE-GETCLOSESTPOINTTOPROJECTION", [curve, ptArg](Lisp&, A& a) {
        const ICurve* c = curve(a.at(0));
        return pt3(c->pointAtDistance(CurveParam(c).distAtPoint(ptArg(a.at(1)))));
    });
    Lr.defineBuiltin("VLAX-CURVE-GETAREA", [this, idOf](Lisp&, A& a) {
        const IEntity* e = in.doc_.find(static_cast<EntityId>(idOf(a.at(0))));
        if (auto* ci = dynamic_cast<const CircleEntity*>(e)) return Lisp::mkReal(ci->area());
        if (auto* p = dynamic_cast<const PolylineEntity*>(e)) return Lisp::mkReal(p->area());
        return Lisp::mkReal(0);
    });
    auto deriv = [curve](int order) {
        return [curve, order](Lisp&, A& a) -> V {
            const ICurve* c = curve(a.at(0));
            CurveParam cp(c);
            const double t = Lisp::num(a.at(1), ""), h = std::max(1e-6, (cp.endParam() - cp.startParam()) * 1e-5);
            auto at = [&](double u) { u = std::clamp(u, cp.startParam(), cp.endParam()); return c->pointAtDistance(cp.distAtParam(u)); };
            const Vec3 p0 = at(t - h), p1 = at(t), p2 = at(t + h);
            const Vec3 d = order == 1 ? (p2 - p0) * (1.0 / (2 * h)) : (p2 - p1 * 2.0 + p0) * (1.0 / (h * h));
            return pt3(d);
        };
    };
    Lr.defineBuiltin("VLAX-CURVE-GETFIRSTDERIV", deriv(1));
    Lr.defineBuiltin("VLAX-CURVE-GETSECONDDERIV", deriv(2));
    (void)Lr;
}

// ------------------------------------------------------------------ Interpreter qismi
LispCtx& Interpreter::lispCtx() {
    if (!lisp_) lisp_ = std::make_shared<LispCtx>(*this);
    lisp_->dcl.ui = dclUi;
    return *lisp_;
}

std::vector<std::string> Interpreter::lispCommands() const { return lisp_ ? lisp_->L->commands() : std::vector<std::string>{}; }

// Qavslar muvozanati (satr va izohlarni hisobga olib)
static int parenDepth(const std::string& s) {
    int d = 0;
    bool str = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (str) {
            if (c == '\\') ++i;
            else if (c == '"') str = false;
            continue;
        }
        if (c == '"') str = true;
        else if (c == ';') { while (i < s.size() && s[i] != '\n') ++i; }
        else if (c == '(') ++d;
        else if (c == ')') --d;
    }
    return d;
}

void Interpreter::reportLispWarnings(std::size_t before) {
    auto& c = lispCtx();
    c.newline();
    for (std::size_t k = before; k < c.L->warnings.size(); ++k) say("msg.lisp_unsupported", {c.L->warnings[k]});
}

bool Interpreter::runLispLine(const std::string& raw) {
    auto& c = lispCtx();
    std::string src = c.pending.empty() ? raw : c.pending + "\n" + raw;
    std::size_t b = src.find_first_not_of(" \t\r\n");
    if (c.pending.empty() && (b == std::string::npos || (src[b] != '(' && src[b] != '!'))) return false;
    if (parenDepth(src) > 0) {
        c.pending = src;
        return true;
    }
    c.pending.clear();
    if (src[b] == '!') src = src.substr(b + 1);  // !var — qiymatni ko'rsatish
    const std::size_t w = c.L->warnings.size();
    lastOk_ = true;
    try {
        V r = c.L->evalString(src);
        c.newline();
        if (!(r && r->kind == Kind::Sym && r->s.empty())) out_ << c.L->toString(r, true) << "\n";
    } catch (const lisp::LispExit&) {
    } catch (const LispError& e) {
        lastOk_ = false;
        c.newline();
        if (!c.L->takeErrorHandled()) say("err.prefix", {loc_.tr("err.lisp", {e.what()})});
    }
    reportLispWarnings(w);
    return true;
}

bool Interpreter::runLispCommand(const std::string& name, const Args& a) {
    if (!lisp_) return false;
    std::string n = name;
    while (!n.empty() && (n[0] == '_' || n[0] == '.')) n.erase(0, 1);
    if (!lisp_->L->hasCommand(n)) return false;
    auto& c = lispCtx();
    c.input.assign(a.begin(), a.end());  // so'rovlarga javoblar (get*)
    const std::size_t w = c.L->warnings.size();
    const std::size_t before = stack_.undoCount();
    const std::size_t pushes = stack_.pushCount();
    try {
        c.L->callCommand(n);
    } catch (const lisp::LispExit&) {
    } catch (const LispError& e) {
        lastOk_ = false;
        say("err.prefix", {loc_.tr("err.lisp", {e.what()})});
    }
    // bitta LISP buyrug'i — bitta undo qadami
    if (stack_.pushCount() > pushes && stack_.undoCount() > before + 1) stack_.squash(stack_.undoCount() - before);
    c.input.clear();
    reportLispWarnings(w);
    return true;
}

bool Interpreter::dispatchLisp(const std::string& cmd, const Args& a) {
    if (cmd == "APPLOAD") {
        auto& c = lispCtx();
        if (a.empty()) {
            say("msg.lisp_list", {std::to_string(c.loaded.size()), [&] {
                                      std::string s;
                                      for (auto& x : c.L->commands()) s += (s.empty() ? "" : ", ") + x;
                                      return s.empty() ? std::string("-") : s;
                                  }()});
            return true;
        }
        for (const auto& f : a) {
            const std::size_t w = c.L->warnings.size();
            const auto cmdsBefore = c.L->commands();
            std::error_code ec;
            const std::string dir = fs::absolute(f, ec).parent_path().string();
            if (!dir.empty() && std::find(c.searchDirs.begin(), c.searchDirs.end(), dir) == c.searchDirs.end()) c.searchDirs.push_back(dir);
            try {
                c.L->loadFile(f);
            } catch (const lisp::LispExit&) {
            } catch (const LispError& e) {
                reportLispWarnings(w);
                throw Error("err.lisp", {std::string(e.what())});
            }
            c.newline();
            c.loaded.push_back(f);
            std::string added;
            for (auto& x : c.L->commands())
                if (std::find(cmdsBefore.begin(), cmdsBefore.end(), x) == cmdsBefore.end()) added += (added.empty() ? "" : ", ") + x;
            say("msg.lisp_loaded", {fs::path(f).filename().string(), added.empty() ? "-" : added});
            reportLispWarnings(w);
        }
        return true;
    }
    if (cmd == "LISP") {
        need(a, 1, cmd);
        std::string src;
        for (const auto& t : a) src += (src.empty() ? "" : " ") + t;
        auto& c = lispCtx();
        c.pending.clear();
        if (!runLispLine(src)) {
            const std::size_t w = c.L->warnings.size();
            try {
                out_ << c.L->toString(c.L->evalString(src), true) << "\n";
            } catch (const LispError& e) {
                reportLispWarnings(w);
                throw Error("err.lisp", {std::string(e.what())});
            }
        }
        return true;
    }
    return false;
}

}  // namespace asiacad::cli
