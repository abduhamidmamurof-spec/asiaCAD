// AsiaCAD — fasad tizimi, yoyilma va metraj.
#include <asiacad/bim/facade.h>

#include <asiacad/core/entities.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <format>
#include <map>

namespace asiacad::bim {

namespace {
using Loop = std::vector<Vec3>;

double area2(const Loop& l) {
    double a = 0;
    for (std::size_t i = 0; i < l.size(); ++i) a += l[i].x * l[(i + 1) % l.size()].y - l[(i + 1) % l.size()].x * l[i].y;
    return a / 2;
}
bool inLoop(const Loop& r, double x, double y) {
    bool c = false;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
        if ((r[i].y > y) != (r[j].y > y) && x < (r[j].x - r[i].x) * (y - r[i].y) / (r[j].y - r[i].y) + r[i].x) c = !c;
    return c;
}
// Duglas–Pekker soddalashtirish (yopiq kontur)
Loop simplify(const Loop& in, double tol) {
    if (in.size() < 4) return in;
    std::vector<char> keep(in.size(), 0);
    // eng uzoq ikki nuqtadan boshlab
    std::size_t a = 0, b = 0;
    double best = -1;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const double d = (in[i] - in[0]).length();
        if (d > best) { best = d; b = i; }
    }
    std::function<void(std::size_t, std::size_t)> rec = [&](std::size_t i, std::size_t j) {   // i..j (aylana bo'yicha)
        const std::size_t n = in.size();
        double dmax = -1;
        std::size_t idx = i;
        const Vec3& p = in[i];
        const Vec3& q = in[j % n];
        const double L = std::hypot(q.x - p.x, q.y - p.y);
        for (std::size_t k = i + 1; k < j; ++k) {
            const Vec3& r = in[k % n];
            const double d = L > 1e-12 ? std::abs((q.x - p.x) * (p.y - r.y) - (p.x - r.x) * (q.y - p.y)) / L : std::hypot(r.x - p.x, r.y - p.y);
            if (d > dmax) { dmax = d; idx = k; }
        }
        if (dmax > tol) {
            keep[idx % n] = 1;
            rec(i, idx);
            rec(idx, j);
        }
    };
    keep[a] = keep[b] = 1;
    rec(a, b);
    rec(b, a + in.size());
    Loop out;
    for (std::size_t i = 0; i < in.size(); ++i)
        if (keep[i]) out.push_back(in[i]);
    return out;
}
// Rastr birlashma: konturlar ichidagi kataklar → tashqi chegara (eng uzun halqa)
Loop unionOutline(const std::vector<Loop>& loops, double cell) {
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (const auto& l : loops)
        for (const auto& p : l) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    if (x0 > x1) return {};
    x0 -= cell; y0 -= cell;
    const int nx = static_cast<int>((x1 - x0) / cell) + 3, ny = static_cast<int>((y1 - y0) / cell) + 3;
    if (static_cast<double>(nx) * ny > 2.5e7) return {};
    std::vector<char> g(static_cast<std::size_t>(nx) * ny, 0);
    for (const auto& l : loops) {
        if (l.size() < 3) continue;
        double lx0 = 1e300, ly0 = 1e300, lx1 = -1e300, ly1 = -1e300;
        for (const auto& p : l) { lx0 = std::min(lx0, p.x); ly0 = std::min(ly0, p.y); lx1 = std::max(lx1, p.x); ly1 = std::max(ly1, p.y); }
        const int i0 = std::max(0, static_cast<int>((lx0 - x0) / cell) - 1), i1 = std::min(nx - 1, static_cast<int>((lx1 - x0) / cell) + 1);
        const int j0 = std::max(0, static_cast<int>((ly0 - y0) / cell) - 1), j1 = std::min(ny - 1, static_cast<int>((ly1 - y0) / cell) + 1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
                if (!g[static_cast<std::size_t>(j) * nx + i] && inLoop(l, x0 + (i + 0.5) * cell, y0 + (j + 0.5) * cell)) g[static_cast<std::size_t>(j) * nx + i] = 1;
    }
    auto F = [&](int i, int j) { return i >= 0 && j >= 0 && i < nx && j < ny && g[static_cast<std::size_t>(j) * nx + i]; };
    // to'ldirilgan katak chap tomonda qoladigan yo'naltirilgan qirralar (CCW)
    std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> next;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            if (!F(i, j)) continue;
            if (!F(i, j - 1)) next[{i, j}].push_back({i + 1, j});
            if (!F(i + 1, j)) next[{i + 1, j}].push_back({i + 1, j + 1});
            if (!F(i, j + 1)) next[{i + 1, j + 1}].push_back({i, j + 1});
            if (!F(i - 1, j)) next[{i, j + 1}].push_back({i, j});
        }
    Loop best;
    double bestA = 0;
    while (!next.empty()) {
        auto it = next.begin();
        const auto start = it->first;
        std::vector<std::pair<int, int>> chain{start};
        auto cur = start;
        for (std::size_t guard = 0; guard < 10000000; ++guard) {
            auto f = next.find(cur);
            if (f == next.end() || f->second.empty()) break;
            const auto nxt = f->second.back();
            f->second.pop_back();
            if (f->second.empty()) next.erase(f);
            if (nxt == start) break;
            chain.push_back(nxt);
            cur = nxt;
        }
        Loop l;
        for (const auto& [i, j] : chain) l.push_back({x0 + i * cell, y0 + j * cell, 0});
        const double a = area2(l);
        if (a > bestA) { bestA = a; best = std::move(l); }
    }
    return simplify(best, cell * 0.9);
}
}  // namespace

