// AsiaCAD — genplan: zonalar, bordyurlar, avtoturargoh joylari, metraj.
#include <asiacad/civil/genplan.h>

#include <asiacad/core/entities.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace asiacad::civil {

namespace {
constexpr double kPI = 3.14159265358979323846;
using Ring = std::vector<GpPoint>;

double signedArea(const Ring& r) {
    double a = 0;
    for (std::size_t i = 0, n = r.size(); i < n; ++i) a += r[i].x * r[(i + 1) % n].y - r[(i + 1) % n].x * r[i].y;
    return a / 2;
}
bool inRing(const Ring& r, double x, double y) {
    bool c = false;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
        const auto& a = r[i];
        const auto& b = r[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) c = !c;
    }
    return c;
}
bool inPoly(const GpPoly& p, double x, double y) {
    if (!inRing(p.outer, x, y)) return false;
    for (const auto& h : p.holes)
        if (inRing(h, x, y)) return false;
    return true;
}
// polilin (bulge yoylari bilan) → halqa
Ring ringOf(const PolylineEntity& pl) {
    Ring r;
    const auto& v = pl.vertices();
    const auto& bl = pl.bulges();
    const std::size_t n = v.size();
    for (std::size_t i = 0; i < n; ++i) {
        r.push_back({v[i].x, v[i].y});
        const double b = i < bl.size() ? bl[i] : 0;
        if (std::abs(b) < 1e-9 || (!pl.isClosed() && i + 1 == n)) continue;
        const auto& a = v[i];
        const auto& c = v[(i + 1) % n];
        const double th = 4 * std::atan(b), dx = c.x - a.x, dy = c.y - a.y, L = std::hypot(dx, dy);
        if (L < 1e-9) continue;
        const double R = L / (2 * std::sin(std::abs(th) / 2));
        const double mx = (a.x + c.x) / 2, my = (a.y + c.y) / 2, h = R * std::cos(th / 2) * (b > 0 ? 1 : -1);
        const double cx = mx - dy / L * h, cy = my + dx / L * h;
        const double a0 = std::atan2(a.y - cy, a.x - cx);
        const int seg = std::max(2, static_cast<int>(std::abs(th) / (kPI / 16)));
        for (int k = 1; k < seg; ++k) {
            const double t = a0 + th * k / seg;
            r.push_back({cx + std::abs(R) * std::cos(t), cy + std::abs(R) * std::sin(t)});
        }
    }
    // takrorlangan nuqtalar
    Ring o;
    for (const auto& p : r)
        if (o.empty() || std::hypot(p.x - o.back().x, p.y - o.back().y) > 1e-4) o.push_back(p);
    if (o.size() > 2 && std::hypot(o.front().x - o.back().x, o.front().y - o.back().y) < 1e-4) o.pop_back();
    return o;
}
// ikki qavariq to'rtburchak kesishadimi (ajratuvchi o'q; 5 sm bardosh)
bool quadsOverlap(const std::array<Vec3, 4>& A, const std::array<Vec3, 4>& B) {
    for (const auto* P : {&A, &B})
        for (int i = 0; i < 4; ++i) {
            const Vec3& a = (*P)[i];
            const Vec3& b = (*P)[(i + 1) % 4];
            const double nx = -(b.y - a.y), ny = b.x - a.x, l = std::hypot(nx, ny);
            if (l < 1e-12) continue;
            double a0 = 1e300, a1 = -1e300, b0 = 1e300, b1 = -1e300;
            for (const auto& q : A) { const double d = (q.x * nx + q.y * ny) / l; a0 = std::min(a0, d); a1 = std::max(a1, d); }
            for (const auto& q : B) { const double d = (q.x * nx + q.y * ny) / l; b0 = std::min(b0, d); b1 = std::max(b1, d); }
            if (a1 <= b0 + 0.05 || b1 <= a0 + 0.05) return false;
        }
    return true;
}
std::string up(const std::string& s) { return utf8::toUpper(s); }
bool curbLayer(const std::string& l) { return l.rfind("GP_", 0) == 0; }

