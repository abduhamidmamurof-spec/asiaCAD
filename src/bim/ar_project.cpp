// AsiaCAD — arxitektura (АР) rejalari va kesimlaridan 3D bino. Batafsil — ar_project.h
#include <asiacad/bim/ar_project.h>
#include <asiacad/core/entities.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace asiacad::bim {

const char* arViewKey(ArViewKind k) {
    switch (k) {
        case ArViewKind::Plan: return "plan";
        case ArViewKind::Section: return "section";
        case ArViewKind::Facade: return "facade";
        case ArViewKind::Roof: return "roof";
        case ArViewKind::Basement: return "basement";
        default: return "other";
    }
}

namespace {

bool has(const std::string& u, std::initializer_list<const char*> ks) {
    for (const char* k : ks)
        if (u.find(k) != std::string::npos) return true;
    return false;
}

// Matndagi butun sonlar (pozitsiyasi bilan)
struct Num { int v; std::size_t pos, end; };
std::vector<Num> numbers(const std::string& s) {
    std::vector<Num> out;
    for (std::size_t i = 0; i < s.size();) {
        if (std::isdigit(static_cast<unsigned char>(s[i]))) {
            std::size_t j = i;
            int v = 0;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])) && j - i < 4) v = v * 10 + (s[j++] - '0');
            // belgilar ("+3.300", "0,000") — qavat raqami emas
            const bool level = (j < s.size() && (s[j] == '.' || s[j] == ',') && j + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[j + 1]))) ||
                               (i > 0 && (s[i - 1] == '.' || s[i - 1] == ',' || s[i - 1] == '+'));
            if (!level) out.push_back({v, i, j});
            while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == ',')) ++j;
            i = j;
        } else {
            ++i;
        }
    }
    return out;
}

// Sarlavhada qavat so'zi qayerda
std::size_t floorWord(const std::string& u) {
    for (const char* k : {"ЭТАЖ", "QAVAT", "КАВАТ", "ҚАВАТ", "FLOOR", "STOREY", "LEVEL", "KAT "})
        if (auto p = u.find(k); p != std::string::npos) return p;
    return std::string::npos;
}

struct Ent {
    const IEntity* e;
    BBox box;
};

void collectAll(const Document& doc, std::vector<std::unique_ptr<IEntity>>& owned, std::vector<Ent>& out) {
    doc.forEach([&](const IEntity& e) { out.push_back({&e, e.bbox()}); });
    (void)owned;
}

Vec3 mid(const BBox& b) { return {(b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, 0}; }
bool inBox(const BBox& b, const Vec3& p) { return p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y; }

bool isWindowBlock(const std::string& u) {
    return has(u, {"ОК-", "ОК_", "ОКНО", "OKNO", "WIN", "DERAZA", "ДЕРАЗА", "VITRAJ", "ВИТРАЖ", "PENCERE"}) ||
           (u.size() >= 2 && u.rfind("ОК", 0) == 0);
}
bool isDoorBlock(const std::string& u) {
    return has(u, {"ДВ-", "ДВ_", "ДВЕР", "DVER", "DOOR", "ESHIK", "ЭШИК", "КАПЫ", "KAPI"}) || (u.size() >= 2 && u.rfind("ДВ", 0) == 0);
}

}  // namespace

ArViewKind classifyArTitle(const std::string& title, int& from, int& to) {
    from = to = 0;
    const std::string u = utf8::toUpper(title);
    if (has(u, {"РАЗРЕЗ", "KESIM", "КЕСИМ", "SECTION", "KESİT"})) return ArViewKind::Section;
    if (has(u, {"ФАСАД", "FASAD", "FACADE", "ELEVATION", "GÖRÜNÜŞ"})) return ArViewKind::Facade;
    if (has(u, {"КРОВЛ", "TOM REJA", "TOMI ", "ROOF", "ÇATI"})) return ArViewKind::Roof;
    if (has(u, {"ПОДВАЛ", "ЦОКОЛ", "YERTO'LA", "YERTOʻLA", "ЕРТЎЛА", "BASEMENT", "BODRUM"})) return ArViewKind::Basement;
    const bool plan = has(u, {"ПЛАН", "REJA", "РЕЖА", "PLAN"});
    const std::size_t fw = floorWord(u);
    if (!plan && fw == std::string::npos) return ArViewKind::Other;
    if (fw == std::string::npos) return ArViewKind::Other;   // "План благоустройства", "Генплан" — qavat yo'q
    const auto ns = numbers(u);
    // oraliq: "2-9", "2÷9", "2...9", "(2-9)", "2 - 9"
    for (std::size_t i = 0; i + 1 < ns.size(); ++i) {
        const std::string between = u.substr(ns[i].end, ns[i + 1].pos - ns[i].end);
        std::string t;
        for (char c : between)
            if (c != ' ' && c != '-' && c != '.' && c != '(' && c != ')') t += c;
        if ((t.empty() || t == "\xC3\xB7" || t == "\xE2\x80\xA6" || t == "ГО" || t == "ДО" || t == "GACHA") && between.size() <= 6 &&
            ns[i].v >= 1 && ns[i + 1].v > ns[i].v && ns[i + 1].v <= 200) {
            from = ns[i].v;
            to = ns[i + 1].v;
            return ArViewKind::Plan;
        }
    }
    // bitta raqam: qavat so'ziga eng yaqini
    int best = -1;
    std::size_t bd = std::string::npos;
    for (const auto& n : ns) {
        if (n.v < 1 || n.v > 200) continue;
        const std::size_t d = n.pos < fw ? fw - n.end : n.pos - fw;
        if (d < bd) { bd = d; best = n.v; }
    }
    if (best > 0 && bd < 24) {
        from = to = best;
        return ArViewKind::Plan;
    }
    if (has(u, {"ТИПОВ", "TIPIK", "TYPICAL"})) return ArViewKind::Plan;   // oraliq sarlavhada yo'q — kesimdan
    if (has(u, {"ПЕРВ", "BIRINCHI", "FIRST", "GROUND"})) { from = to = 1; return ArViewKind::Plan; }
    return ArViewKind::Other;
}

