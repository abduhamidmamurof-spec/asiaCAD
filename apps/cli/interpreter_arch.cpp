// AsiaCAD CLI — arxitektura (birlik — metr):
//   WALL p1 p2 [p ...] [T=0.3] [H=3] [J=C|L|R] [CLOSE]     aqlli devor(lar), burchaklar tutashadi
//   DOOR p [W=0.9] [H=2.1] [RIGHT] [OUT] | DOOR p DEL       eshik: eng yaqin devorga, devor qayta chiziladi
//   WINDOW p [W=1.5] [H=1.5] [SILL=0.9] | WINDOW p DEL      deraza
//   ROOM p [NAME=..] [TYPE=LIVING|OFFICE|CORRIDOR|WC|TECH|STAIR|...] [RENT=0|1] [TH=0.25]
//   ROOMS [XLSX=fayl]                                        xonalar jadvali: netto, o'qlar bo'yicha, ijara; bino brutto
//   ARSECTION p1 p2 AT=x,y [FLOORS=n] [SLAB=0.2] [DEPTH=d] [TITLE=..] [TH=0.25]   kesim (yoki tashqaridan — fasad)
//   AR3D [FLOORS=n] [SLAB=0.2]                              devorlar va plitalar 3D (MeshEntity) — 3D ko'rinish va render eksporti uchun
//   ARSHEET nom [A0..A4]                                     reja va kesimlar bilan list (so'ng PLOT … SHEET=nom)
#include "interpreter.h"

#include <asiacad/bim/arch.h>
#include <asiacad/bim/building.h>
#include <asiacad/core/commands.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/xlsx.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <set>