template <class T>
void put(Entities& out, std::unique_ptr<T> e, const char* layer) {
    e->setLayer(layer);
    out.push_back(std::move(e));
}
}  // namespace

const char* gpZoneKey(GpZone z) {
    switch (z) {
        case GpZone::Road: return "ROAD";
        case GpZone::Sidewalk: return "SIDEWALK";
        case GpZone::Green: return "GREEN";
        case GpZone::Parking: return "PARKING";
        case GpZone::Building: return "BUILDING";
        case GpZone::Playground: return "PLAYGROUND";
        case GpZone::Ramp: return "RAMP";
        case GpZone::Path: return "PATH";
        default: return "NONE";
    }
}
bool gpZoneFromKey(const std::string& k, GpZone& out) {
    const std::string u = up(k);
    for (int i = 0; i <= 8; ++i)
        if (u == gpZoneKey(static_cast<GpZone>(i))) { out = static_cast<GpZone>(i); return true; }
    return false;
}

std::vector<GpRule> gpDefaultRules() {
    // tartib muhim: birinchi mos kelgan qoida ishlaydi (masalan, "DOROZHK" — yo'lak, "DOROG" — yo'l)
    std::vector<GpRule> r;
    auto add = [&](GpZone z, std::initializer_list<const char*> ps) { for (const char* p : ps) r.push_back({p, z}); };
    add(GpZone::Building, {"ZDANI", "ЗДАНИ", "BUILDING", "BINOLAR", "BINO", "BINA", "KONTUR_ZD", "КОНТУР ЗД"});
    add(GpZone::Parking, {"PARKOV", "ПАРКОВ", "PARKING", "OTOPARK", "AVTOTURARGOH", "STOYAN", "СТОЯН", "AVTOSTOYAN"});
    add(GpZone::Ramp, {"RAMP", "РАМП", "PANDUS", "ПАНДУС"});
    add(GpZone::Playground, {"PLOSHAD", "ПЛОЩАД", "MAYDON", "OYUN", "PLAYGROUND", "DETSK", "ДЕТСК", "SPORT"});
    add(GpZone::Path, {"DOROZHK", "ДОРОЖК", "PATH", "YOLAK", "PATIKA", "BEGOV", "БЕГОВ", "YURISH"});
    add(GpZone::Road, {"DOROG", "ДОРОГ", "ROAD", "PROEZD", "ПРОЕЗД", "ASFALT", "АСФАЛЬТ", "KOLTSEV", "КОЛЬЦЕВ", "YO'L", "YOL", "AVTOYOL"});
    add(GpZone::Sidewalk, {"TROTUAR", "ТРОТУАР", "SIDEWALK", "POKRYT", "ПОКРЫТ", "PLITK", "ПЛИТК", "TVERD", "ТВЕРД", "KALDIRIM", "BRUSCHAT",
                           "БРУСЧАТ", "PAVING", "KAPLAMA"});
    add(GpZone::Green, {"OZELEN", "ОЗЕЛЕН", "ZELEN", "ЗЕЛЕН", "GAZON", "ГАЗОН", "GREEN", "YASHIL", "YESIL", "YEŞIL", "CIM", "ÇIM", "LAWN"});
    return r;
}

GpZone gpClassifyLayer(const std::string& layer, const std::vector<GpRule>& rules) {
    const std::string u = up(layer);
    for (const auto& r : rules)
        if (u.find(up(r.pattern)) != std::string::npos) return r.zone;
    return GpZone::Unknown;
}