bool floorChain(const std::vector<double>& levels0, double& height, int& floors) {
    std::vector<double> lv;
    for (double v : levels0)
        if (v > -0.01 && v < 400) lv.push_back(v);
    std::sort(lv.begin(), lv.end());
    lv.erase(std::unique(lv.begin(), lv.end(), [](double a, double b) { return std::abs(a - b) < 0.005; }), lv.end());
    if (lv.size() < 2 || std::abs(lv.front()) > 0.01) return false;
    // eng ko'p uchragan qadam (5 sm aniqlikda) 2.4..6.0 m oralig'ida
    std::map<long, int> votes;
    for (std::size_t i = 0; i < lv.size(); ++i)
        for (std::size_t j = i + 1; j < lv.size(); ++j) {
            const double d = lv[j] - lv[i];
            if (d >= 2.4 && d <= 6.0) votes[std::lround(d * 20)]++;
        }
    double bestH = 0;
    int bestN = 0;
    for (const auto& [k, c] : votes) {
        const double h = k / 20.0;
        int n = 0;   // zanjir: h, 2h, 3h ... (har biri belgilarda bor)
        while (std::any_of(lv.begin(), lv.end(), [&](double v) { return std::abs(v - (n + 1) * h) < 0.06; })) ++n;
        // aniq qadam: zanjirning oxirgi a'zosidan
        if (n > bestN || (n == bestN && c > 0 && h < bestH)) {
            bestN = n;
            double last = 0;
            for (double v : lv)
                if (std::abs(v - n * h) < 0.06) last = v;
            bestH = n > 0 ? last / n : h;
        }
    }
    if (bestN < 1) return false;
    height = bestH;
    floors = bestN;
    return true;
}