bool facadeTypeFromKey(const std::string& k, FacadeType& out) {
    const std::string u = utf8::toUpper(k);
    if (u == "CURTAIN" || u == "VITRAJ" || u == "ВИТРАЖ") { out = FacadeType::Curtain; return true; }
    if (u == "BAND" || u == "LENTA" || u == "ЛЕНТА") { out = FacadeType::Band; return true; }
    if (u == "PUNCHED" || u == "WINDOWS" || u == "DERAZA" || u == "ОКНА") { out = FacadeType::Punched; return true; }
    return false;
}

std::vector<FacadeFloor> facadeFloorsFromIfc(const IfcModel& m, double cell) {
    std::vector<FacadeFloor> out;
    std::vector<int> slabs;
    for (std::size_t i = 0; i < m.elements.size(); ++i)
        if (std::string(ifcCategory(m.elements[i].type)) == "SLAB") slabs.push_back(static_cast<int>(i));
    if (slabs.empty() || m.storeys.size() < 2) return out;
    std::vector<double> E;
    for (const auto& s : m.storeys) E.push_back(s.elevation);
    std::sort(E.begin(), E.end());
    for (std::size_t i = 0; i < E.size(); ++i) {
        const double z0 = E[i];
        const double z1 = i + 1 < E.size() ? E[i + 1] : z0 + (i > 0 ? E[i] - E[i - 1] : 3.0);
        if (z1 < 0.5) continue;   // yer osti — fasad yo'q
        std::vector<Loop> loops;
        for (double z : {z1 - 0.03, z0 + 0.03})
            for (const auto& s : ifcSection(m, z, slabs))
                if (s.closed && s.pts.size() >= 3) loops.push_back(s.pts);
        if (loops.empty()) continue;
        Loop o = unionOutline(loops, cell);
        if (o.size() < 3) continue;
        if (area2(o) < 0) std::reverse(o.begin(), o.end());
        out.push_back({o, std::max(z0, 0.0), z1, static_cast<int>(i)});
    }
    return out;
}

std::vector<FacadeFloor> facadeFloorsFromDoc(const Document& doc, const std::string& layer, int floors, double h, double z0) {
    Loop best;
    doc.forEach([&](const IEntity& e) {
        const auto* pl = dynamic_cast<const PolylineEntity*>(&e);
        if (!pl || !pl->isClosed() || pl->vertices().size() < 3) return;
        const std::string u = utf8::toUpper(e.layer());
        const bool match = layer.empty() ? (u.find("FASAD") != std::string::npos || u.find("FACADE") != std::string::npos || u.find("КОНТУР") != std::string::npos ||
                                            u.find("KONTUR") != std::string::npos || u.find("OUTLINE") != std::string::npos)
                                         : e.layer() == layer;
        if (!match) return;
        Loop l(pl->vertices().begin(), pl->vertices().end());
        if (std::abs(area2(l)) > std::abs(area2(best))) best = l;
    });
    std::vector<FacadeFloor> out;
    if (best.size() < 3) return out;
    if (area2(best) < 0) std::reverse(best.begin(), best.end());
    for (int k = 0; k < floors; ++k) out.push_back({best, z0 + k * h, z0 + (k + 1) * h, k + 1});
    return out;
}