std::map<std::string, GpZone> gpParseMap(const std::string& text) {
    std::map<std::string, GpZone> m;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t j = text.find(',', i);
        if (j == std::string::npos) j = text.size();
        const std::string item = text.substr(i, j - i);
        i = j + 1;
        if (item.empty()) continue;
        const auto c = item.rfind(':');
        GpZone z;
        if (c == std::string::npos || c == 0 || !gpZoneFromKey(item.substr(c + 1), z)) throw std::invalid_argument(item);
        m[item.substr(0, c)] = z;
    }
    return m;
}

std::vector<GpPoly> gpExtractZones(const Document& doc, const GenplanSpec& s, std::map<std::string, std::pair<GpZone, int>>* layers) {
    std::vector<GpPoly> out;
    auto zoneOf = [&](const std::string& l) {
        if (auto it = s.overrides.find(l); it != s.overrides.end()) return it->second;
        return gpClassifyLayer(l, s.rules);
    };
    auto addRings = [&](const std::string& layer, std::vector<Ring> rings) {
        const GpZone z = zoneOf(layer);
        if (layers) { auto& e = (*layers)[layer]; e.first = z; e.second += static_cast<int>(rings.size()); }
        if (z == GpZone::Unknown) return;
        // eng katta halqalar — tashqi; ichida yotganlari — teshik
        std::sort(rings.begin(), rings.end(), [](const Ring& a, const Ring& b) { return std::abs(signedArea(a)) > std::abs(signedArea(b)); });
        std::vector<GpPoly> polys;
        for (auto& r : rings) {
            if (r.size() < 3 || std::abs(signedArea(r)) < s.minArea) continue;
            GpPoly* host = nullptr;
            for (auto& p : polys)
                if (inRing(p.outer, r[0].x, r[0].y) && inRing(p.outer, r[r.size() / 2].x, r[r.size() / 2].y)) { host = &p; break; }
            if (host) {
                if (signedArea(r) > 0) std::reverse(r.begin(), r.end());   // teshik — CW
                host->holes.push_back(std::move(r));
            } else {
                GpPoly p;
                p.zone = z;
                p.layer = layer;
                if (signedArea(r) < 0) std::reverse(r.begin(), r.end());   // tashqi — CCW
                p.outer = std::move(r);
                polys.push_back(std::move(p));
            }
        }
        for (auto& p : polys) {
            p.area = signedArea(p.outer);
            for (const auto& h : p.holes) p.area += signedArea(h);
            out.push_back(std::move(p));
        }
    };
    doc.forEach([&](const IEntity& e) {
        if (curbLayer(e.layer())) return;
        if (const auto* pl = dynamic_cast<const PolylineEntity*>(&e); pl && pl->isClosed()) addRings(e.layer(), {ringOf(*pl)});
        else if (const auto* h = dynamic_cast<const HatchEntity*>(&e)) {
            std::vector<Ring> rs;
            for (const auto& l : h->loops()) rs.push_back(ringOf(l));
            addRings(e.layer(), std::move(rs));
        }
    });
    return out;
}