std::vector<ArView> detectArViews(const Document& doc, const std::vector<DxfDimInfo>& dims) {
    std::vector<ArView> out;
    const auto kv = detectKjViews(doc, dims);
    // matnlar (kesim / fasad sarlavhalari KJ sarlavha qidiruvida yo'q — qisqa va boshqa so'zlar)
    struct T { Vec3 c; double h; std::string s; };
    std::vector<T> texts;
    std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
        if (e.type() == EntityType::Insert) {
            for (const auto& c : static_cast<const InsertEntity&>(e).explode()) visit(*c);
        } else if (e.type() == EntityType::Text) {
            const auto& t = static_cast<const TextEntity&>(e);
            if (!t.text().empty()) texts.push_back({mid(t.bbox()), t.height(), t.text()});
        }
    };
    doc.forEach(visit);
    for (const auto& k : kv) {
        ArView v;
        v.box = k.box;
        v.scale = k.scale;
        v.origin = k.origin;
        v.aligned = k.aligned;
        v.levels = k.levels;
        v.title = k.title;
        v.kind = classifyArTitle(v.title, v.floorFrom, v.floorTo);
        if (v.kind == ArViewKind::Other && k.axesY.empty()) {
            // faqat bitta qator pufakchalar (pastda) — kesim yoki fasad: ko'rinish yuqoriga cho'ziladi
            const double w = v.box.max.x - v.box.min.x;
            v.box.min.x -= w * 0.25;
            v.box.max.x += w * 0.25;
            v.box.max.y = v.box.min.y + w * 1.6;
            v.levels.clear();
            for (const auto& t : texts) {
                double lv;
                if (inBox(v.box, t.c) && parseLevelText(t.s, lv) && std::abs(lv) < 200) v.levels.push_back(lv);
            }
            std::sort(v.levels.begin(), v.levels.end());
            v.levels.erase(std::unique(v.levels.begin(), v.levels.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }), v.levels.end());
        }
        if (v.kind == ArViewKind::Other) {
            // yaqin qisqa sarlavha: "Разрез 1-1", "Фасад 1-4", "Kesim A-A"
            const double w = v.box.max.x - v.box.min.x, h = v.box.max.y - v.box.min.y;
            double bd = 1e18;
            for (const auto& t : texts) {
                int a = 0, b = 0;
                const ArViewKind kk = classifyArTitle(t.s, a, b);
                if (kk == ArViewKind::Other) continue;
                const double dx = std::max({v.box.min.x - t.c.x, 0.0, t.c.x - v.box.max.x});
                const double dy = std::max({v.box.min.y - t.c.y, 0.0, t.c.y - v.box.max.y});
                const double d = std::hypot(dx, dy);
                if (d > std::max(w, h) * 0.5 || d >= bd) continue;
                bd = d;
                v.kind = kk;
                v.title = t.s;
                v.floorFrom = a;
                v.floorTo = b;
            }
        }
        // birlik: o'qlar oralig'i katta (> 300) — mm
        double span = 0;
        for (const auto& [n, x] : k.axesX) span = std::max(span, std::abs(x));
        for (const auto& [n, y] : k.axesY) span = std::max(span, std::abs(y));
        if (span > 300) v.scale *= 0.001;
        out.push_back(std::move(v));
    }
    // o'q pufakchalarisiz kesim / fasad: sarlavha atrofidagi belgilar (sarlavha shriftining 60 barobari radiusida)
    const bool anySection = std::any_of(out.begin(), out.end(), [](const ArView& v) { return v.kind == ArViewKind::Section || v.kind == ArViewKind::Facade; });
    if (!anySection)
        for (const auto& t : texts) {
            int a = 0, b = 0;
            const ArViewKind kk = classifyArTitle(t.s, a, b);
            if (kk != ArViewKind::Section && kk != ArViewKind::Facade) continue;
            bool inPlan = false;
            for (const auto& v : out) inPlan = inPlan || inBox(v.box, t.c);
            if (inPlan) continue;
            ArView v;
            v.kind = kk;
            v.title = t.s;
            const double r = t.h * 60;
            v.box.expand({t.c.x - r, t.c.y - r * 1.5, 0});
            v.box.expand({t.c.x + r, t.c.y + r * 0.2, 0});
            for (const auto& q : texts) {
                double lv;
                if (inBox(v.box, q.c) && parseLevelText(q.s, lv) && std::abs(lv) < 200) v.levels.push_back(lv);
            }
            std::sort(v.levels.begin(), v.levels.end());
            if (!v.levels.empty()) out.push_back(std::move(v));
        }
    return out;
}

