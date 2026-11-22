// AsiaCAD — arxitektura: aqlli devorlar, eshik/deraza, xonalar, kesim/fasad ko'rinishi.
#include <asiacad/bim/arch.h>

#include <asiacad/bim/ifc.h>
#include <asiacad/core/entities.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <sstream>

namespace asiacad::bim {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-6;

Vec3 unit2(const Vec3& v) {
    const double l = std::hypot(v.x, v.y);
    return l < 1e-15 ? Vec3{1, 0, 0} : Vec3{v.x / l, v.y / l, 0};
}
Vec3 leftN(const Vec3& d) { return {-d.y, d.x, 0}; }
double cross2(const Vec3& a, const Vec3& b) { return a.x * b.y - a.y * b.x; }
double dot2(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y; }
// p + d·s va q + e·u chiziqlari kesishishi
bool lineX(const Vec3& p, const Vec3& d, const Vec3& q, const Vec3& e, Vec3& out) {
    const double den = cross2(d, e);
    if (std::abs(den) < 1e-9) return false;
    const double s = cross2(q - p, e) / den;
    out = p + d * s;
    out.z = 0;
    return true;
}
std::string f6(double v) {
    std::string s = std::format("{:.6f}", v);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}
std::vector<double> nums(const std::string& s, char sep) {
    std::vector<double> v;
    std::stringstream ss(s);
    std::string t;
    while (std::getline(ss, t, sep)) {
        try { v.push_back(std::stod(t)); } catch (...) { v.push_back(0); }
    }
    return v;
}
std::unique_ptr<IEntity> tag(std::unique_ptr<IEntity> e, const ArchWall& w, const std::string& layer, const std::string& part) {
    e->setLayer(layer);
    e->xset("aw.wall", w.id);
    e->xset("aw.def", wallDef(w));
    e->xset("aw.ops", opsDef(w.ops));
    e->xset("aw.part", part);
    return e;
}
}  // namespace

double polygonArea(const std::vector<Vec3>& p) {
    double a = 0;
    for (std::size_t i = 0; i < p.size(); ++i) a += cross2(p[i], p[(i + 1) % p.size()]);
    return a / 2;
}

bool pointInPolygon(const std::vector<Vec3>& poly, const Vec3& p) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec3 &a = poly[i], &b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

bool roomRentableByType(const std::string& t0) {
    std::string t;
    for (char c : t0) t += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const std::set<std::string> yes{"LIVING", "BEDROOM", "OFFICE", "RETAIL", "SHOP", "HOTEL", "APARTMENT", "ROOM", "KITCHEN", "CAFE", "STORE"};
    return yes.count(t) > 0;
}

std::vector<ArchWall> makeWallChain(const std::vector<Vec3>& pts0, double t, double h, char just, bool closed, const std::string& idPrefix) {
    std::vector<Vec3> pts;
    for (const auto& p : pts0)
        if (pts.empty() || std::hypot(p.x - pts.back().x, p.y - pts.back().y) > kEps) pts.push_back({p.x, p.y, 0});
    if (closed && pts.size() > 2 && std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y) < kEps) pts.pop_back();
    std::vector<ArchWall> ws;
    const std::size_t n = pts.size();
    if (n < 2) return ws;
    const std::size_t segs = closed && n > 2 ? n : n - 1;
    for (std::size_t i = 0; i < segs; ++i) {
        ArchWall w;
        w.id = idPrefix + std::to_string(i + 1);
        w.a = pts[i];
        w.b = pts[(i + 1) % n];
        w.t = t;
        w.h = h;
        w.just = just;
        const Vec3 d = unit2(w.b - w.a), nl = leftN(d);
        w.la = w.a + nl * w.leftOff();
        w.ra = w.a - nl * w.rightOff();
        w.lb = w.b + nl * w.leftOff();
        w.rb = w.b - nl * w.rightOff();
        ws.push_back(w);
    }
    // tutashuvlar: qo'shni devorlar yuzlari chiziqlari kesishgan joyda kesiladi
    auto joint = [&](ArchWall& p, ArchWall& q) {  // p.b == q.a
        const Vec3 dp = unit2(p.b - p.a), dq = unit2(q.b - q.a);
        if (std::abs(cross2(dp, dq)) < 1e-6) { p.jb = q.ja = true; return; }  // to'g'ri davom
        Vec3 x;
        p.jb = q.ja = true;
        if (lineX(p.la, dp, q.la, dq, x) && x.distanceTo(p.b) < 10 * (p.t + q.t)) { p.lb = x; q.la = x; }
        if (lineX(p.ra, dp, q.ra, dq, x) && x.distanceTo(p.b) < 10 * (p.t + q.t)) { p.rb = x; q.ra = x; }
    };
    for (std::size_t i = 0; i + 1 < ws.size(); ++i) joint(ws[i], ws[i + 1]);
    if (closed && ws.size() > 2) joint(ws.back(), ws.front());
    return ws;
}