FacadeResult buildFacade(const std::vector<FacadeFloor>& floors, const FacadeSpec& s) {
    FacadeResult r;
    double zmin = 1e300, zmax = -1e300;
    for (const auto& f : floors) {
        const Loop& o = f.outline;
        const std::size_t n = o.size();
        if (n < 3) continue;
        ++r.floors;
        zmin = std::min(zmin, f.z0);
        zmax = std::max(zmax, f.z1);
        // tashqariga siljitilgan kontur (bissektrisa bo'yicha)
        Loop off(n);
        for (std::size_t i = 0; i < n; ++i) {
            const Vec3& p = o[(i + n - 1) % n];
            const Vec3& c = o[i];
            const Vec3& q = o[(i + 1) % n];
            auto nrm = [](const Vec3& a, const Vec3& b) {
                const double dx = b.x - a.x, dy = b.y - a.y, L = std::hypot(dx, dy);
                return L > 1e-12 ? Vec3{dy / L, -dx / L, 0} : Vec3{0, 0, 0};
            };
            const Vec3 n1 = nrm(p, c), n2 = nrm(c, q);
            Vec3 b{n1.x + n2.x, n1.y + n2.y, 0};
            const double bl = std::hypot(b.x, b.y);
            const double cosh = bl / 2;
            if (bl > 1e-9) b = {b.x / bl, b.y / bl, 0};
            const double d = s.offset / std::max(cosh, 0.35);
            off[i] = {c.x + b.x * d, c.y + b.y * d, 0};
        }
        double perim = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const Vec3& a = off[i];
            const Vec3& b = off[(i + 1) % n];
            const double L = std::hypot(b.x - a.x, b.y - a.y);
            if (L < 1e-6) continue;
            const double ux = (b.x - a.x) / L, uy = (b.y - a.y) / L;
            const int segs = L < s.minEdge ? 1 : std::max(1, static_cast<int>(std::lround(L / s.module)));
            const double w = L / segs;
            auto P = [&](double t, double z) { return Vec3{a.x + ux * t, a.y + uy * t, z}; };
            auto quad = [&](FacadePart part, double t0, double t1, double za, double zb) {
                if (t1 - t0 < 1e-4 || zb - za < 1e-4) return;
                r.quads.push_back({part, {P(t0, za), P(t1, za), P(t1, zb), P(t0, zb)}, static_cast<int>(i), perim + t0, perim + t1});
                const double A = (t1 - t0) * (zb - za);
                if (part == FacadePart::Glass) { r.glassArea += A; ++r.glassCount; }
                else if (part == FacadePart::Panel) { r.panelArea += A; ++r.panelCount; }
            };
            const double H = f.z1 - f.z0;
            for (int k = 0; k < segs; ++k) {
                const double t0 = k * w, t1 = (k + 1) * w;
                switch (s.type) {
                    case FacadeType::Curtain: {
                        const double sp = std::min(s.spandrel, H * 0.4);
                        quad(FacadePart::Glass, t0, t1, f.z0, f.z1 - sp);
                        quad(FacadePart::Panel, t0, t1, f.z1 - sp, f.z1);
                        r.mullionLen += t1 - t0;   // transom (spandrel ostida)
                        break;
                    }
                    case FacadeType::Band: {
                        const double low = std::min(s.sill, H * 0.35), top = std::min(0.35, H * 0.15);
                        quad(FacadePart::Panel, t0, t1, f.z0, f.z0 + low);
                        quad(FacadePart::Glass, t0, t1, f.z0 + low, f.z1 - top);
                        quad(FacadePart::Panel, t0, t1, f.z1 - top, f.z1);
                        r.mullionLen += 2 * (t1 - t0);
                        break;
                    }
                    case FacadeType::Punched: {
                        const double ww = std::min(s.winW, w - 0.4), wz0 = f.z0 + s.sill, wz1 = std::min(f.z0 + s.sill + s.winH, f.z1 - 0.3);
                        if (ww < 0.6 || wz1 - wz0 < 0.6) { quad(FacadePart::Panel, t0, t1, f.z0, f.z1); break; }
                        const double c0 = (t0 + t1) / 2 - ww / 2, c1 = c0 + ww;
                        quad(FacadePart::Panel, t0, t1, f.z0, wz0);
                        quad(FacadePart::Panel, t0, t1, wz1, f.z1);
                        quad(FacadePart::Panel, t0, c0, wz0, wz1);
                        quad(FacadePart::Panel, c1, t1, wz0, wz1);
                        quad(FacadePart::Glass, c0, c1, wz0, wz1);
                        r.mullionLen += 2 * (ww + (wz1 - wz0));
                        break;
                    }
                }
                // vertikal profil (segment boshida), fasad tekisligidan 3 sm oldinda
                if (s.type != FacadeType::Punched) {
                    const double hm = s.mullion / 2;
                    const Vec3 nrmv{uy, -ux, 0};
                    auto Q = [&](double t, double z) { const Vec3 p = P(t, z); return Vec3{p.x + nrmv.x * 0.03, p.y + nrmv.y * 0.03, z}; };
                    r.quads.push_back({FacadePart::Mullion, {Q(t0 - hm, f.z0), Q(t0 + hm, f.z0), Q(t0 + hm, f.z1), Q(t0 - hm, f.z1)}, static_cast<int>(i), perim + t0 - hm,
                                       perim + t0 + hm});
                    r.mullionLen += H;
                }
            }
            perim += L;
        }
        r.perimeter = std::max(r.perimeter, perim);
    }
    if (zmax > zmin) r.height = zmax - zmin;
    return r;
}