ArProject buildArProject(const Document& doc, const std::vector<DxfDimInfo>& dims, const ArParams& p, const std::vector<ArView>* given) {
    ArProject r;
    r.views = given ? *given : detectArViews(doc, dims);
    auto issue = [&](bool err, const char* key, std::vector<std::string> a = {}) { r.issues.push_back({err, key, std::move(a)}); };

    // ---- qavat balandligi va soni: kesim / fasad belgilaridan
    for (const auto& v : r.views)
        if (v.use && (v.kind == ArViewKind::Section || v.kind == ArViewKind::Facade))
            for (double l : v.levels) r.sectionLevels.push_back(l);
    std::sort(r.sectionLevels.begin(), r.sectionLevels.end());
    r.sectionLevels.erase(std::unique(r.sectionLevels.begin(), r.sectionLevels.end(), [](double a, double b) { return std::abs(a - b) < 0.005; }),
                          r.sectionLevels.end());
    double H = 0;
    int nSec = 0;
    if (floorChain(r.sectionLevels, H, nSec)) r.heightFromSection = true;
    else if (std::none_of(r.views.begin(), r.views.end(), [](const ArView& v) { return v.kind == ArViewKind::Section || v.kind == ArViewKind::Facade; }))
        issue(false, "ar.chk.no_section");
    else
        issue(false, "ar.chk.section_levels");
    r.floorHeight = p.floorHeight > 0 ? p.floorHeight : (H > 0 ? H : (p.building.floorHeight > 0 ? p.building.floorHeight : 3.0));

    // ---- rejalar
    std::vector<const ArView*> plans;
    int maxPlanFloor = 0;
    for (const auto& v : r.views)
        if (v.use && v.kind == ArViewKind::Plan) {
            plans.push_back(&v);
            maxPlanFloor = std::max(maxPlanFloor, v.floorTo);
        }
    if (plans.empty()) {
        issue(true, "ar.chk.no_plan");
        return r;
    }
    r.floors = p.floors > 0 ? p.floors : std::max(nSec, std::max(maxPlanFloor, 1));
    // "Tipik" reja raqamsiz bo'lsa: 1-qavatdan keyingi barcha qavatlar
    std::vector<std::pair<int, int>> ranges;
    for (const auto* v : plans) {
        int a = v->floorFrom, b = v->floorTo;
        if (a == 0) { a = maxPlanFloor >= 1 ? 2 : 1; b = r.floors; }
        ranges.push_back({a, std::max(a, b)});
    }

    std::vector<Ent> ents;
    std::vector<std::unique_ptr<IEntity>> owned;
    collectAll(doc, owned, ents);

    FloorPlans fp;
    std::optional<PlanInput> first;
    std::vector<ArView> views = r.views;   // statistika yoziladi
    for (std::size_t i = 0; i < plans.size(); ++i) {
        const ArView& v = *plans[i];
        Document local;
        for (const auto& [n, l] : doc.layers()) local.addLayer(l);
        int win = 0, door = 0;
        std::vector<OpeningMark> marks;
        auto toLocal = [&](const Vec3& q) { return Vec3{(q.x - v.origin.x) * v.scale, (q.y - v.origin.y) * v.scale, 0}; };
        for (const auto& e : ents) {
            if (!inBox(v.box, mid(e.box))) continue;
            if (e.e->type() == EntityType::Insert) {
                const std::string bn = utf8::toUpper(static_cast<const InsertEntity&>(*e.e).block()->name);
                const bool w = isWindowBlock(bn), d = !w && isDoorBlock(bn);
                if (w || d) {
                    // eni — blok qutisining uzun tomoni (eshik yoyi kvadrat — eni o'sha)
                    const double bw = std::max(e.box.max.x - e.box.min.x, e.box.max.y - e.box.min.y) * v.scale;
                    marks.push_back({toLocal(static_cast<const InsertEntity&>(*e.e).insertionPoint()), bw, d});
                    ++(w ? win : door);
                    continue;
                }
            }
            auto c = e.e->clone();
            c->transform(Transform::translation({-v.origin.x, -v.origin.y, 0}));
            c->transform(Transform::scaling({0, 0, 0}, v.scale));
            local.add(std::move(c));
        }
        PlanInput pl = readPlan(local, autoLayerMap(local));
        pl.marks = std::move(marks);
        for (auto& vv : views)
            if (std::abs(vv.box.min.x - v.box.min.x) < 1e-9 && std::abs(vv.box.min.y - v.box.min.y) < 1e-9) {
                vv.walls = static_cast<int>(pl.walls.size() + pl.wallLines.size());
                vv.columns = static_cast<int>(pl.columns.size());
                vv.infill = static_cast<int>(pl.infill.size() + pl.infillLines.size());
                vv.windows = win;
                vv.doors = door;
            }
        if (pl.empty()) {
            issue(false, "ar.chk.plan_empty", {v.title});
            continue;
        }
        pl.levelTop.reset();       // reja belgisi — pol; plita usti qavat zanjiridan
        pl.levelSet = {};
        const auto [a, b] = ranges[i];
        for (int k = a; k <= b && k <= r.floors; ++k) {
            fp.floors[k] = pl;
            r.windowsByFloor[k] = win;
            r.doorsByFloor[k] = door;
        }
        if (!first || a == 1) first = pl;
    }
    r.views = std::move(views);
    if (!first) {
        issue(true, "ar.chk.no_plan");
        return r;
    }
    // berilmagan qavatlar — pastdagi eng yaqin reja
    for (int k = 1; k <= r.floors; ++k)
        if (!fp.floors.count(k)) {
            for (int j = k - 1; j >= 1; --j)
                if (auto it = fp.floors.find(j); it != fp.floors.end()) {
                    fp.floors[k] = it->second;
                    r.windowsByFloor[k] = r.windowsByFloor[j];
                    r.doorsByFloor[k] = r.doorsByFloor[j];
                    break;
                }
            if (!fp.floors.count(k)) fp.floors[k] = *first;
        }
    // plita usti belgilari: k·H (kesimdagi aniq belgilar bo'lsa — o'shalar)
    for (auto& [k, pl] : fp.floors) {
        double top = k * r.floorHeight;
        for (double l : r.sectionLevels)
            if (std::abs(l - top) < 0.06) top = l;
        pl.levelTop = top;
    }
    r.params = p.building;
    r.params.floors = r.floors;
    r.params.floorHeight = r.floorHeight;
    r.bm = buildModel(fp.floors.at(1), r.params, fp);
    return r;
}

} // namespace asiacad::bim