std::string wallDef(const ArchWall& w) {
    std::string s;
    for (double v : {w.a.x, w.a.y, w.b.x, w.b.y, w.t, w.h}) s += f6(v) + ",";
    s += std::string(1, w.just);
    for (const Vec3* p : {&w.la, &w.ra, &w.lb, &w.rb}) s += "," + f6(p->x) + "," + f6(p->y);
    s += std::string(",") + (w.ja ? "1" : "0") + "," + (w.jb ? "1" : "0");
    return s;
}

std::string opsDef(const std::vector<ArchOpening>& ops) {
    std::string s;
    for (const auto& o : ops)
        s += (s.empty() ? "" : ";") + std::string(1, o.kind) + ":" + f6(o.pos) + ":" + f6(o.w) + ":" + f6(o.h) + ":" + f6(o.sill) + ":" + (o.right ? "1" : "0") +
             ":" + (o.out ? "1" : "0");
    return s;
}

bool parseWall(const std::string& def, const std::string& ops, ArchWall& w) {
    std::vector<std::string> f;
    std::stringstream ss(def);
    std::string t;
    while (std::getline(ss, t, ',')) f.push_back(t);
    if (f.size() != 15 && f.size() != 17) return false;
    auto d = [&](std::size_t i) { try { return std::stod(f[i]); } catch (...) { return 0.0; } };
    w.a = {d(0), d(1), 0};
    w.b = {d(2), d(3), 0};
    w.t = d(4);
    w.h = d(5);
    w.just = f[6].empty() ? 'C' : f[6][0];
    w.la = {d(7), d(8), 0};
    w.ra = {d(9), d(10), 0};
    w.lb = {d(11), d(12), 0};
    w.rb = {d(13), d(14), 0};
    w.ja = f.size() == 17 && f[15] == "1";
    w.jb = f.size() == 17 && f[16] == "1";
    w.ops.clear();
    std::stringstream so(ops);
    while (std::getline(so, t, ';')) {
        if (t.size() < 3) continue;
        ArchOpening o;
        o.kind = t[0];
        const auto v = nums(t.substr(2), ':');
        if (v.size() < 4) continue;
        o.pos = v[0]; o.w = v[1]; o.h = v[2]; o.sill = v[3];
        if (v.size() > 4) o.right = v[4] != 0;
        if (v.size() > 5) o.out = v[5] != 0;
        w.ops.push_back(o);
    }
    return w.t > 0 && w.length() > kEps;
}

std::vector<std::unique_ptr<IEntity>> wallEntities(const ArchWall& w0) {
    ArchWall w = w0;
    std::sort(w.ops.begin(), w.ops.end(), [](const ArchOpening& a, const ArchOpening& b) { return a.pos < b.pos; });
    std::vector<std::unique_ptr<IEntity>> out;
    const double L = w.length();
    const Vec3 d = unit2(w.b - w.a), nl = leftN(d);
    const double lo = w.leftOff(), ro = w.rightOff();
    auto A = [&](double s, double off) { Vec3 p = w.a + d * s + nl * off; p.z = 0; return p; };
    auto Lp = [&](double s) { return s <= kEps ? w.la : s >= L - kEps ? w.lb : A(s, lo); };
    auto Rp = [&](double s) { return s <= kEps ? w.ra : s >= L - kEps ? w.rb : A(s, -ro); };
    // yaxlit bo'laklar
    std::vector<std::pair<double, double>> gaps;
    for (const auto& o : w.ops) gaps.emplace_back(std::clamp(o.pos - o.w / 2, 0.0, L), std::clamp(o.pos + o.w / 2, 0.0, L));
    double s = 0;
    auto solid = [&](double u0, double u1) {
        if (u1 - u0 < kEps) return;
        // yuzlar; yopuvchi chiziq faqat erkin uchda va teshik yonida (tutashuvda chizilmaydi)
        const bool capA = u0 > kEps || !w.ja, capB = u1 < L - kEps || !w.jb;
        std::vector<Vec3> poly;
        if (capA && capB) {
            out.push_back(tag(std::make_unique<PolylineEntity>(std::vector<Vec3>{Lp(u0), Lp(u1), Rp(u1), Rp(u0)}, true), w, "A-WALL", "body"));
            return;
        }
        if (capA) poly = {Lp(u1), Lp(u0), Rp(u0), Rp(u1)};
        else if (capB) poly = {Lp(u0), Lp(u1), Rp(u1), Rp(u0)};
        if (!poly.empty()) { out.push_back(tag(std::make_unique<PolylineEntity>(std::move(poly), false), w, "A-WALL", "body")); return; }
        out.push_back(tag(std::make_unique<LineEntity>(Lp(u0), Lp(u1)), w, "A-WALL", "body"));
        out.push_back(tag(std::make_unique<LineEntity>(Rp(u0), Rp(u1)), w, "A-WALL", "body"));
    };
    for (const auto& [g0, g1] : gaps) {
        solid(s, g0);
        s = std::max(s, g1);
    }
    solid(s, L);
    // belgilar
    for (const auto& o : w.ops) {
        const double s0 = std::clamp(o.pos - o.w / 2, 0.0, L), s1 = std::clamp(o.pos + o.w / 2, 0.0, L);
        if (o.kind == 'W') {
            const double mid = (lo - ro) / 2, g = std::min(0.03, w.t / 8);
            for (double off : {lo, -ro, mid - g, mid + g})
                out.push_back(tag(std::make_unique<LineEntity>(A(s0, off), A(s1, off)), w, "A-WINDOW", "window"));
            continue;
        }
        // eshik: tabaqa va 90° ochilish yoyi
        const double face = o.out ? -ro : lo;
        const Vec3 side = o.out ? nl * -1.0 : nl;
        const double sh = o.right ? s1 : s0, so = o.right ? s0 : s1;
        const Vec3 H = A(sh, face), J = A(so, face);
        const double r = std::abs(s1 - s0);
        const Vec3 E = H + side * r;
        out.push_back(tag(std::make_unique<LineEntity>(H, E), w, "A-DOOR", "door"));
        double a1 = std::atan2(E.y - H.y, E.x - H.x), a2 = std::atan2(J.y - H.y, J.x - H.x);
        double sweep = std::fmod(a2 - a1 + 4 * kPi, 2 * kPi);
        if (std::abs(sweep - kPi / 2) > 1e-3) std::swap(a1, a2);
        out.push_back(tag(std::make_unique<ArcEntity>(H, r, a1, a2), w, "A-DOOR", "door"));
    }
    return out;
}