std::vector<std::unique_ptr<IEntity>> facadeEntities(const FacadeResult& r) {
    std::vector<std::unique_ptr<IEntity>> out;
    const char* layers[3] = {"FASAD_SHISHA", "FASAD_PANEL", "FASAD_PROFIL"};
    for (int part = 0; part < 3; ++part) {
        std::vector<Vec3> v;
        std::vector<MeshEntity::Face> f;
        auto flush = [&] {
            if (f.empty()) return;
            auto e = std::make_unique<MeshEntity>(std::move(v), std::move(f));
            e->setLayer(layers[part]);
            out.push_back(std::move(e));
            v.clear();
            f.clear();
        };
        for (const auto& q : r.quads) {
            if (static_cast<int>(q.part) != part) continue;
            const int b = static_cast<int>(v.size());
            for (const auto& p : q.q) v.push_back(p);
            f.push_back({b, b + 1, b + 2, b + 3, 0});
            if (v.size() > 30000) flush();   // DXF polyface chegarasi (32767)
        }
        flush();
    }
    return out;
}

std::vector<std::unique_ptr<IEntity>> facadeElevation(const FacadeResult& r, const std::vector<FacadeFloor>& floors, const Vec3& origin, double textH) {
    std::vector<std::unique_ptr<IEntity>> out;
    auto put = [&](std::unique_ptr<IEntity> e) { e->setLayer("FASAD_YOYILMA"); out.push_back(std::move(e)); };
    double zmin = 1e300;
    for (const auto& f : floors) zmin = std::min(zmin, f.z0);
    auto X = [&](double s) { return origin.x + s; };
    auto Y = [&](double z) { return origin.y + (z - zmin); };
    for (const auto& q : r.quads) {
        if (q.part == FacadePart::Mullion) continue;
        const double za = q.q[0].z, zb = q.q[2].z;
        put(std::make_unique<PolylineEntity>(std::vector<Vec3>{{X(q.s0), Y(za), 0}, {X(q.s1), Y(za), 0}, {X(q.s1), Y(zb), 0}, {X(q.s0), Y(zb), 0}}, true));
        if (q.part == FacadePart::Glass)   // shisha belgisi — diagonal
            put(std::make_unique<LineEntity>(Vec3{X(q.s0 + (q.s1 - q.s0) * 0.15), Y(za + (zb - za) * 0.15), 0}, Vec3{X(q.s0 + (q.s1 - q.s0) * 0.45), Y(za + (zb - za) * 0.45), 0}));
    }
    // qavat belgilari (chapda)
    for (const auto& f : floors) {
        put(std::make_unique<LineEntity>(Vec3{X(-3), Y(f.z0), 0}, Vec3{X(0), Y(f.z0), 0}));
        put(std::make_unique<TextEntity>(Vec3{X(-3.2), Y(f.z0), 0}, textH, std::format("{:+.3f}", f.z0), 0.0, TextHAlign::Right, TextVAlign::Middle));
    }
    if (!floors.empty())
        put(std::make_unique<TextEntity>(Vec3{X(-3.2), Y(floors.back().z1), 0}, textH, std::format("{:+.3f}", floors.back().z1), 0.0, TextHAlign::Right, TextVAlign::Middle));
    return out;
}

}  // namespace asiacad::bim