GenplanResult generateGenplan(const Document& doc, const GenplanSpec& s) {
    GenplanResult res;
    res.zones = gpExtractZones(doc, s, &res.layers);
    auto& Z = res.zones;
    for (const auto& p : Z) res.area[p.zone] += p.area;
    struct Box { double x0, y0, x1, y1; };
    std::vector<Box> bb;
    for (const auto& p : Z) {
        Box b{1e300, 1e300, -1e300, -1e300};
        for (const auto& q : p.outer) { b.x0 = std::min(b.x0, q.x); b.y0 = std::min(b.y0, q.y); b.x1 = std::max(b.x1, q.x); b.y1 = std::max(b.y1, q.y); }
        bb.push_back(b);
    }
    // nuqtadagi zona (o'zidan boshqa, eng kichik maydonli)
    auto zoneAt = [&](double x, double y, std::size_t self) {
        GpZone z = GpZone::Unknown;
        double best = 1e300;
        for (std::size_t k = 0; k < Z.size(); ++k) {
            if (k == self || x < bb[k].x0 || x > bb[k].x1 || y < bb[k].y0 || y > bb[k].y1) continue;
            if (Z[k].area < best && inPoly(Z[k], x, y)) { best = Z[k].area; z = Z[k].zone; }
        }
        return z;
    };
    auto paved = [](GpZone z) { return z == GpZone::Sidewalk || z == GpZone::Path || z == GpZone::Playground || z == GpZone::Ramp; };
    // 0 — yo'q, 1 — yo'l bordyuri, 2 — bog' bordyuri (shu zona tomonidan chiziladi)
    auto curbType = [&](GpZone me, GpZone nb) -> int {
        if (me == nb) return 0;
        if (nb == GpZone::Unknown) return 0;   // aniqlanmagan qo'shni (parapet, skameyka, uchastka chegarasi) — inson qaror qiladi
        if (me == GpZone::Road) return (nb == GpZone::Parking || nb == GpZone::Building) ? 0 : 1;
        if (me == GpZone::Parking) return (nb == GpZone::Road || nb == GpZone::Building) ? 0 : 1;
        if (paved(me)) {
            if (nb == GpZone::Green) return 2;
            return 0;   // yo'l/avtoturargoh tomoni o'zi chizadi; bino — otmostka; boshqa qattiq qoplama bilan — tekis
        }
        return 0;
    };
    if (s.curbs)
        for (std::size_t k = 0; k < Z.size(); ++k) {
            std::vector<const Ring*> rings{&Z[k].outer};
            for (const auto& h : Z[k].holes) rings.push_back(&h);
            for (const Ring* rp : rings) {
                const Ring& r = *rp;
                const std::size_t n = r.size();
                std::vector<int> type(n, 0);
                for (std::size_t i = 0; i < n; ++i) {
                    const auto& a = r[i];
                    const auto& b = r[(i + 1) % n];
                    const double dx = b.x - a.x, dy = b.y - a.y, L = std::hypot(dx, dy);
                    if (L < 1e-6) continue;
                    const double ox = dy / L * 0.3, oy = -dx / L * 0.3;   // tashqariga (CCW tashqi, CW teshik)
                    type[i] = curbType(Z[k].zone, zoneAt((a.x + b.x) / 2 + ox, (a.y + b.y) / 2 + oy, k));
                }
                // ketma-ket bir xil turdagi qirralar → bitta polilin; halqa boshidan uzilmasin
                std::size_t start = 0;
                while (start < n && type[start] == type[(start + n - 1) % n]) ++start;
                if (start == n) start = 0;
                std::size_t i = 0;
                while (i < n) {
                    const std::size_t e0 = (start + i) % n;
                    const int t = type[e0];
                    std::size_t len = 1;
                    while (i + len < n && type[(start + i + len) % n] == t) ++len;
                    if (t != 0) {
                        std::vector<Vec3> pts;
                        for (std::size_t q = 0; q <= len; ++q) { const auto& p = r[(e0 + q) % n]; pts.push_back({p.x, p.y, 0}); }
                        const bool whole = len == n;
                        if (whole) pts.pop_back();
                        double L = 0;
                        for (std::size_t q = 0; q + 1 < pts.size(); ++q) L += std::hypot(pts[q + 1].x - pts[q].x, pts[q + 1].y - pts[q].y);
                        if (whole) L += std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y);
                        const double w = t == 1 ? s.roadCurb.width : s.gardenCurb.width;
                        // ikkinchi chiziq — zona ichiga eni qadar (burchaklarda bissektrisa)
                        std::vector<Vec3> in;
                        const std::size_t m = pts.size();
                        for (std::size_t q = 0; q < m; ++q) {
                            auto nrm = [&](std::size_t a, std::size_t b) {
                                const double dx = pts[b].x - pts[a].x, dy = pts[b].y - pts[a].y, l = std::hypot(dx, dy);
                                return std::pair{l > 0 ? -dy / l : 0.0, l > 0 ? dx / l : 0.0};   // ichkariga (CCW chap tomon)
                            };
                            std::pair<double, double> n1, n2;
                            const bool hasPrev = q > 0 || whole, hasNext = q + 1 < m || whole;
                            if (hasPrev) n1 = nrm((q + m - 1) % m, q);
                            if (hasNext) n2 = nrm(q, (q + 1) % m);
                            if (!hasPrev) n1 = n2;
                            if (!hasNext) n2 = n1;
                            double nx = n1.first + n2.first, ny = n1.second + n2.second;
                            const double l = std::hypot(nx, ny);
                            const double cosh = l / 2;
                            nx /= std::max(l, 1e-9);
                            ny /= std::max(l, 1e-9);
                            const double d = w / std::max(cosh, 0.3);
                            in.push_back({pts[q].x + nx * d, pts[q].y + ny * d, 0});
                        }
                        const char* layer = t == 1 ? "GP_BORDYUR_YOL" : "GP_BORDYUR_BOG";
                        put(res.entities, std::make_unique<PolylineEntity>(pts, whole), layer);
                        put(res.entities, std::make_unique<PolylineEntity>(in, whole), layer);
                        (t == 1 ? res.roadCurbLen : res.gardenCurbLen) += L;
                    }
                    i += len;
                }
            }
        }
    if (s.parking)
        for (std::size_t k = 0; k < Z.size(); ++k) {
            if (Z[k].zone != GpZone::Parking) continue;
            const Ring& r = Z[k].outer;
            std::vector<std::array<Vec3, 4>> placed;
            const double W = s.stallW, Lst = s.stallL;
            // uzun qirralardan boshlab (joylar ko'proq sig'adi)
            std::vector<std::size_t> order(r.size());
            for (std::size_t i = 0; i < r.size(); ++i) order[i] = i;
            auto elen = [&](std::size_t i) { const auto& a = r[i]; const auto& b = r[(i + 1) % r.size()]; return std::hypot(b.x - a.x, b.y - a.y); };
            std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return elen(x) > elen(y); });
            for (std::size_t i : order) {
                const std::size_t n = r.size();
                const auto& a = r[i];
                const auto& b = r[(i + 1) % n];
                const double dx = b.x - a.x, dy = b.y - a.y, L = std::hypot(dx, dy);
                if (L < W) continue;
                const double ux = dx / L, uy = dy / L, nx = -uy, ny = ux;   // ichkariga
                const int cnt = static_cast<int>(std::floor(L / W + 1e-6));
                const double t0 = (L - cnt * W) / 2;
                for (int j = 0; j < cnt; ++j) {
                    const double s0 = t0 + j * W;
                    const double px = a.x + ux * s0 + nx * 0.05, py = a.y + uy * s0 + ny * 0.05;
                    const Vec3 c0{px, py, 0}, c1{px + ux * W, py + uy * W, 0}, c2{c1.x + nx * Lst, c1.y + ny * Lst, 0}, c3{px + nx * Lst, py + ny * Lst, 0};
                    const double cx = (c0.x + c2.x) / 2, cy = (c0.y + c2.y) / 2;
                    bool ok = inPoly(Z[k], cx, cy);
                    for (const Vec3& c : {c0, c1, c2, c3}) ok = ok && inPoly(Z[k], c.x + (cx - c.x) * 0.01, c.y + (cy - c.y) * 0.01);   // chegarada — ichkari
                    const std::array<Vec3, 4> q{c0, c1, c2, c3};
                    for (const auto& o : placed) ok = ok && !quadsOverlap(o, q);
                    if (!ok) continue;
                    placed.push_back(q);
                    put(res.entities, std::make_unique<PolylineEntity>(std::vector<Vec3>{c3, c0, c1, c2}, false), "GP_AVTOTURARGOH");
                }
            }
            res.stalls += static_cast<int>(placed.size());
        }
    return res;
}

}  // namespace asiacad::civil