bool trimEndTo(ArchWall& w, bool atB, const ArchWall& o) {
    const Vec3 E = atB ? w.b : w.a, F = atB ? w.a : w.b;
    const Vec3 dO = unit2(o.b - o.a);
    const double L = o.length();
    const double s = dot2(E - o.a, dO);
    if (s < 0.01 || s > L - 0.01) return false;           // o ning uchida — oddiy burchak (bu yerda emas)
    const double off = cross2(dO, E - o.a);                 // o'qdan chapga masofa (ishorali)
    if (off > o.leftOff() + 1e-6 || off < -o.rightOff() - 1e-6) return false;  // o tanasida emas
    const double side = cross2(dO, F - o.a);
    if (std::abs(side) < 1e-9) return false;                // o bilan bir chiziqda
    const Vec3 nO = leftN(dO);
    const Vec3 face = side > 0 ? o.a + nO * o.leftOff() : o.a - nO * o.rightOff();
    const Vec3 dW = unit2(w.b - w.a), nW = leftN(dW);
    Vec3 xl, xr;
    if (!lineX(w.a + nW * w.leftOff(), dW, face, dO, xl) || !lineX(w.a - nW * w.rightOff(), dW, face, dO, xr)) return false;
    if (atB) { w.lb = xl; w.rb = xr; w.jb = true; }
    else { w.la = xl; w.ra = xr; w.ja = true; }
    return true;
}

std::vector<std::size_t> joinWalls(std::vector<ArchWall>& fresh, std::vector<ArchWall>& existing) {
    std::vector<std::size_t> changed;
    for (auto& w : fresh) {
        for (bool atB : {false, true}) {
            if (atB ? w.jb : w.ja) continue;
            for (const auto& o : existing) if (trimEndTo(w, atB, o)) break;
            if (atB ? w.jb : w.ja) continue;
            for (const auto& o : fresh) if (&o != &w && trimEndTo(w, atB, o)) break;
        }
    }
    for (std::size_t i = 0; i < existing.size(); ++i) {
        auto& w = existing[i];
        bool ch = false;
        for (bool atB : {false, true}) {
            if (atB ? w.jb : w.ja) continue;
            for (const auto& o : fresh) if (trimEndTo(w, atB, o)) { ch = true; break; }
        }
        if (ch) changed.push_back(i);
    }
    return changed;
}

std::vector<ArchWall> wallsFromDoc(const Document& doc) {
    std::map<std::string, ArchWall> m;
    std::vector<std::string> order;
    doc.forEach([&](const IEntity& e) {
        const std::string id = e.xget("aw.wall");
        if (id.empty() || m.count(id)) return;
        ArchWall w;
        if (!parseWall(e.xget("aw.def"), e.xget("aw.ops"), w)) return;
        w.id = id;
        m[id] = w;
        order.push_back(id);
    });
    std::vector<ArchWall> v;
    for (auto& id : order) v.push_back(m[id]);
    return v;
}