namespace asiacad::cli {

struct ArchOpts {
    std::vector<std::string> pos;
    std::map<std::string, std::string> kv;
    std::set<std::string> flags;
};
static ArchOpts opts(const std::vector<std::string>& a, std::size_t from) {
    ArchOpts o;
    for (std::size_t i = from; i < a.size(); ++i) {
        const auto eq = a[i].find('=');
        if (eq != std::string::npos && eq > 0 && std::isalpha(static_cast<unsigned char>(a[i][0]))) o.kv[utf8::toUpper(a[i].substr(0, eq))] = a[i].substr(eq + 1);
        else if (!a[i].empty() && std::isalpha(static_cast<unsigned char>(a[i][0])) && a[i].find(',') == std::string::npos) o.flags.insert(utf8::toUpper(a[i]));
        else o.pos.push_back(a[i]);
    }
    return o;
}

// Devorni qayta chizish: eski bo'laklar o'chiriladi, yangilari qo'shiladi (bitta undo qadami)
void Interpreter::replaceWall(const bim::ArchWall& w, const std::string& name) {
    std::vector<EntityId> old;
    doc_.forEach([&](const IEntity& e) { if (e.xget("aw.wall") == w.id) old.push_back(e.id()); });
    auto comp = std::make_unique<CompositeCommand>(name);
    if (!old.empty()) comp->add(std::make_unique<DeleteCommand>(old));
    for (auto& e : bim::wallEntities(w)) comp->add(std::make_unique<AddEntityCommand>(std::move(e)));
    stack_.execute(std::move(comp));
}

bool Interpreter::dispatchArch(const std::string& cmd, const Args& a) {
    // bayroqlar har tilda: kalit so'zlar kanonik nomga
    auto opts = [this](const std::vector<std::string>& v, std::size_t from) {
        ArchOpts o = cli::opts(v, from);
        std::set<std::string> f;
        for (const auto& x : o.flags) {
            const std::string k = loc_.resolveKeyword(x);
            f.insert(k.empty() ? x : k);
        }
        o.flags = f;
        return o;
    };
    if (cmd == "WALL") {
        const ArchOpts o = opts(a, 0);
        if (o.pos.size() < 2) throw Error("err.usage", {cmd});
        std::vector<Vec3> pts;
        for (auto& p : o.pos) pts.push_back(parsePoint(p));
        const double t = o.kv.count("T") ? parseNumber(o.kv.at("T")) : 0.3, h = o.kv.count("H") ? parseNumber(o.kv.at("H")) : 3.0;
        if (t <= 0 || t > 5 || h <= 0) throw Error("err.bad_value", {std::format("T={:g} H={:g}", t, h)});
        const std::string j = o.kv.count("J") ? utf8::toUpper(o.kv.at("J")) : "C";
        if (j != "C" && j != "L" && j != "R") throw Error("err.bad_value", {"J=" + j});
        const bool closed = o.flags.count("CLOSE") > 0;
        // yangi guruh raqami
        int maxN = 0;
        doc_.forEach([&](const IEntity& e) {
            const std::string id = e.xget("aw.wall");
            if (id.size() > 1 && id[0] == 'W') maxN = std::max(maxN, std::atoi(id.c_str() + 1));
        });
        auto ws = bim::makeWallChain(pts, t, h, j[0], closed, std::format("W{}-", maxN + 1));
        if (ws.empty()) throw Error("err.usage", {cmd});
        // T-tutashuvlar: yangi devor uchlari mavjud devorlar yuzigacha, mavjudlari — yangisigacha
        auto existing = bim::wallsFromDoc(doc_);
        const auto changed = bim::joinWalls(ws, existing);
        auto comp = std::make_unique<CompositeCommand>("WALL");
        for (std::size_t i : changed) {
            std::vector<EntityId> old;
            doc_.forEach([&](const IEntity& e) { if (e.xget("aw.wall") == existing[i].id) old.push_back(e.id()); });
            if (!old.empty()) comp->add(std::make_unique<DeleteCommand>(old));
            for (auto& e : bim::wallEntities(existing[i])) comp->add(std::make_unique<AddEntityCommand>(std::move(e)));
        }
        std::vector<AddEntityCommand*> adds;
        for (const auto& w : ws)
            for (auto& e : bim::wallEntities(w)) {
                e->setColor(color_);
                auto c = std::make_unique<AddEntityCommand>(std::move(e));
                adds.push_back(c.get());
                comp->add(std::move(c));
            }
        stack_.execute(std::move(comp));
        last_.clear();
        for (auto* c : adds) last_.push_back(c->entityId());
        double len = 0;
        for (auto& w : ws) len += w.length();
        say("msg.walls_created", {std::to_string(ws.size()), std::format("{:.2f}", len), std::format("{:g}", t)});
        return true;
    }
    if (cmd == "DOOR" || cmd == "WINDOW") {
        const ArchOpts o = opts(a, 0);
        if (o.pos.empty()) throw Error("err.usage", {cmd});
        const Vec3 p = parsePoint(o.pos[0]);
        auto ws = bim::wallsFromDoc(doc_);
        double along = 0;
        const int wi = bim::nearestWall(ws, p, 1.0, &along);
        if (wi < 0) throw Error("err.no_wall");
        bim::ArchWall w = ws[static_cast<std::size_t>(wi)];
        const char kind = cmd == "DOOR" ? 'D' : 'W';
        if (o.flags.count("DEL")) {
            int best = -1;
            double bd = 1e300;
            for (std::size_t i = 0; i < w.ops.size(); ++i)
                if (w.ops[i].kind == kind && std::abs(w.ops[i].pos - along) < bd) { bd = std::abs(w.ops[i].pos - along); best = static_cast<int>(i); }
            if (best < 0 || bd > w.ops[static_cast<std::size_t>(best)].w) throw Error("err.no_opening");
            w.ops.erase(w.ops.begin() + best);
            replaceWall(w, cmd);
            say("msg.opening_removed", {w.id});
            return true;
        }
        bim::ArchOpening op;
        op.kind = kind;
        op.pos = along;
        op.w = o.kv.count("W") ? parseNumber(o.kv.at("W")) : (kind == 'D' ? 0.9 : 1.5);
        op.h = o.kv.count("H") ? parseNumber(o.kv.at("H")) : (kind == 'D' ? 2.1 : 1.5);
        op.sill = kind == 'D' ? 0.0 : (o.kv.count("SILL") ? parseNumber(o.kv.at("SILL")) : 0.9);
        op.right = o.flags.count("RIGHT") > 0;
        op.out = o.flags.count("OUT") > 0;
        std::string why;
        if (!bim::openingFits(w, op, &why)) throw Error("err.opening_fit", {w.id, why});
        w.ops.push_back(op);
        replaceWall(w, cmd);
        say(kind == 'D' ? "msg.door_added" : "msg.window_added", {w.id, std::format("{:g}", op.w), std::format("{:g}", op.h)});
        return true;
    }
    if (cmd == "ROOM") {
        const ArchOpts o = opts(a, 0);
        if (o.pos.empty()) throw Error("err.usage", {cmd});
        const Vec3 p = parsePoint(o.pos[0]);
        const auto ws = bim::wallsFromDoc(doc_);
        const auto r = bim::findRoom(ws, p);
        if (!r.ok) throw Error("err.no_room");
        const std::string type = o.kv.count("TYPE") ? utf8::toUpper(o.kv.at("TYPE")) : "ROOM";
        int n = 0;
        doc_.forEach([&](const IEntity& e) { if (!e.xget("aw.room").empty()) ++n; });
        const std::string name = o.kv.count("NAME") ? o.kv.at("NAME") : loc_.tr("arch.room") + " " + std::to_string(n + 1);
        const bool rent = o.kv.count("RENT") ? parseNumber(o.kv.at("RENT")) != 0 : bim::roomRentableByType(type);
        const double th = o.kv.count("TH") ? parseNumber(o.kv.at("TH")) : 0.25;
        std::vector<std::unique_ptr<IEntity>> es;
        auto poly = std::make_unique<PolylineEntity>(r.net, true);
        poly->xset("aw.room", name);
        poly->xset("aw.rtype", type);
        poly->xset("aw.rent", rent ? "1" : "0");
        poly->xset("aw.net", std::format("{:.4f}", r.netArea));
        poly->xset("aw.axis", std::format("{:.4f}", r.axisArea));
        poly->xset("aw.perim", std::format("{:.4f}", r.perimeter));
        poly->setLayer("A-ROOM");
        es.push_back(std::move(poly));
        auto t1 = std::make_unique<TextEntity>(r.center + Vec3{0, th * 0.7, 0}, th, name, 0.0, TextHAlign::Center, TextVAlign::Bottom);
        auto t2 = std::make_unique<TextEntity>(r.center - Vec3{0, th * 0.7, 0}, th * 0.9, std::format("{:.2f} m²", r.netArea), 0.0, TextHAlign::Center, TextVAlign::Top);
        for (auto* t : {t1.get(), t2.get()}) { t->setLayer("A-ROOM-TEXT"); t->xset("aw.roomlabel", name); }
        es.push_back(std::move(t1));
        es.push_back(std::move(t2));
        auto comp = std::make_unique<CompositeCommand>("ROOM");
        for (auto& e : es) { e->setColor(color_); comp->add(std::make_unique<AddEntityCommand>(std::move(e))); }
        stack_.execute(std::move(comp));
        say("msg.room_created", {name, std::format("{:.2f}", r.netArea), std::format("{:.2f}", r.axisArea), std::format("{:.2f}", r.perimeter)});
        return true;
    }
    if (cmd == "ROOMS") {
        const ArchOpts o = opts(a, 0);
        struct Row { std::string name, type; double net, axis, perim; bool rent; };
        std::vector<Row> rows;
        doc_.forEach([&](const IEntity& e) {
            if (e.xget("aw.room").empty()) return;
            rows.push_back({e.xget("aw.room"), e.xget("aw.rtype"), std::atof(e.xget("aw.net").c_str()), std::atof(e.xget("aw.axis").c_str()),
                            std::atof(e.xget("aw.perim").c_str()), e.xget("aw.rent") == "1"});
        });
        const auto ws = bim::wallsFromDoc(doc_);
        const auto outer = bim::buildingOutline(ws);
        double net = 0, rent = 0;
        for (auto& r : rows) {
            net += r.net;
            if (r.rent) rent += r.net;
            out_ << std::format("  {:<24} {:<10} {:>9.2f} {:>9.2f} {}\n", r.name, r.type, r.net, r.axis, r.rent ? "*" : "");
        }
        const double gross = outer.ok ? outer.netArea : 0.0;
        say("msg.rooms_total", {std::to_string(rows.size()), std::format("{:.2f}", net), std::format("{:.2f}", rent), std::format("{:.2f}", gross)});
        if (o.kv.count("XLSX")) {
            xlsx::Workbook wb;
            auto& sh = wb.add(loc_.tr("arch.xlsx.sheet"));
            sh.add({xlsx::Cell::s(loc_.tr("arch.xlsx.name"), 1), xlsx::Cell::s(loc_.tr("arch.xlsx.type"), 1), xlsx::Cell::s(loc_.tr("arch.xlsx.net"), 1),
                    xlsx::Cell::s(loc_.tr("arch.xlsx.axis"), 1), xlsx::Cell::s(loc_.tr("arch.xlsx.perim"), 1), xlsx::Cell::s(loc_.tr("arch.xlsx.rent"), 1)});
            for (auto& r : rows)
                sh.add({xlsx::Cell::s(r.name), xlsx::Cell::s(r.type), xlsx::Cell::n(r.net), xlsx::Cell::n(r.axis), xlsx::Cell::n(r.perim),
                        xlsx::Cell::s(r.rent ? loc_.tr("arch.yes") : loc_.tr("arch.no"))});
            const int last = static_cast<int>(rows.size()) + 1;
            sh.add({xlsx::Cell::s(loc_.tr("arch.xlsx.total"), 4), xlsx::Cell::s(""),
                    xlsx::Cell::f(std::format("SUM(C2:C{})", last), net, 4), xlsx::Cell::f(std::format("SUM(D2:D{})", last), 0, 4), xlsx::Cell::s(""),
                    xlsx::Cell::f(std::format("SUMIF(F2:F{},\"{}\",C2:C{})", last, loc_.tr("arch.yes"), last), rent, 4)});
            sh.add({xlsx::Cell::s(loc_.tr("arch.xlsx.gross"), 4), xlsx::Cell::s(""), xlsx::Cell::n(gross, 4)});
            sh.widths = {28, 12, 12, 14, 12, 10};
            xlsx::save(wb, o.kv.at("XLSX"));
            say("msg.saved", {o.kv.at("XLSX")});
        }
        return true;
    }
    if (cmd == "ARSECTION") {
        const ArchOpts o = opts(a, 0);
        if (o.pos.size() < 2 || !o.kv.count("AT")) throw Error("err.usage", {cmd});
        const Vec3 p1 = parsePoint(o.pos[0]), p2 = parsePoint(o.pos[1]), at = parsePoint(o.kv.at("AT"));
        bim::SectionOptions so;
        if (o.kv.count("FLOORS")) so.floors = std::max(1, static_cast<int>(parseNumber(o.kv.at("FLOORS"))));
        if (o.kv.count("SLAB")) so.slab = parseNumber(o.kv.at("SLAB"));
        if (o.kv.count("DEPTH")) so.depth = parseNumber(o.kv.at("DEPTH"));
        if (o.kv.count("TH")) so.textH = parseNumber(o.kv.at("TH"));
        int nSect = 0;
        doc_.forEach([&](const IEntity& e) { if (e.xget("aw.sect") == "mark") ++nSect; });
        const std::string mark = o.kv.count("TITLE") ? o.kv.at("TITLE") : std::to_string(nSect / 2 + 1);
        so.title = loc_.tr("arch.section") + " " + mark + "-" + mark;
        const auto ws = bim::wallsFromDoc(doc_);
        int cuts = 0;
        auto es = bim::sectionEntities(ws, p1, p2, at, so, &cuts);
        if (es.empty()) throw Error("err.no_wall");
        // rejadagi kesim belgisi: uchlarda qalin chiziqlar va raqam
        const Vec3 u = (p2 - p1) / std::max(1e-9, p1.distanceTo(p2)), nv{-u.y, u.x, 0};
        for (const Vec3& e : {p1, p2}) {
            auto l = std::make_unique<LineEntity>(e, e + nv * 0.6);
            l->setLayer("A-SECT-MARK");
            l->xset("aw.sect", "mark");
            es.push_back(std::move(l));
            auto tx = std::make_unique<TextEntity>(e + nv * 0.9, so.textH * 1.6, mark, 0.0, TextHAlign::Center, TextVAlign::Middle);
            tx->setLayer("A-SECT-MARK");
            es.push_back(std::move(tx));
        }
        auto comp = std::make_unique<CompositeCommand>("ARSECTION");
        for (auto& e : es) comp->add(std::make_unique<AddEntityCommand>(std::move(e)));
        stack_.execute(std::move(comp));
        say(cuts > 0 ? "msg.section_created" : "msg.elevation_created", {so.title, std::to_string(cuts)});
        return true;
    }
    if (cmd == "AREDIT") {   // ARMODEL natijasini tuzatish: tanlangan element turi, belgilari, beton sinfi; DEL; UNDO
        const ArchOpts o = opts(a, 0);
        if (!lastAr_ || lastAr_->prisms.empty()) throw Error("err.no_armodel");
        if (o.flags.count("UNDO") || (!a.empty() && loc_.resolveCommand(a[0]) == "UNDO")) {
            if (arHist_.empty()) throw Error("err.nothing_to_undo");
            lastAr_ = arHist_.back();
            arHist_.pop_back();
            say("msg.aredit_undone");
            return true;
        }
        if (o.pos.empty()) throw Error("err.usage", {cmd});
        const int i = static_cast<int>(parseNumber(o.pos[0]));
        if (i < 0 || i >= static_cast<int>(lastAr_->prisms.size())) throw Error("err.bad_value", {o.pos[0]});
        bim::BuildingModel nm = *lastAr_;
        auto& pr = nm.prisms[static_cast<std::size_t>(i)];
        if (o.flags.count("DEL")) nm.prisms.erase(nm.prisms.begin() + i);
        else {
            if (o.kv.count("KIND")) {
                bim::ElemKind k;
                if (!bim::kindFromKey(utf8::toUpper(o.kv.at("KIND")), k)) throw Error("err.bad_value", {"KIND=" + o.kv.at("KIND")});
                pr.kind = k;
            }
            if (o.kv.count("Z0")) pr.z0 = parseNumber(o.kv.at("Z0"));
            if (o.kv.count("Z1")) pr.z1 = parseNumber(o.kv.at("Z1"));
            if (o.kv.count("H")) pr.z1 = pr.z0 + parseNumber(o.kv.at("H"));
            if (o.kv.count("CONC")) pr.concrete = utf8::toUpper(o.kv.at("CONC"));
            if (pr.z1 <= pr.z0) throw Error("err.bad_value", {std::format("Z0={:g} Z1={:g}", pr.z0, pr.z1)});
        }
        // jami hajm va beton sinflari bo'yicha qayta
        nm.totalVolume = 0;
        nm.byConcrete.clear();
        for (const auto& q : nm.prisms) {
            if (q.kind == bim::ElemKind::Infill) continue;
            nm.totalVolume += q.volume();
            nm.byConcrete[q.concrete] += q.volume();
        }
        arHist_.push_back(*lastAr_);
        if (arHist_.size() > 50) arHist_.erase(arHist_.begin());
        lastAr_ = std::move(nm);
        say("msg.aredit", {std::to_string(i), std::format("{:.2f}", lastAr_->totalVolume)});
        return true;
    }
    if (cmd == "AR3D") {
        const ArchOpts o = opts(a, 0);
        const auto ws = bim::wallsFromDoc(doc_);
        if (ws.empty()) throw Error("err.no_wall");
        const int floors = o.kv.count("FLOORS") ? std::max(1, static_cast<int>(parseNumber(o.kv.at("FLOORS")))) : 1;
        const double slab = o.kv.count("SLAB") ? parseNumber(o.kv.at("SLAB")) : 0.2;
        std::vector<EntityId> old;
        doc_.forEach([&](const IEntity& e) { if (e.xget("aw.3d") == "1") old.push_back(e.id()); });
        auto comp = std::make_unique<CompositeCommand>("AR3D");
        if (!old.empty()) comp->add(std::make_unique<DeleteCommand>(old));
        std::size_t faces = 0;
        for (auto& e : bim::archMeshes(ws, floors, slab)) {
            e->xset("aw.3d", "1");
            if (auto* m = dynamic_cast<MeshEntity*>(e.get())) faces += m->faces().size();
            comp->add(std::make_unique<AddEntityCommand>(std::move(e)));
        }
        stack_.execute(std::move(comp));
        say("msg.ar3d_created", {std::to_string(ws.size()), std::to_string(floors), std::to_string(faces)});
        return true;
    }
    if (cmd == "ARSHEET") {
        need(a, 1, cmd);
        const std::string name = a[0];
        const std::string size = a.size() > 1 ? utf8::toUpper(a[1]) : "A1";
        BBox plan, sect;
        doc_.forEach([&](const IEntity& e) {
            const std::string l = utf8::toUpper(e.layer());
            if (e.xget("aw.3d") == "1") return;
            if (e.xget("aw.sect") == "view") sect.expand(e.bbox());
            else if (l.rfind("A-", 0) == 0) plan.expand(e.bbox());
        });
        if (!plan.valid()) throw Error("err.no_wall");
        auto pt = [](const Vec3& v) { return std::format("{:.3f},{:.3f}", v.x, v.y); };
        const Vec3 pad{1, 1, 0};
        run(std::format("SHEET NEW \"{}\" {}", name, size));
        run(std::format("SHEET VIEW \"{}\" {} {} \"TITLE={}\"", name, pt(plan.min - pad), pt(plan.max + pad), loc_.tr("arch.plan")));
        if (sect.valid()) run(std::format("SHEET VIEW \"{}\" {} {} \"TITLE={}\"", name, pt(sect.min - pad), pt(sect.max + pad), loc_.tr("arch.sections")));
        return true;
    }
    return false;
}

}  // namespace asiacad::cli
