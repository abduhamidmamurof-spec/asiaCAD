// AsiaCAD — BIM modeldan smeta (5D): QTO, narxlar bazasi, Excel smeta.
#include <asiacad/bim/smeta.h>

#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace asiacad::bim {

namespace {

std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> out;
    std::size_t p = 0;
    while (p <= s.size()) {
        const auto q = s.find('|', p);
        std::string t = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
        if (!t.empty()) out.push_back(utf8::toUpper(t));
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return out;
}

bool anyEqual(const std::string& pattern, const std::string& value) {
    if (pattern.empty()) return true;
    const std::string v = utf8::toUpper(value);
    for (const auto& t : tokens(pattern))
        if (t == v) return true;
    return false;
}

bool anyContains(const std::string& pattern, const std::vector<std::string>& values) {
    if (pattern.empty()) return true;
    std::vector<std::string> up;
    for (const auto& v : values) up.push_back(utf8::toUpper(v));
    for (const auto& t : tokens(pattern))
        for (const auto& v : up)
            if (v.find(t) != std::string::npos) return true;
    return false;
}

bool matches(const SmetaRule& r, const IfcElement& e) {
    return anyEqual(r.category, ifcCategory(e.type)) && anyEqual(r.ifcType, e.type) && anyEqual(r.predefined, e.predefined) &&
           anyContains(r.material, {e.material}) && anyContains(r.name, {e.name, e.objectType, e.tag});
}

double triArea(const std::array<Vec3, 3>& t, double& nz) {
    const Vec3 a = t[1] - t[0], b = t[2] - t[0];
    const Vec3 c{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    const double l = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
    nz = l > 1e-12 ? c.z / l : 0;
    return 0.5 * l;
}

bool propNum(const IfcElement& e, const char* suffix, double& out) {
    const std::string suf = std::string(".") + suffix;
    for (const auto& [k, v] : e.props) {
        if (k.size() < suf.size() || k.compare(k.size() - suf.size(), suf.size(), suf) != 0) continue;
        if (k.rfind("Qto_", 0) != 0 && k.find("BaseQuantities") == std::string::npos) continue;
        char* end = nullptr;
        const double d = std::strtod(v.c_str(), &end);
        if (end != v.c_str() && std::isfinite(d) && d > 0) { out = d; return true; }
    }
    return false;
}

std::string text(const xlsx::Sheet& s, std::size_t r, std::size_t c) { return s.str(r, c); }
double number(const xlsx::Sheet& s, std::size_t r, std::size_t c, double def = 0) {
    if (auto v = s.num(r, c)) return *v;
    return def;
}

} // namespace

SmetaMeasure smetaMeasureFromText(const std::string& s0) {
    const std::string s = utf8::toUpper(s0);
    if (s == "VOLUME" || s == "HAJM" || s == "M3" || s == "M³") return SmetaMeasure::Volume;
    if (s == "AREA" || s == "YUZA" || s == "SIDE") return SmetaMeasure::AreaSide;
    if (s == "PLAN" || s == "REJA" || s == "TOP") return SmetaMeasure::AreaPlan;
    if (s == "LENGTH" || s == "UZUNLIK") return SmetaMeasure::Length;
    if (s == "COUNT" || s == "SONI" || s == "DONA") return SmetaMeasure::Count;
    if (s == "MASS" || s == "MASSA" || s == "KG") return SmetaMeasure::Mass;
    if (s == "OPENING" || s == "TESHIK") return SmetaMeasure::Opening;
    return SmetaMeasure::Volume;
}

const char* smetaMeasureName(SmetaMeasure m) {
    switch (m) {
    case SmetaMeasure::Volume: return "VOLUME";
    case SmetaMeasure::AreaSide: return "AREA";
    case SmetaMeasure::AreaPlan: return "PLAN";
    case SmetaMeasure::Length: return "LENGTH";
    case SmetaMeasure::Count: return "COUNT";
    case SmetaMeasure::Mass: return "MASS";
    case SmetaMeasure::Opening: return "OPENING";
    }
    return "VOLUME";
}

SmetaQty smetaMeasureElement(const IfcElement& e) {
    SmetaQty q;
    q.volume = e.volume;
    q.mass = e.barMass;
    for (const auto& p : e.parts)
        for (const auto& t : p.tris) {
            double nz = 0;
            const double a = triArea(t, nz);
            if (std::fabs(nz) < 0.3) q.areaSide += a;
            else if (nz > 0.7) q.areaPlan += a;
        }
    q.areaSide *= 0.5;   // ingichka element: ikki tomoni — bitta tomon
    if (e.box.valid()) {
        const double dx = e.box.max.x - e.box.min.x, dy = e.box.max.y - e.box.min.y, dz = e.box.max.z - e.box.min.z;
        q.length = std::max({dx, dy, dz});
    }
    if (e.barLength > 0) q.length = e.barLength;
    if (auto w = e.num.find("OverallWidth"); w != e.num.end())
        if (auto h = e.num.find("OverallHeight"); h != e.num.end()) q.opening = w->second * h->second;
    if (q.opening <= 0) q.opening = q.areaSide;
    // IFC Qto (Revit / ArchiCAD hisoblagan aniq miqdorlar) ustun turadi
    double v = 0;
    if (propNum(e, "NetVolume", v) || propNum(e, "GrossVolume", v)) { q.volume = v; q.fromQto = true; }
    if (propNum(e, "NetSideArea", v) || propNum(e, "GrossSideArea", v)) { q.areaSide = v; q.fromQto = true; }
    if (propNum(e, "NetArea", v) || propNum(e, "GrossArea", v)) {
        const std::string cat = ifcCategory(e.type);
        if (cat == "WINDOW" || cat == "DOOR") q.opening = v; else q.areaPlan = v;
        q.fromQto = true;
    }
    if (propNum(e, "Length", v)) { q.length = v; q.fromQto = true; }
    if (q.mass <= 0 && propNum(e, "NetWeight", v)) q.mass = v;
    return q;
}

double smetaQuantity(const SmetaQty& q, SmetaMeasure m) {
    switch (m) {
    case SmetaMeasure::Volume: return q.volume;
    case SmetaMeasure::AreaSide: return q.areaSide;
    case SmetaMeasure::AreaPlan: return q.areaPlan;
    case SmetaMeasure::Length: return q.length;
    case SmetaMeasure::Count: return 1.0;
    case SmetaMeasure::Mass: return q.mass;
    case SmetaMeasure::Opening: return q.opening;
    }
    return 0;
}

SmetaResult computeSmeta(const IfcModel& m, const SmetaBase& base) {
    SmetaResult r;
    std::vector<SmetaLine> lines(base.items.size());
    std::map<std::string, SmetaUnmatched> un;
    for (std::size_t i = 0; i < m.elements.size(); ++i) {
        const auto& e = m.elements[i];
        const std::string cat = ifcCategory(e.type);
        if (cat == "SPACE" || cat == "SITE" || (cat == "OTHER" && e.parts.empty())) continue;
        ++r.elements;
        const SmetaQty q = smetaMeasureElement(e);
        r.fromQto += q.fromQto;
        bool any = false;
        for (std::size_t k = 0; k < base.items.size(); ++k) {
            const auto& it = base.items[k];
            if (!matches(it.rule, e)) continue;
            const double v = smetaQuantity(q, it.measure) * it.factor;
            auto& L = lines[k];
            L.item = static_cast<int>(k);
            ++L.elements;
            L.qty += v;
            r.rows.push_back({static_cast<int>(i), static_cast<int>(k), v});
            any = true;
            if (!it.cont) break;
        }
        if (any) ++r.matched;
        else {
            auto& u = un[cat];
            u.category = cat;
            ++u.count;
            u.volume += q.volume;
            r.rows.push_back({static_cast<int>(i), -1, 0});
        }
    }
    for (auto& L : lines) {
        if (L.item < 0) continue;
        const auto& it = base.items[L.item];
        L.material = L.qty * it.material;
        L.labor = L.qty * it.labor;
        L.machine = L.qty * it.machine;
        L.total = L.material + L.labor + L.machine;
        r.material += L.material;
        r.labor += L.labor;
        r.machine += L.machine;
        r.lines.push_back(L);
    }
    for (auto& [k, u] : un) r.unmatched.push_back(u);
    std::sort(r.unmatched.begin(), r.unmatched.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    r.direct = r.material + r.labor + r.machine;
    r.overhead = r.direct * base.overheadPct / 100.0;
    r.profit = (r.direct + r.overhead) * base.profitPct / 100.0;
    r.vat = (r.direct + r.overhead + r.profit) * base.vatPct / 100.0;
    r.total = r.direct + r.overhead + r.profit + r.vat;
    return r;
}

SmetaBase defaultSmetaBase() {
    // NAMUNA: narxlar shartli (1 birlik uchun, so'm) — haqiqiy smeta uchun o'z bazangiz bilan almashtiring.
    SmetaBase b;
    b.title = "NAMUNA baza — narxlar shartli";
    b.overheadPct = 12;
    b.profitPct = 8;
    b.vatPct = 12;
    auto add = [&](const char* code, const char* name, const char* sec, const char* unit, SmetaMeasure ms, double f, double mat, double lab, double mach,
                   SmetaRule rule, bool cont = false) {
        SmetaItem it;
        it.code = code; it.name = name; it.section = sec; it.unit = unit; it.measure = ms; it.factor = f;
        it.material = mat; it.labor = lab; it.machine = mach; it.rule = std::move(rule); it.cont = cont;
        b.items.push_back(std::move(it));
    };
    const char* S1 = "01 Yer osti va poydevor";
    const char* S2 = "02 Monolit temir-beton";
    const char* S3 = "03 Armatura";
    const char* S4 = "04 Devorlar";
    const char* S5 = "05 Deraza, eshik, vitraj";
    const char* S6 = "06 Tom, pardoz, to'siqlar";
    add("01-001", "Burg'ilangan qoziq, beton B25", S1, "m³", SmetaMeasure::Volume, 1.0, 1150000, 220000, 310000, {"PILE", "", "", "", ""});
    add("01-002", "Monolit poydevor (plita, lenta), beton B25", S1, "m³", SmetaMeasure::Volume, 1.0, 1100000, 180000, 90000, {"FOOTING", "", "", "", ""});
    add("02-001", "Monolit plita (yopma), beton B25", S2, "m³", SmetaMeasure::Volume, 1.0, 1100000, 260000, 95000, {"SLAB", "", "", "", ""});
    add("02-002", "Monolit ustun, beton B25", S2, "m³", SmetaMeasure::Volume, 1.0, 1100000, 340000, 95000, {"COLUMN", "", "", "", ""});
    add("02-003", "Monolit to'sin / rigel, beton B25", S2, "m³", SmetaMeasure::Volume, 1.0, 1100000, 320000, 95000, {"BEAM", "", "", "", ""});
    add("02-004", "Monolit devor / perda, beton B25", S2, "m³", SmetaMeasure::Volume, 1.0, 1100000, 300000, 95000,
        {"WALL", "", "BETON|CONCRETE|БЕТОН|B25|B30|C25|C30|ЖБ|RC", "", ""});
    add("02-005", "Monolit zina marshi va maydonchasi", S2, "m³", SmetaMeasure::Volume, 1.0, 1150000, 420000, 95000, {"STAIR", "", "", "", ""});
    add("03-001", "Armatura A500C (o'rnatish bilan)", S3, "t", SmetaMeasure::Mass, 0.001, 9800000, 1900000, 250000, {"REBAR", "", "", "", ""});
    add("04-001", "Gazoblok devor D500", S4, "m³", SmetaMeasure::Volume, 1.02, 620000, 210000, 15000,
        {"WALL", "", "GAZOBLOK|GAZOBETON|AERATED|AAC|ГАЗОБЕТОН|ГАЗОБЛОК|D500|D600", "", ""});
    add("04-002", "G'isht devor", S4, "m³", SmetaMeasure::Volume, 1.02, 900000, 380000, 15000, {"WALL", "", "G'ISHT|GʻISHT|GISHT|BRICK|КИРПИЧ", "", ""});
    add("04-003", "Boshqa devorlar (materiali aniqlanmagan)", S4, "m³", SmetaMeasure::Volume, 1.0, 700000, 250000, 15000, {"WALL", "", "", "", ""});
    add("05-001", "Deraza bloki (PVX / alyumin, ikki qavatli oyna)", S5, "m²", SmetaMeasure::Opening, 1.0, 950000, 120000, 0, {"WINDOW", "", "", "", ""});
    add("05-002", "Eshik bloki", S5, "m²", SmetaMeasure::Opening, 1.0, 1300000, 150000, 0, {"DOOR", "", "", "", ""});
    add("05-003", "Vitraj (curtain wall)", S5, "m²", SmetaMeasure::AreaSide, 1.0, 1600000, 250000, 30000, {"CURTAIN|PLATE", "", "", "", ""});
    add("06-001", "Tom (qatlamlari bilan)", S6, "m²", SmetaMeasure::AreaPlan, 1.0, 420000, 110000, 10000, {"ROOF", "", "", "", ""});
    add("06-002", "Pardoz / qoplama", S6, "m²", SmetaMeasure::AreaPlan, 1.0, 180000, 90000, 0, {"COVERING", "", "", "", ""});
    add("06-003", "To'siq / panjara", S6, "m", SmetaMeasure::Length, 1.0, 450000, 90000, 0, {"RAILING", "", "", "", ""});
    return b;
}

xlsx::Workbook smetaBaseWorkbook(const SmetaBase& b, const SmetaTr& tr) {
    using xlsx::Cell;
    xlsx::Workbook wb;
    auto& s = wb.add(tr("smeta.xl.base"));
    std::vector<Cell> h;
    for (const char* k : {"code", "name", "section", "unit", "measure", "factor", "mat", "lab", "mach", "cat", "type", "matkey", "namekey", "predef", "cont"})
        h.push_back(Cell::s(tr(std::string("smeta.xl.col.") + k), 1));
    s.add(h);
    for (const auto& it : b.items)
        s.add({Cell::s(it.code), Cell::s(it.name), Cell::s(it.section), Cell::s(it.unit), Cell::s(smetaMeasureName(it.measure)), Cell::n(it.factor, 3),
               Cell::n(it.material, 5), Cell::n(it.labor, 5), Cell::n(it.machine, 5), Cell::s(it.rule.category), Cell::s(it.rule.ifcType),
               Cell::s(it.rule.material), Cell::s(it.rule.name), Cell::s(it.rule.predefined), Cell::n(it.cont ? 1 : 0, 5)});
    s.widths = {10, 46, 28, 8, 10, 8, 13, 13, 13, 16, 18, 34, 18, 14, 7};
    auto& p = wb.add(tr("smeta.xl.params"));
    p.autoFilter = false;
    p.add({Cell::s(tr("smeta.xl.p.title"), 1), Cell::s(b.title)});
    p.add({Cell::s(tr("smeta.xl.p.currency"), 1), Cell::s(b.currency)});
    p.add({Cell::s(tr("smeta.xl.p.overhead"), 1), Cell::n(b.overheadPct)});
    p.add({Cell::s(tr("smeta.xl.p.profit"), 1), Cell::n(b.profitPct)});
    p.add({Cell::s(tr("smeta.xl.p.vat"), 1), Cell::n(b.vatPct)});
    p.add({});
    p.add({Cell::s(tr("smeta.xl.p.help"), 7)});
    p.widths = {26, 50};
    return wb;
}

SmetaBase smetaBaseFromWorkbook(const xlsx::Workbook& wb) {
    if (wb.sheets.empty()) throw Error("smeta.err.base", {"-"});
    SmetaBase b;
    const auto& s = wb.sheets[0];
    for (std::size_t r = 1; r < s.rows.size(); ++r) {
        SmetaItem it;
        it.code = text(s, r, 0);
        it.name = text(s, r, 1);
        if (it.code.empty() && it.name.empty()) continue;
        it.section = text(s, r, 2);
        it.unit = text(s, r, 3);
        it.measure = smetaMeasureFromText(text(s, r, 4));
        it.factor = number(s, r, 5, 1.0);
        if (it.factor == 0) it.factor = 1.0;
        it.material = number(s, r, 6);
        it.labor = number(s, r, 7);
        it.machine = number(s, r, 8);
        it.rule.category = text(s, r, 9);
        it.rule.ifcType = text(s, r, 10);
        it.rule.material = text(s, r, 11);
        it.rule.name = text(s, r, 12);
        it.rule.predefined = text(s, r, 13);
        it.cont = number(s, r, 14) != 0;
        b.items.push_back(std::move(it));
    }
    if (b.items.empty()) throw Error("smeta.err.base", {s.name});
    if (wb.sheets.size() > 1) {
        const auto& p = wb.sheets[1];
        if (auto t = text(p, 0, 1); !t.empty()) b.title = t;
        if (auto t = text(p, 1, 1); !t.empty()) b.currency = t;
        b.overheadPct = number(p, 2, 1, b.overheadPct);
        b.profitPct = number(p, 3, 1, b.profitPct);
        b.vatPct = number(p, 4, 1, b.vatPct);
    }
    return b;
}

xlsx::Workbook smetaWorkbook(const IfcModel& m, const SmetaBase& b, const SmetaResult& r, const SmetaTr& tr) {
    using xlsx::Cell;
    using xlsx::ref;
    xlsx::Workbook wb;
    auto& s = wb.add(tr("smeta.xl.sheet"));
    s.autoFilter = false;
    s.freezeHeader = false;
    s.add({Cell::s(b.title, 1)});
    s.add({Cell::s(tr("smeta.xl.model")), Cell::s(m.project.empty() ? (m.building.empty() ? "—" : m.building) : m.project),
           Cell::s(m.schema + (m.application.empty() ? "" : " · " + m.application))});
    s.add({Cell::s(tr("smeta.xl.currency")), Cell::s(b.currency)});
    s.add({});
    std::vector<Cell> h;
    for (const char* k : {"no", "code", "name", "unit", "qty", "mat", "lab", "mach", "matsum", "labsum", "machsum", "total"})
        h.push_back(Cell::s(tr(std::string("smeta.xl.col.") + k), 1));
    s.add(h);
    // bo'limlar bo'yicha
    std::vector<std::string> order;
    std::map<std::string, std::vector<const SmetaLine*>> bySec;
    for (const auto& L : r.lines) {
        const auto& sec = b.items[L.item].section;
        if (!bySec.count(sec)) order.push_back(sec);
        bySec[sec].push_back(&L);
    }
    int no = 0;
    std::vector<int> subRows;
    for (const auto& sec : order) {
        s.add({Cell::s(""), Cell::s(""), Cell::s(sec.empty() ? "—" : sec, 1)});
        const int first = static_cast<int>(s.rows.size());
        double sm = 0, sl = 0, sh = 0, st = 0;
        for (const SmetaLine* L : bySec[sec]) {
            const auto& it = b.items[L->item];
            const int row = static_cast<int>(s.rows.size());
            s.add({Cell::n(++no, 5), Cell::s(it.code), Cell::s(it.name), Cell::s(it.unit), Cell::n(L->qty, 3), Cell::n(it.material), Cell::n(it.labor),
                   Cell::n(it.machine), Cell::f(ref(row, 4) + "*" + ref(row, 5), L->material), Cell::f(ref(row, 4) + "*" + ref(row, 6), L->labor),
                   Cell::f(ref(row, 4) + "*" + ref(row, 7), L->machine), Cell::f(ref(row, 8) + "+" + ref(row, 9) + "+" + ref(row, 10), L->total)});
            sm += L->material; sl += L->labor; sh += L->machine; st += L->total;
        }
        const int last = static_cast<int>(s.rows.size()) - 1;
        auto sum = [&](int c, double v) { return Cell::f("SUM(" + ref(first, c) + ":" + ref(last, c) + ")", v, 4); };
        subRows.push_back(static_cast<int>(s.rows.size()));
        s.add({Cell::s(""), Cell::s(""), Cell::s(tr("smeta.xl.subtotal"), 1), Cell::s(""), Cell::s(""), Cell::s(""), Cell::s(""), Cell::s(""),
               sum(8, sm), sum(9, sl), sum(10, sh), sum(11, st)});
    }
    s.add({});
    auto subs = [&](int c) {
        std::string f;
        for (int rr : subRows) f += (f.empty() ? "" : "+") + ref(rr, c);
        return f.empty() ? std::string("0") : f;
    };
    const int dRow = static_cast<int>(s.rows.size());
    s.add({Cell::s(""), Cell::s(""), Cell::s(tr("smeta.xl.direct"), 1), Cell::s(""), Cell::s(""), Cell::s(""), Cell::s(""), Cell::s(""),
           Cell::f(subs(8), r.material, 4), Cell::f(subs(9), r.labor, 4), Cell::f(subs(10), r.machine, 4), Cell::f(subs(11), r.direct, 4)});
    const int oRow = dRow + 1;
    s.add({Cell::s(""), Cell::s(""), Cell::s(tr("smeta.xl.overhead")), Cell::n(b.overheadPct, 2), Cell::s("%"), Cell::s(""), Cell::s(""), Cell::s(""),
           Cell::s(""), Cell::s(""), Cell::s(""), Cell::f(ref(dRow, 11) + "*" + ref(oRow, 3) + "/100", r.overhead, 4)});
    const int pRow = dRow + 2;
    s.add({Cell::s(""), Cell::s(""), Cell::s(tr("smeta.xl.profit")), Cell::n(b.profitPct, 2), Cell::s("%"), Cell::s(""), Cell::s(""), Cell::s(""),
           Cell::s(""), Cell::s(""), Cell::s(""), Cell::f("(" + ref(dRow, 11) + "+" + ref(oRow, 11) + ")*" + ref(pRow, 3) + "/100", r.profit, 4)});
    const int vRow = dRow + 3;
    s.add({Cell::s(""), Cell::s(""), Cell::s(tr("smeta.xl.vat")), Cell::n(b.vatPct, 2), Cell::s("%"), Cell::s(""), Cell::s(""), Cell::s(""),
           Cell::s(""), Cell::s(""), Cell::s(""),
           Cell::f("(" + ref(dRow, 11) + "+" + ref(oRow, 11) + "+" + ref(pRow, 11) + ")*" + ref(vRow, 3) + "/100", r.vat, 4)});
    s.add({Cell::s(""), Cell::s(""), Cell::s(tr("smeta.xl.total"), 1), Cell::s(""), Cell::s(""), Cell::s(""), Cell::s(""), Cell::s(""),
           Cell::s(""), Cell::s(""), Cell::s(""),
           Cell::f(ref(dRow, 11) + "+" + ref(oRow, 11) + "+" + ref(pRow, 11) + "+" + ref(vRow, 11), r.total, 4)});
    s.widths = {5, 10, 48, 7, 12, 13, 13, 13, 16, 16, 16, 18};

    // Hajmlar (har element)
    auto& q = wb.add(tr("smeta.xl.qty"));
    std::vector<Cell> qh;
    for (const char* k : {"guid", "cat", "ename", "storey", "material", "code", "qty", "unit"})
        qh.push_back(Cell::s(tr(std::string("smeta.xl.col.") + k), 1));
    q.add(qh);
    for (const auto& row : r.rows) {
        const auto& e = m.elements[row.element];
        const std::string st = e.storey >= 0 && e.storey < static_cast<int>(m.storeys.size()) ? m.storeys[e.storey].name : "";
        if (row.item >= 0) {
            const auto& it = b.items[row.item];
            q.add({Cell::s(e.guid), Cell::s(ifcCategory(e.type)), Cell::s(e.name), Cell::s(st), Cell::s(e.material), Cell::s(it.code), Cell::n(row.qty, 3),
                   Cell::s(it.unit)});
        } else {
            q.add({Cell::s(e.guid), Cell::s(ifcCategory(e.type)), Cell::s(e.name), Cell::s(st), Cell::s(e.material), Cell::s(tr("smeta.xl.none"), 7)});
        }
    }
    q.widths = {24, 10, 40, 18, 30, 10, 12, 7};

    // Moslanmaganlar
    auto& u = wb.add(tr("smeta.xl.unmatched"));
    u.add({Cell::s(tr("smeta.xl.col.cat"), 1), Cell::s(tr("smeta.xl.col.count"), 1), Cell::s(tr("smeta.xl.col.volume"), 1)});
    for (const auto& x : r.unmatched) u.add({Cell::s(x.category), Cell::n(x.count, 5), Cell::n(x.volume, 3)});
    u.widths = {16, 10, 14};
    return wb;
}

} // namespace asiacad::bim