int nearestWall(const std::vector<ArchWall>& ws, const Vec3& p, double maxDist, double* along) {
    int best = -1;
    double bd = maxDist;
    for (std::size_t i = 0; i < ws.size(); ++i) {
        const auto& w = ws[i];
        const Vec3 d = unit2(w.b - w.a);
        const double L = w.length();
        const double s = std::clamp(dot2(p - w.a, d), 0.0, L);
        const Vec3 q = w.a + d * s;
        // o'q emas, devor tanasi markazidan masofa
        const Vec3 c = q + leftN(d) * ((w.leftOff() - w.rightOff()) / 2);
        const double dist = std::hypot(p.x - c.x, p.y - c.y);
        if (dist <= bd) { bd = dist; best = static_cast<int>(i); if (along) *along = s; }
    }
    return best;
}

bool openingFits(const ArchWall& w, const ArchOpening& o, std::string* why) {
    const double L = w.length(), m = 0.05;
    if (o.w <= 0 || o.h <= 0) { if (why) *why = "size"; return false; }
    if (o.pos - o.w / 2 < m - 1e-9 || o.pos + o.w / 2 > L - m + 1e-9) { if (why) *why = "end"; return false; }
    if (o.kind == 'W' && o.sill + o.h > w.h + 1e-9) { if (why) *why = "height"; return false; }
    if (o.kind == 'D' && o.h > w.h + 1e-9) { if (why) *why = "height"; return false; }
    for (const auto& q : w.ops)
        if (std::abs(q.pos - o.pos) < (q.w + o.w) / 2 + 0.1 - 1e-9) { if (why) *why = "overlap"; return false; }
    return true;
}

// ------------------------------------------------------------------ xonalar: o'qlar tekis grafi
namespace {
struct GEdge { int u, v; double offLeft; };  // offLeft — u→v yo'nalishida chap yuzgacha
struct Graph {
    std::vector<Vec3> P;
    std::vector<GEdge> E;   // yo'naltirilgan yarim qirralar (juft-juft: 2k, 2k+1)
};
Graph buildGraph(const std::vector<ArchWall>& ws) {
    Graph g;
    auto vid = [&](const Vec3& p) {
        for (std::size_t i = 0; i < g.P.size(); ++i) if (std::hypot(g.P[i].x - p.x, g.P[i].y - p.y) < 1e-4) return static_cast<int>(i);
        g.P.push_back({p.x, p.y, 0});
        return static_cast<int>(g.P.size() - 1);
    };
    // har devor o'qini boshqa uchlar va kesishishlar bo'yicha bo'lish
    for (std::size_t i = 0; i < ws.size(); ++i) {
        const auto& w = ws[i];
        const Vec3 d = unit2(w.b - w.a);
        const double L = w.length();
        std::vector<double> cuts{0, L};
        for (std::size_t j = 0; j < ws.size(); ++j) {
            if (j == i) continue;
            const auto& o = ws[j];
            for (const Vec3& q : {o.a, o.b}) {
                const double s = dot2(q - w.a, d);
                if (s > 1e-4 && s < L - 1e-4 && std::abs(cross2(d, q - w.a)) < 1e-4) cuts.push_back(s);
            }
            Vec3 x;
            const Vec3 e = unit2(o.b - o.a);
            if (lineX(w.a, d, o.a, e, x)) {
                const double s = dot2(x - w.a, d), u = dot2(x - o.a, e);
                if (s > 1e-4 && s < L - 1e-4 && u > -1e-4 && u < o.length() + 1e-4) cuts.push_back(s);
            }
        }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
            if (cuts[k + 1] - cuts[k] < 1e-4) continue;
            const int u = vid(w.a + d * cuts[k]), v = vid(w.a + d * cuts[k + 1]);
            if (u == v) continue;
            bool dup = false;
            for (std::size_t q = 0; q < g.E.size(); q += 2) if ((g.E[q].u == u && g.E[q].v == v) || (g.E[q].u == v && g.E[q].v == u)) dup = true;
            if (dup) continue;
            g.E.push_back({u, v, w.leftOff()});
            g.E.push_back({v, u, w.rightOff()});
        }
    }
    // osilib qolgan qirralarni olib tashlash (daraja 1)
    for (bool changed = true; changed;) {
        changed = false;
        std::vector<int> deg(g.P.size(), 0);
        for (std::size_t q = 0; q < g.E.size(); q += 2) { ++deg[static_cast<std::size_t>(g.E[q].u)]; ++deg[static_cast<std::size_t>(g.E[q].v)]; }
        std::vector<GEdge> keep;
        for (std::size_t q = 0; q < g.E.size(); q += 2)
            if (deg[static_cast<std::size_t>(g.E[q].u)] > 1 && deg[static_cast<std::size_t>(g.E[q].v)] > 1) { keep.push_back(g.E[q]); keep.push_back(g.E[q + 1]); }
            else changed = true;
        g.E = std::move(keep);
    }
    return g;
}
struct Face { std::vector<int> he; double area = 0; std::vector<Vec3> poly; };
std::vector<Face> faces(const Graph& g) {
    std::map<int, std::vector<int>> out;  // tugun -> chiquvchi yarim qirralar (burchak bo'yicha)
    for (std::size_t i = 0; i < g.E.size(); ++i) out[g.E[i].u].push_back(static_cast<int>(i));
    auto ang = [&](int h) { const Vec3 d = g.P[static_cast<std::size_t>(g.E[static_cast<std::size_t>(h)].v)] - g.P[static_cast<std::size_t>(g.E[static_cast<std::size_t>(h)].u)]; return std::atan2(d.y, d.x); };
    for (auto& [v, l] : out) std::sort(l.begin(), l.end(), [&](int a, int b) { return ang(a) < ang(b); });
    std::vector<char> seen(g.E.size(), 0);
    std::vector<Face> fs;
    for (std::size_t s = 0; s < g.E.size(); ++s) {
        if (seen[s]) continue;
        Face f;
        int h = static_cast<int>(s);
        for (int guard = 0; guard < 100000 && !seen[static_cast<std::size_t>(h)]; ++guard) {
            seen[static_cast<std::size_t>(h)] = 1;
            f.he.push_back(h);
            f.poly.push_back(g.P[static_cast<std::size_t>(g.E[static_cast<std::size_t>(h)].u)]);
            const int twin = h ^ 1;
            const auto& l = out[g.E[static_cast<std::size_t>(h)].v];
            const auto it = std::find(l.begin(), l.end(), twin);
            const std::size_t k = static_cast<std::size_t>(it - l.begin());
            h = l[(k + l.size() - 1) % l.size()];  // soat strelkasi bo'yicha keyingisi — yuz chapda qoladi
        }
        f.area = polygonArea(f.poly);
        fs.push_back(std::move(f));
    }
    return fs;
}
// Yuz qirralarini chap tomonga (har biri o'z qalinligi bilan) surib, ichki kontur
std::vector<Vec3> offsetFace(const Graph& g, const Face& f) {
    const std::size_t n = f.he.size();
    std::vector<Vec3> res;
    for (std::size_t i = 0; i < n; ++i) {
        const GEdge& e0 = g.E[static_cast<std::size_t>(f.he[(i + n - 1) % n])];
        const GEdge& e1 = g.E[static_cast<std::size_t>(f.he[i])];
        const Vec3 a0 = g.P[static_cast<std::size_t>(e0.u)], b0 = g.P[static_cast<std::size_t>(e0.v)];
        const Vec3 a1 = g.P[static_cast<std::size_t>(e1.u)], b1 = g.P[static_cast<std::size_t>(e1.v)];
        const Vec3 d0 = unit2(b0 - a0), d1 = unit2(b1 - a1);
        const Vec3 p0 = a0 + leftN(d0) * e0.offLeft, p1 = a1 + leftN(d1) * e1.offLeft;
        Vec3 x;
        if (std::abs(cross2(d0, d1)) < 1e-9 || !lineX(p0, d0, p1, d1, x)) x = a1 + leftN(d1) * e1.offLeft;
        res.push_back(x);
    }
    return res;
}
RoomResult faceResult(const Graph& g, const Face& f) {
    RoomResult r;
    r.ok = true;
    r.axis = f.poly;
    r.net = offsetFace(g, f);
    r.axisArea = std::abs(f.area);
    r.netArea = std::abs(polygonArea(r.net));
    for (std::size_t i = 0; i < r.net.size(); ++i) r.perimeter += r.net[i].distanceTo(r.net[(i + 1) % r.net.size()]);
    // markaz (netto kontur og'irlik markazi)
    double a = 0, cx = 0, cy = 0;
    for (std::size_t i = 0; i < r.net.size(); ++i) {
        const Vec3 &p = r.net[i], &q = r.net[(i + 1) % r.net.size()];
        const double c = p.x * q.y - q.x * p.y;
        a += c; cx += (p.x + q.x) * c; cy += (p.y + q.y) * c;
    }
    if (std::abs(a) > 1e-12) r.center = {cx / (3 * a), cy / (3 * a), 0};
    return r;
}
}  // namespace

RoomResult findRoom(const std::vector<ArchWall>& ws, const Vec3& p) {
    const Graph g = buildGraph(ws);
    const auto fs = faces(g);
    const Face* best = nullptr;
    for (const auto& f : fs)
        if (f.area > 1e-9 && pointInPolygon(f.poly, p) && (!best || f.area < best->area)) best = &f;
    if (!best) return {};
    RoomResult r = faceResult(g, *best);
    if (!pointInPolygon(r.net, r.center)) r.center = p;
    return r;
}

RoomResult buildingOutline(const std::vector<ArchWall>& ws) {
    const Graph g = buildGraph(ws);
    const auto fs = faces(g);
    const Face* best = nullptr;
    for (const auto& f : fs) if (f.area < -1e-9 && (!best || f.area < best->area)) best = &f;
    if (!best) return {};
    RoomResult r = faceResult(g, *best);
    std::reverse(r.axis.begin(), r.axis.end());
    std::reverse(r.net.begin(), r.net.end());
    return r;
}

std::vector<std::unique_ptr<IEntity>> archMeshes(const std::vector<ArchWall>& ws, int floors, double slab) {
    std::vector<std::unique_ptr<IEntity>> out;
    floors = std::max(1, floors);
    double H = 0;
    for (const auto& w : ws) H = std::max(H, w.h);
    const double step = H + slab;
    std::vector<Vec3> V;
    std::vector<MeshEntity::Face> F;
    auto box = [&](const Vec3& l0, const Vec3& l1, const Vec3& r1, const Vec3& r0, double z0, double z1) {
        if (z1 - z0 < 1e-6) return;
        const int b = static_cast<int>(V.size());
        for (const Vec3& p : {l0, l1, r1, r0}) V.push_back({p.x, p.y, z0});
        for (const Vec3& p : {l0, l1, r1, r0}) V.push_back({p.x, p.y, z1});
        auto q = [&](int a, int c, int d, int e) { MeshEntity::Face f; f.a = b + a; f.b = b + c; f.c = b + d; f.d = b + e; F.push_back(f); };
        q(0, 3, 2, 1); q(4, 5, 6, 7); q(0, 1, 5, 4); q(1, 2, 6, 5); q(2, 3, 7, 6); q(3, 0, 4, 7);
    };
    for (int fl = 0; fl < floors; ++fl) {
        const double z = fl * step;
        for (const auto& w0 : ws) {
            ArchWall w = w0;
            std::sort(w.ops.begin(), w.ops.end(), [](const ArchOpening& a, const ArchOpening& b) { return a.pos < b.pos; });
            const double L = w.length();
            const Vec3 d = unit2(w.b - w.a), nl = leftN(d);
            auto Lp = [&](double s) { return s <= kEps ? w.la : s >= L - kEps ? w.lb : w.a + d * s + nl * w.leftOff(); };
            auto Rp = [&](double s) { return s <= kEps ? w.ra : s >= L - kEps ? w.rb : w.a + d * s - nl * w.rightOff(); };
            auto piece = [&](double u0, double u1, double z0, double z1) { if (u1 - u0 > kEps) box(Lp(u0), Lp(u1), Rp(u1), Rp(u0), z + z0, z + z1); };
            double s = 0;
            for (const auto& o : w.ops) {
                const double s0 = std::clamp(o.pos - o.w / 2, 0.0, L), s1 = std::clamp(o.pos + o.w / 2, 0.0, L);
                piece(s, s0, 0, w.h);
                if (o.kind == 'D') piece(s0, s1, o.h, w.h);
                else { piece(s0, s1, 0, o.sill); piece(s0, s1, o.sill + o.h, w.h); }
                s = std::max(s, s1);
            }
            piece(s, L, 0, w.h);
        }
    }
    if (!F.empty()) {
        auto m = std::make_unique<MeshEntity>(std::move(V), std::move(F));
        m->setLayer("A-3D-WALL");
        out.push_back(std::move(m));
    }
    // plitalar: bino tashqi konturi bo'yicha (har qavat ostida va tomda)
    const RoomResult outer = buildingOutline(ws);
    if (outer.ok && outer.net.size() >= 3) {
        const auto tris = triangulatePolygon(outer.net, {});
        std::vector<Vec3> SV;
        std::vector<MeshEntity::Face> SF;
        for (int fl = 0; fl <= floors; ++fl) {
            const double zt = fl * step, zb = zt - slab;
            for (const auto& t : tris) {
                const int b = static_cast<int>(SV.size());
                for (const auto& p : t) SV.push_back({p.x, p.y, zt});
                for (const auto& p : t) SV.push_back({p.x, p.y, zb});
                MeshEntity::Face top; top.a = b; top.b = b + 1; top.c = b + 2;
                MeshEntity::Face bot; bot.a = b + 3; bot.b = b + 5; bot.c = b + 4;
                SF.push_back(top);
                SF.push_back(bot);
            }
            const std::size_t n = outer.net.size();
            for (std::size_t i = 0; i < n; ++i) {
                const Vec3 &p = outer.net[i], &q = outer.net[(i + 1) % n];
                const int b = static_cast<int>(SV.size());
                SV.push_back({p.x, p.y, zb}); SV.push_back({q.x, q.y, zb}); SV.push_back({q.x, q.y, zt}); SV.push_back({p.x, p.y, zt});
                MeshEntity::Face f; f.a = b; f.b = b + 1; f.c = b + 2; f.d = b + 3;
                SF.push_back(f);
            }
        }
        auto m = std::make_unique<MeshEntity>(std::move(SV), std::move(SF));
        m->setLayer("A-3D-SLAB");
        out.push_back(std::move(m));
    }
    return out;
}

// ------------------------------------------------------------------ kesim / fasad
std::vector<std::unique_ptr<IEntity>> sectionEntities(const std::vector<ArchWall>& ws, const Vec3& p1, const Vec3& p2, const Vec3& origin,
                                                      const SectionOptions& o, int* cutCount) {
    std::vector<std::unique_ptr<IEntity>> out;
    const Vec3 u = unit2(p2 - p1), nv = leftN(u);
    const double Ls = std::hypot(p2.x - p1.x, p2.y - p1.y);
    struct Cut { double s0, s1; const ArchWall* w; double q; };
    struct Far { double s0, s1, depth; const ArchWall* w; };
    std::vector<Cut> cuts;
    std::vector<Far> fars;
    for (const auto& w : ws) {
        const Vec3 d = unit2(w.b - w.a);
        Vec3 x;
        const double sn = std::abs(cross2(u, d));
        bool cut = false;
        if (sn > 0.1 && lineX(p1, u, w.a, d, x)) {
            const double s = dot2(x - p1, u), q = dot2(x - w.a, d);
            if (s >= -1e-9 && s <= Ls + 1e-9 && q >= -1e-6 && q <= w.length() + 1e-6) {
                const Vec3 c = x + leftN(d) * ((w.leftOff() - w.rightOff()) / 2);
                const double sc = dot2(c - p1, u), wt = w.t / sn;
                cuts.push_back({sc - wt / 2, sc + wt / 2, &w, q});
                cut = true;
            }
        }
        if (cut) continue;
        double s0 = 1e300, s1 = -1e300, dep = 1e300, depMax = -1e300;
        for (const Vec3& c : {w.la, w.ra, w.lb, w.rb}) {
            const double s = dot2(c - p1, u), dd = dot2(c - p1, nv);
            s0 = std::min(s0, s); s1 = std::max(s1, s);
            dep = std::min(dep, dd); depMax = std::max(depMax, dd);
        }
        if (depMax <= 0 || dep > o.depth || s1 < 0 || s0 > Ls) continue;
        if (s1 - s0 < 1e-3) continue;  // ko'rinish yo'nalishiga parallel — chiziq bo'lib ko'rinadi, tashlab ketiladi
        fars.push_back({std::max(0.0, s0), std::min(Ls, s1), std::max(0.0, dep), &w});
    }
    if (cutCount) *cutCount = static_cast<int>(cuts.size());
    if (cuts.empty() && fars.empty()) return out;
    double smin = 1e300, smax = -1e300, H = 0;
    for (auto& c : cuts) { smin = std::min(smin, c.s0); smax = std::max(smax, c.s1); H = std::max(H, c.w->h); }
    for (auto& f : fars) { smin = std::min(smin, f.s0); smax = std::max(smax, f.s1); H = std::max(H, f.w->h); }
    const double step = H + o.slab;
    auto P = [&](double s, double z) { return Vec3{origin.x + (s - smin), origin.y + z, 0}; };
    auto put = [&](std::unique_ptr<IEntity> e, const char* layer) { e->setLayer(layer); e->xset("aw.sect", "view"); out.push_back(std::move(e)); };
    auto rect = [&](double s0, double s1, double z0, double z1, const char* layer, bool hatch) {
        if (s1 - s0 < 1e-6 || z1 - z0 < 1e-6) return;
        std::vector<Vec3> pts{P(s0, z0), P(s1, z0), P(s1, z1), P(s0, z1)};
        if (hatch) {
            std::vector<PolylineEntity> loops;
            loops.emplace_back(pts, true);
            put(std::make_unique<HatchEntity>(std::move(loops), "ANSI31", 0.03, 0.0), "A-SECT-CUT");
        }
        put(std::make_unique<PolylineEntity>(std::move(pts), true), layer);
    };
    // ko'rinadigan oraliqlar (yaqinroq devor orqasidagilar yashiriladi)
    auto visibleParts = [&](const Far& f) {
        std::vector<std::pair<double, double>> vis;
        const double ds = 0.02;
        double cur0 = -1;
        for (double s = f.s0; s <= f.s1 + 1e-9; s += ds) {
            bool hidden = false;
            for (const auto& c : cuts) if (s > c.s0 + 1e-6 && s < c.s1 - 1e-6) hidden = true;
            for (const auto& g : fars) if (&g != &f && g.depth < f.depth - 1e-6 && s > g.s0 + 1e-6 && s < g.s1 - 1e-6) hidden = true;
            if (!hidden && cur0 < 0) cur0 = s;
            if (hidden && cur0 >= 0) { vis.emplace_back(cur0, s); cur0 = -1; }
        }
        if (cur0 >= 0) vis.emplace_back(cur0, f.s1);
        return vis;
    };
    for (int fl = 0; fl < std::max(1, o.floors); ++fl) {
        const double z = fl * step;
        // kesilgan devorlar (shtrix), teshiklar hisobga olinadi
        for (const auto& c : cuts) {
            const ArchOpening* op = nullptr;
            for (const auto& q : c.w->ops) if (c.q > q.pos - q.w / 2 && c.q < q.pos + q.w / 2) op = &q;
            const double wh = c.w->h;
            if (!op) rect(c.s0, c.s1, z, z + wh, "A-SECT", true);
            else if (op->kind == 'D') rect(c.s0, c.s1, z + op->h, z + wh, "A-SECT", true);
            else {
                rect(c.s0, c.s1, z, z + op->sill, "A-SECT", true);
                rect(c.s0, c.s1, z + op->sill + op->h, z + wh, "A-SECT", true);
                rect(c.s0, c.s1, z + op->sill, z + op->sill + op->h, "A-WINDOW", false);
                const double m = (c.s0 + c.s1) / 2;
                put(std::make_unique<LineEntity>(P(m, z + op->sill), P(m, z + op->sill + op->h)), "A-WINDOW");
            }
        }
        // ko'rinadigan devorlar va ulardagi eshik/derazalar
        for (const auto& f : fars) {
            const Vec3 d = unit2(f.w->b - f.w->a);
            for (const auto& [v0, v1] : visibleParts(f)) {
                const double wh = f.w->h;
                put(std::make_unique<LineEntity>(P(v0, z + wh), P(v1, z + wh)), "A-SECT");
                if (std::abs(v0 - f.s0) < 0.03) put(std::make_unique<LineEntity>(P(v0, z), P(v0, z + wh)), "A-SECT");
                if (std::abs(v1 - f.s1) < 0.03) put(std::make_unique<LineEntity>(P(v1, z), P(v1, z + wh)), "A-SECT");
                for (const auto& q : f.w->ops) {
                    double a = dot2(f.w->a + d * (q.pos - q.w / 2) - p1, u), b = dot2(f.w->a + d * (q.pos + q.w / 2) - p1, u);
                    if (a > b) std::swap(a, b);
                    a = std::max(a, v0);
                    b = std::min(b, v1);
                    if (b - a < 0.05) continue;
                    if (q.kind == 'D') rect(a, b, z, z + q.h, "A-DOOR", false);
                    else {
                        rect(a, b, z + q.sill, z + q.sill + q.h, "A-WINDOW", false);
                        put(std::make_unique<LineEntity>(P((a + b) / 2, z + q.sill), P((a + b) / 2, z + q.sill + q.h)), "A-WINDOW");
                    }
                }
            }
        }
        // plita: kesimda shtrixli, fasadda kontur
        rect(smin, smax, z + H, z + H + o.slab, "A-SECT", !cuts.empty());
        // sath belgisi
        const double lev = z;
        put(std::make_unique<TextEntity>(P(smin - 1.2, lev), o.textH, lev == 0 ? std::string("±0.000") : std::format("+{:.3f}", lev), 0.0,
                                         TextHAlign::Right, TextVAlign::Middle), "A-SECT-TEXT");
    }
    const double top = std::max(1, o.floors) * step;
    put(std::make_unique<TextEntity>(P(smin - 1.2, top), o.textH, std::format("+{:.3f}", top), 0.0, TextHAlign::Right, TextVAlign::Middle), "A-SECT-TEXT");
    put(std::make_unique<LineEntity>(P(smin - 1.0, 0), P(smax + 1.0, 0)), "A-SECT");  // yer sathi
    if (!o.title.empty())
        put(std::make_unique<TextEntity>(P((smin + smax) / 2, -0.8), o.textH * 1.6, o.title, 0.0, TextHAlign::Center, TextVAlign::Middle), "A-SECT-TEXT");
    return out;
}

}  // namespace asiacad::bim
