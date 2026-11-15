// AsiaCAD CLI — seysmik va geotexnik hisoblar (formulalar: bim/seismic.h, bim/geotech.h).
//   SEISMIC fayl.seis [I=6..12 | PGA=g] [DIR=X|Y] [SWEEP] [XLSX=f]   bino: modal, spektral, tekshiruvlar, 6–12 ball
//   PILECAP D=m L=m R=kPa F=kPa [GRS=1.5] [GRU=2] [LOAD=kN]          burg'ilangan qoziq ko'tarish qobiliyati
//   BEARING B=m L=m DF=m C=kPa PHI=° [G=kN/m³] [FS=3]                 sayoz poydevor zamini (Vesić)
//   SETTLE B=m L=m DF=m P=kPa LAYERS=h:E:γ,…                          cho'kish (qatlamlab yig'ish)
//   PIT C=m/s T=s L0=m [TOL=m]                                         qoziq butunligi (uzunlik, sifat)
#include "interpreter.h"

#include <asiacad/bim/geotech.h>
#include <asiacad/civil/axes.h>
#include <asiacad/civil/genplan.h>
#include <asiacad/bim/scene_export.h>
#include <asiacad/bim/building.h>
#include <asiacad/bim/facade.h>
#include <asiacad/bim/fem.h>
#include <filesystem>
#include <asiacad/core/commands.h>
#include <asiacad/bim/seismic.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/xlsx.h>

#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>

namespace asiacad::cli {

namespace {
std::map<std::string, std::string> kv(const std::vector<std::string>& a, std::size_t from, std::vector<std::string>* flags = nullptr) {
    std::map<std::string, std::string> m;
    for (std::size_t i = from; i < a.size(); ++i) {
        const auto eq = a[i].find('=');
        if (eq == std::string::npos) { if (flags) flags->push_back(utf8::toUpper(a[i])); continue; }
        m[utf8::toUpper(a[i].substr(0, eq))] = a[i].substr(eq + 1);
    }
    return m;
}
}  // namespace

bool Interpreter::dispatchSeismic(const std::string& cmd, const Args& a) {
    using namespace bim;
    auto usage = [&]() { return Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))}); };
    auto num = [&](const std::map<std::string, std::string>& m, const char* k, double def, bool required = false) {
        auto it = m.find(k);
        if (it == m.end()) { if (required) throw usage(); return def; }
        return parseNumber(it->second);
    };
    if (cmd == "GRIDAXES") {   // aks to'ri (to'g'ri burchakli / burilgan / radial), markalar va o'lcham zanjiri
        if (a.empty()) throw usage();
        civil::AxesSpec sp;
        try {
            sp = civil::parseAxesArgs(a);
        } catch (const std::invalid_argument& e) {
            throw Error("axes.err.arg", {e.what()});
        }
        auto es = civil::generateAxes(sp);
        int lines = 0;
        for (const auto& e : es) lines += e->layer() == "AKS";
        addLayered(cmd, std::move(es));
        double W = 0, D = 0;
        for (double d : sp.radial ? sp.da : sp.dx) W += d;
        for (double d : sp.radial ? sp.dr : sp.dy) D += d;
        say(sp.radial ? "axes.msg.radial" : "axes.msg.done", {std::to_string(lines), std::format("{:g}", W), std::format("{:g}", D)});
        return true;
    }
    if (cmd == "EXPORT3D") {   // render uchun sahna: OBJ/MTL yoki glTF + markerlar (daraxt/odam/avto) + quyosh/kamera
        if (a.empty()) throw usage();
        const std::string path = a[0];
        std::vector<std::string> flags;
        const auto m = kv(a, 1, &flags);
        auto has = [&](const char* f) { return std::find(flags.begin(), flags.end(), f) != flags.end(); };
        for (const auto& f : flags)
            if (f != "BUILDING" && f != "GENPLAN" && f != "NOMARKERS" && f != "SHELL" && f != "IFCCOLORS") throw usage();
        for (const auto& [k, v] : m)
            if (k != "IFC" && k != "DATE" && k != "TIME" && k != "MAX" && k != "RENDERER") throw usage();
        bim::Scene sc;
        int parts = 0;
        if (auto it = m.find("IFC"); it != m.end()) {
            bim::IfcModel im;
            std::string err;
            if (!bim::readIfc(it->second, im, err, false)) throw Error(err, {it->second});
            const std::size_t before = sc.triangleCount();
            bim::addIfc(sc, im, bim::IfcExportOptions{has("SHELL"), has("IFCCOLORS")});
            if (has("SHELL")) {
                std::size_t all = 0;
                for (const auto& e : im.elements)
                    for (const auto& p : e.parts) all += p.tris.size();
                say("export.msg.shell", {std::to_string(sc.triangleCount() - before), std::to_string(all)});
            }
            ++parts;
        }
        const bool wantBuilding = has("BUILDING") || (!m.count("IFC") && !has("GENPLAN"));
        if (wantBuilding) {
            bim::BuildingParams bp;
            auto lm = bim::autoLayerMap(doc_);
            if (const std::string pj = doc_.dictGet("bim:params"); !pj.empty()) bim::paramsFromJson(pj, bp, lm);
            const auto plan = bim::readPlan(doc_, lm);
            if (!plan.empty()) { bim::addBuilding(sc, bim::buildModel(plan, bp)); ++parts; }
        }
        // chizmadagi 3D to'rlar (fasad va boshqalar): qatlam → material
        doc_.forEach([&](const IEntity& e) {
            const auto* me = dynamic_cast<const MeshEntity*>(&e);
            if (!me) return;
            const std::string L = utf8::toUpper(e.layer());
            const std::string mat = L.find("SHISHA") != std::string::npos || L.find("GLASS") != std::string::npos ? "AC_Shisha"
                                    : L.find("PROFIL") != std::string::npos                                          ? "AC_Alyuminiy"
                                    : L.find("PANEL") != std::string::npos                                           ? "AC_Panel"
                                                                                                                     : "AC_Boshqa";
            const auto& v = me->vertices();
            for (const auto& f : me->faces()) {
                auto idx = [&](int i) { return v[static_cast<std::size_t>(std::abs(i) - (i < 0 ? 1 : 0))]; };
                if (f.a < 0 || f.b < 0 || f.c < 0 || static_cast<std::size_t>(std::max({f.a, f.b, f.c})) >= v.size()) continue;
                sc.add(mat, {idx(f.a), idx(f.b), idx(f.c)});
                if (f.d >= 0 && static_cast<std::size_t>(f.d) < v.size()) sc.add(mat, {idx(f.a), idx(f.c), idx(f.d)});
            }
            ++parts;
        });
        // genreja: yer qoplamalari (yupqa qatlam) + markerlar
        {
            civil::GenplanSpec gs;
            const auto zones = civil::gpExtractZones(doc_, gs);
            if (!zones.empty() && (has("GENPLAN") || parts == 0 || true)) {
                for (const auto& z : zones) {
                    std::string mat;
                    double t = 0.05;
                    switch (z.zone) {
                        case civil::GpZone::Road: case civil::GpZone::Parking: mat = "AC_Asfalt"; break;
                        case civil::GpZone::Sidewalk: case civil::GpZone::Path: case civil::GpZone::Playground: case civil::GpZone::Ramp: mat = "AC_Plitka"; t = 0.10; break;
                        case civil::GpZone::Green: mat = "AC_Chim"; t = 0.12; break;
                        default: continue;
                    }
                    std::vector<Vec3> o;
                    for (const auto& q : z.outer) o.push_back({q.x, q.y, 0});
                    std::vector<std::vector<Vec3>> hs;
                    for (const auto& h : z.holes) { hs.emplace_back(); for (const auto& q : h) hs.back().push_back({q.x, q.y, 0}); }
                    bim::addPrism(sc, mat, o, hs, -t, 0);
                }
                ++parts;
                if (!has("NOMARKERS")) {
                    auto inZone = [](const civil::GpPoly& z, double x, double y) {
                        auto in = [](const std::vector<civil::GpPoint>& r, double px, double py) {
                            bool c = false;
                            for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
                                if ((r[i].y > py) != (r[j].y > py) && px < (r[j].x - r[i].x) * (py - r[i].y) / (r[j].y - r[i].y) + r[i].x) c = !c;
                            return c;
                        };
                        if (!in(z.outer, x, y)) return false;
                        for (const auto& h : z.holes) if (in(h, x, y)) return false;
                        return true;
                    };
                    unsigned seed = 7;
                    auto rnd = [&] { seed = seed * 1103515245u + 12345u; return (seed >> 8) / double(1u << 24); };
                    for (const auto& z : zones) {
                        double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
                        for (const auto& q : z.outer) { x0 = std::min(x0, q.x); y0 = std::min(y0, q.y); x1 = std::max(x1, q.x); y1 = std::max(y1, q.y); }
                        const bool green = z.zone == civil::GpZone::Green, walk = z.zone == civil::GpZone::Sidewalk || z.zone == civil::GpZone::Path;
                        if (!green && !walk) continue;
                        const double step = green ? 8.0 : 14.0;
                        for (double x = x0 + step / 2; x < x1; x += step)
                            for (double y = y0 + step / 2; y < y1; y += step) {
                                const double px = x + (rnd() - 0.5) * step * 0.5, py = y + (rnd() - 0.5) * step * 0.5;
                                // chetdan 1,5 m ichkarida
                                bool ok = inZone(z, px, py);
                                for (double dx : {-1.5, 1.5}) ok = ok && inZone(z, px + dx, py) && inZone(z, px, py + dx);
                                if (!ok) continue;
                                if (green) sc.markers.push_back({rnd() < 0.7 ? "TREE" : "SHRUB", {px, py, 0}, 0, rnd() * 360});
                                else if (rnd() < 0.5) sc.markers.push_back({"PERSON", {px, py, 0}, 1.75, rnd() * 360});
                            }
                    }
                    for (auto& mk : sc.markers) if (mk.height == 0) mk.height = mk.kind == "TREE" ? 6 + rnd() * 6 : 1.2 + rnd();
                    // avtoturargoh joylari (GP_AVTOTURARGOH) — har ikkinchisida avto
                    int k = 0;
                    for (EntityId id : doc_.ids())
                        if (const auto* pl = dynamic_cast<const PolylineEntity*>(doc_.find(id)); pl && pl->layer() == "GP_AVTOTURARGOH" && pl->vertices().size() == 4)
                            if (++k % 2) {
                                const auto& v = pl->vertices();
                                const Vec3 c{(v[0].x + v[2].x) / 2, (v[0].y + v[2].y) / 2, 0};
                                const double ang = std::atan2(v[0].y - v[1].y, v[0].x - v[1].x) * 180 / 3.14159265358979;
                                sc.markers.push_back({"CAR", c, 1.5, ang});
                            }
                }
            }
        }
        if (sc.triangleCount() == 0) throw Error("export.err.empty", {});
        bim::centerScene(sc);
        const std::string ext = utf8::toUpper(std::filesystem::path(path).extension().string());
        if (ext == ".OBJ") bim::writeObj(sc, path);
        else bim::writeGltf(sc, path);
        auto sib = [&](const std::string& suffix) {
            auto p = std::filesystem::path(path);
            return (p.parent_path() / (p.stem().string() + suffix)).string();
        };
        if (!sc.markers.empty()) bim::writeMarkersCsv(sc, sib("_markerlar.csv"));
        // quyosh (Toshkent) va kamera
        int Y = 2026, M = 6, D = 21;
        double hour = 16.0;
        if (auto it = m.find("DATE"); it != m.end() && std::sscanf(it->second.c_str(), "%d-%d-%d", &Y, &M, &D) != 3) throw usage();
        if (auto it = m.find("TIME"); it != m.end()) {
            int h = 0, mi = 0;
            if (std::sscanf(it->second.c_str(), "%d:%d", &h, &mi) < 1) throw usage();
            hour = h + mi / 60.0;
        }
        const auto sun = bim::sunPosition(41.31, 69.28, Y, M, D, hour, 5.0);
        {
            std::ofstream j(sib("_sahna.json"), std::ios::binary);
            j << std::format("{{\"origin\":[{:.3f},{:.3f},{:.3f}],\"sun\":{{\"azimuth\":{:.2f},\"elevation\":{:.2f},\"date\":\"{:04d}-{:02d}-{:02d}\",\"time\":\"{:.2f}\","
                             "\"place\":\"Toshkent 41.31N 69.28E UTC+5\"}},\"markers\":{},\"triangles\":{},\"up\":\"{}\"}}",
                             sc.origin.x, sc.origin.y, sc.origin.z, sun.azimuthDeg, sun.elevationDeg, Y, M, D, hour, sc.markers.size(), sc.triangleCount(),
                             ext == ".OBJ" ? "Z" : "Y");
        }
        // 3ds Max skripti (Corona / V-Ray): OBJ + .ms + aktivlar jadvali namunasi
        if (auto it = m.find("MAX"); it != m.end()) {
            bim::MaxScriptOptions mo;
            if (auto r = m.find("RENDERER"); r != m.end() && !bim::maxRendererFromKey(r->second, mo.renderer)) throw usage();
            mo.sunAzimuthDeg = sun.azimuthDeg;
            mo.sunElevationDeg = sun.elevationDeg;
            const auto ms = std::filesystem::path(it->second);
            const auto obj = ms.parent_path() / (ms.stem().string() + ".obj");
            bim::writeObj(sc, obj.string());
            mo.objFile = obj.filename().string();
            const auto ac = ms.parent_path() / (ms.stem().string() + "_aktivlar.csv");
            mo.assetsCsv = ac.filename().string();
            if (!std::filesystem::exists(ac)) {
                std::ofstream a(ac, std::ios::binary);
                a << "kind;fayl;balandlik\n"
                     "# TREE;Chinor_12m.max;12   — Chaos Cosmos / Maxtree / Laubwerk proksi saqlangan .max fayl (skript yonida)\n"
                     "# SHRUB;Buta_1m.max;1.2\n# PERSON;Odam_yurgan.max;1.75\n# CAR;Avto_sedan.max;1.5\n";
            }
            bim::writeMaxScript(sc, ms.string(), mo);
            say("export.msg.max", {ms.string(), obj.filename().string(), ac.filename().string()});
        }
        std::map<std::string, int> cnt;
        for (const auto& mk : sc.markers) ++cnt[mk.kind];
        say("export.msg.done", {path, std::to_string(sc.triangleCount()), std::to_string(sc.meshes.size()), std::to_string(cnt["TREE"] + cnt["SHRUB"]),
                                std::to_string(cnt["PERSON"]), std::to_string(cnt["CAR"]), std::format("{:.0f}", sun.azimuthDeg), std::format("{:.0f}", sun.elevationDeg)});
        return true;
    }
    if (cmd == "FACADE") {   // fasad tizimi: IFC qavatlaridan yoki chizmadagi konturdan; 3D panellar, yoyilma, metraj
        std::vector<std::string> flags;
        const auto m = kv(a, 0, &flags);
        bool elev = false;
        for (const auto& f : flags) {
            if (f == "ELEV") elev = true;
            else throw usage();
        }
        bim::FacadeSpec sp;
        if (auto it = m.find("TYPE"); it != m.end() && !bim::facadeTypeFromKey(it->second, sp.type)) throw usage();
        if (sp.type == bim::FacadeType::Curtain) sp.spandrel = 0.6;
        sp.module = num(m, "MODULE", sp.module);
        sp.spandrel = num(m, "SPANDREL", sp.spandrel);
        sp.offset = num(m, "OFFSET", sp.offset);
        if (auto it = m.find("WIN"); it != m.end()) {
            const auto x = utf8::toUpper(it->second).find('X');
            if (x == std::string::npos) throw usage();
            sp.winW = parseNumber(it->second.substr(0, x));
            sp.winH = parseNumber(it->second.substr(x + 1));
        }
        sp.sill = num(m, "SILL", sp.sill);
        std::vector<bim::FacadeFloor> floors;
        if (auto it = m.find("IFC"); it != m.end()) {
            bim::IfcModel im;
            std::string err;
            if (!bim::readIfc(it->second, im, err, false)) throw Error(err, {it->second});
            floors = bim::facadeFloorsFromIfc(im);
        } else {
            const int n = static_cast<int>(num(m, "FLOORS", 0, true));
            floors = bim::facadeFloorsFromDoc(doc_, m.count("LAYER") ? m.at("LAYER") : std::string(), n, num(m, "H", 3.3), num(m, "Z0", 0));
        }
        if (floors.empty()) throw Error("facade.err.nofloors", {});
        const auto r = bim::buildFacade(floors, sp);
        auto es = bim::facadeEntities(r);
        if (elev) {
            BBox bb = doc_.extents(false);
            double x0 = 0, y0 = 0;
            for (const auto& f : floors)
                for (const auto& p : f.outline) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); }
            const Vec3 o{bb.valid() ? bb.min.x : x0, (bb.valid() ? bb.min.y : y0) - r.height - 20, 0};
            for (auto& e : bim::facadeElevation(r, floors, o, 0.6)) es.push_back(std::move(e));
        }
        // oldingi fasad almashtiriladi
        std::vector<EntityId> old;
        for (EntityId id : doc_.ids())
            if (const IEntity* e = doc_.find(id); e && e->layer().rfind("FASAD_", 0) == 0) old.push_back(id);
        auto comp = std::make_unique<CompositeCommand>(cmd);
        if (!old.empty()) comp->add(std::make_unique<DeleteCommand>(old));
        std::set<std::string> made;
        for (const auto& e : es)
            if (!doc_.layer(e->layer()) && made.insert(e->layer()).second) comp->add(std::make_unique<SetLayerCommand>(Layer{e->layer()}));
        std::vector<AddEntityCommand*> adds;
        for (auto& e : es) {
            auto c = std::make_unique<AddEntityCommand>(std::move(e), cmd);
            adds.push_back(c.get());
            comp->add(std::move(c));
        }
        stack_.execute(std::move(comp));
        last_.clear();
        for (auto* c : adds) last_.push_back(c->entityId());
        say("facade.msg.done", {std::to_string(r.floors), std::format("{:.1f}", r.perimeter), std::format("{:.1f}", r.height), std::format("{:.1f}", r.glassArea),
                                std::to_string(r.glassCount), std::format("{:.1f}", r.panelArea), std::to_string(r.panelCount), std::format("{:.1f}", r.mullionLen)});
        if (auto it = m.find("XLSX"); it != m.end()) {
            xlsx::Workbook wb;
            auto& sh = wb.add(loc_.tr("facade.xl.sheet"));
            sh.add({xlsx::Cell::s(loc_.tr("gp.xl.item"), 1), xlsx::Cell::s(loc_.tr("gp.xl.unit"), 1), xlsx::Cell::s(loc_.tr("gp.xl.qty"), 1)});
            sh.add({xlsx::Cell::s(loc_.tr("facade.xl.glass")), xlsx::Cell::s("m²"), xlsx::Cell::n(r.glassArea)});
            sh.add({xlsx::Cell::s(loc_.tr("facade.xl.glass_n")), xlsx::Cell::s(loc_.tr("gp.xl.pcs")), xlsx::Cell::n(r.glassCount, 5)});
            sh.add({xlsx::Cell::s(loc_.tr("facade.xl.panel")), xlsx::Cell::s("m²"), xlsx::Cell::n(r.panelArea)});
            sh.add({xlsx::Cell::s(loc_.tr("facade.xl.panel_n")), xlsx::Cell::s(loc_.tr("gp.xl.pcs")), xlsx::Cell::n(r.panelCount, 5)});
            sh.add({xlsx::Cell::s(loc_.tr("facade.xl.profile")), xlsx::Cell::s("m"), xlsx::Cell::n(r.mullionLen)});
            sh.widths = {40, 8, 14};
            xlsx::save(wb, it->second);
            say("dev.msg.saved", {it->second});
        }
        return true;
    }
    if (cmd == "GENPLAN") {   // vaziyat rejasidan: bordyurlar, avtoturargoh joylari, metraj
        std::vector<std::string> flags;
        const auto m = kv(a, 0, &flags);
        civil::GenplanSpec sp;
        auto has = [&](const char* f) { return std::find(flags.begin(), flags.end(), f) != flags.end(); };
        for (const auto& f : flags)
            if (f != "NOCURB" && f != "NOPARK" && f != "LAYERS") throw usage();
        sp.curbs = !has("NOCURB");
        sp.parking = !has("NOPARK");
        if (auto it = m.find("MAP"); it != m.end()) {
            try { sp.overrides = civil::gpParseMap(it->second); } catch (const std::invalid_argument& e) { throw Error("gp.err.map", {e.what()}); }
        }
        if (auto it = m.find("STALL"); it != m.end()) {
            const auto x = utf8::toUpper(it->second).find('X');
            if (x == std::string::npos) throw usage();
            sp.stallW = parseNumber(it->second.substr(0, x));
            sp.stallL = parseNumber(it->second.substr(x + 1));
        }
        if (auto it = m.find("CURB"); it != m.end()) {
            const auto c = it->second.find(';');
            sp.roadCurb.width = parseNumber(c == std::string::npos ? it->second : it->second.substr(0, c)) / 100;
            if (c != std::string::npos) sp.gardenCurb.width = parseNumber(it->second.substr(c + 1)) / 100;
        }
        auto r = civil::generateGenplan(doc_, sp);
        // qatlamlar ro'yxati (tasnif) — inson tekshiradi
        for (const auto& [layer, zc] : r.layers)
            out_ << "  " << layer << " → " << loc_.tr(std::string("gp.zone.") + civil::gpZoneKey(zc.first)) << " (" << zc.second << ")\n";
        if (has("LAYERS")) return true;
        if (r.zones.empty()) throw Error("gp.err.nozones", {});
        // oldingi GP_* natijasi almashtiriladi
        std::vector<EntityId> old;
        for (EntityId id : doc_.ids())
            if (const IEntity* e = doc_.find(id); e && e->layer().rfind("GP_", 0) == 0) old.push_back(id);
        auto comp = std::make_unique<CompositeCommand>(cmd);
        if (!old.empty()) comp->add(std::make_unique<DeleteCommand>(old));
        std::set<std::string> made;
        for (const auto& e : r.entities)
            if (!doc_.layer(e->layer()) && made.insert(e->layer()).second) comp->add(std::make_unique<SetLayerCommand>(Layer{e->layer()}));
        std::vector<AddEntityCommand*> adds;
        for (auto& e : r.entities) {
            auto c = std::make_unique<AddEntityCommand>(std::move(e), cmd);
            adds.push_back(c.get());
            comp->add(std::move(c));
        }
        stack_.execute(std::move(comp));
        last_.clear();
        for (auto* c : adds) last_.push_back(c->entityId());
        say("gp.msg.done", {std::to_string(r.zones.size()), std::format("{:.1f}", r.roadCurbLen), std::format("{:.1f}", r.gardenCurbLen),
                            std::to_string(r.stalls)});
        for (const auto& [z, ar] : r.area) out_ << "  " << loc_.tr(std::string("gp.zone.") + civil::gpZoneKey(z)) << ": " << std::format("{:.1f}", ar) << " m²\n";
        if (auto it = m.find("XLSX"); it != m.end()) {
            xlsx::Workbook wb;
            auto& sh = wb.add(loc_.tr("gp.xl.sheet"));
            sh.add({xlsx::Cell::s(loc_.tr("gp.xl.item"), 1), xlsx::Cell::s(loc_.tr("gp.xl.unit"), 1), xlsx::Cell::s(loc_.tr("gp.xl.qty"), 1)});
            sh.add({xlsx::Cell::s(loc_.tr("gp.xl.road_curb", {sp.roadCurb.name})), xlsx::Cell::s("m"), xlsx::Cell::n(r.roadCurbLen)});
            sh.add({xlsx::Cell::s(loc_.tr("gp.xl.garden_curb", {sp.gardenCurb.name})), xlsx::Cell::s("m"), xlsx::Cell::n(r.gardenCurbLen)});
            sh.add({xlsx::Cell::s(loc_.tr("gp.xl.stalls")), xlsx::Cell::s(loc_.tr("gp.xl.pcs")), xlsx::Cell::n(r.stalls, 5)});
            for (const auto& [z, ar] : r.area) sh.add({xlsx::Cell::s(loc_.tr(std::string("gp.zone.") + civil::gpZoneKey(z))), xlsx::Cell::s("m²"), xlsx::Cell::n(ar)});
            sh.widths = {44, 8, 14};
            xlsx::save(wb, it->second);
            say("dev.msg.saved", {it->second});
        }
        return true;
    }
    if (cmd == "FEM") {   // 3D sterjenli FEM: statik, xos tebranishlar, spektral (Lot-16 spektri), DRAW — chizmaga
        std::vector<std::string> flags;
        const auto m = kv(a, 0, &flags);
        bool draw = false;
        std::string file;
        for (const auto& f : flags) {
            if (f == "DRAW") draw = true;
            else if (file.empty()) file = f;
            else throw usage();
        }
        bim::fem::Model md;
        if (auto it = m.find("GEN"); it != m.end()) {   // GEN=6x3;6x2 — X va Y oraliqlari
            auto spans = [&](const std::string& s) {
                std::vector<double> v;
                const auto u = utf8::toUpper(s);
                const auto x = u.find('X');
                if (x == std::string::npos) v.push_back(parseNumber(s));
                else for (int i = 0, n = static_cast<int>(parseNumber(s.substr(x + 1))); i < n; ++i) v.push_back(parseNumber(s.substr(0, x)));
                return v;
            };
            const auto sc = it->second.find(';');
            const auto dx = spans(it->second.substr(0, sc));
            const auto dy = sc == std::string::npos ? dx : spans(it->second.substr(sc + 1));
            auto pair = [&](const char* k, double d1, double d2) {
                std::pair<double, double> p{d1, d2};
                if (auto j = m.find(k); j != m.end()) {
                    const auto x = utf8::toUpper(j->second).find('X');
                    if (x == std::string::npos) throw usage();
                    p = {parseNumber(j->second.substr(0, x)), parseNumber(j->second.substr(x + 1))};
                }
                return p;
            };
            const auto [cb, ch] = pair("COL", 0.4, 0.4);
            const auto [bb, bh] = pair("BEAM", 0.3, 0.6);
            const int fl = static_cast<int>(num(m, "FLOORS", 5));
            if (dx.empty() || dy.empty() || fl < 1) throw usage();
            md = bim::fem::frameGenerator(dx, dy, fl, num(m, "H", 3.3), cb, ch, bb, bh, num(m, "E", 3.0e7), num(m, "Q", 1.0), num(m, "G", 10.0));
        } else {
            if (file.empty()) throw usage();
            std::ifstream f(file, std::ios::binary);
            if (!f) throw Error("acad.err.open", {file});
            std::stringstream ss;
            ss << f.rdbuf();
            try { md = bim::fem::parseModel(ss.str()); } catch (const std::exception& e) { throw Error("fem.err.parse", {e.what()}); }
        }
        if (auto it = m.find("SAVE"); it != m.end()) {
            std::ofstream o(it->second, std::ios::binary);
            if (!o) throw Error("err.write_failed", {it->second});
            o << bim::fem::writeModel(md);
            say("dev.msg.saved", {it->second});
        }
        double totMass = 0;
        for (const auto& [n, v] : md.mass) totMass += v;
        say("fem.msg.model", {std::to_string(md.nodes.size()), std::to_string(md.beams.size()), std::format("{:.1f}", totMass)});
        xlsx::Workbook wb;
        xlsx::Sheet* sh = nullptr;
        if (m.count("XLSX")) {
            sh = &wb.add(loc_.tr("fem.xl.sheet"));
            sh->widths = {28, 14, 14, 14, 14};
        }
        try {
            if (!md.loads.empty()) {
                const auto r = bim::fem::solveStatic(md);
                double rz = 0;
                for (const auto& [n, R] : r.reactions) rz += R[2];
                double maxM = 0;
                for (const auto& bf : r.forces) for (int k : {4, 5, 10, 11}) maxM = std::max(maxM, std::abs(bf.f[static_cast<std::size_t>(k)]));
                say("fem.msg.static", {std::format("{:.2f}", r.maxDisp * 1000), std::format("{:.0f}", rz), std::format("{:.0f}", maxM)});
                if (sh) {
                    sh->add({xlsx::Cell::s(loc_.tr("fem.xl.static"), 1)});
                    sh->add({xlsx::Cell::s(loc_.tr("fem.xl.maxdisp")), xlsx::Cell::n(r.maxDisp * 1000, 2)});
                    sh->add({xlsx::Cell::s(loc_.tr("fem.xl.rz")), xlsx::Cell::n(rz, 1)});
                    sh->add({xlsx::Cell::s(loc_.tr("fem.xl.maxm")), xlsx::Cell::n(maxM, 1)});
                }
            }
            const int nm = static_cast<int>(num(m, "MODES", 6));
            if (totMass > 0 && nm > 0) {
                const auto modes = bim::fem::solveModes(md, nm);
                if (sh) sh->add({xlsx::Cell::s(loc_.tr("fem.xl.mode"), 1), xlsx::Cell::s("T, s", 1), xlsx::Cell::s("Mx, %", 1), xlsx::Cell::s("My, %", 1), xlsx::Cell::s("Mz, %", 1)});
                double sx = 0, sy = 0;
                for (std::size_t k = 0; k < modes.size(); ++k) {
                    const auto& q = modes[k];
                    sx += q.meffRatio[0];
                    sy += q.meffRatio[1];
                    say("fem.msg.mode", {std::to_string(k + 1), std::format("{:.3f}", q.T), std::format("{:.1f}", q.meffRatio[0] * 100), std::format("{:.1f}", q.meffRatio[1] * 100),
                                         std::format("{:.1f}", q.meffRatio[2] * 100)});
                    if (sh) sh->add({xlsx::Cell::n(static_cast<double>(k + 1), 0), xlsx::Cell::n(q.T, 3), xlsx::Cell::n(q.meffRatio[0] * 100, 1), xlsx::Cell::n(q.meffRatio[1] * 100, 1),
                                     xlsx::Cell::n(q.meffRatio[2] * 100, 1)});
                }
                // QMQ 2.01.03-19: samarali massa yig'indisi ≥ 90%
                say(std::min(sx, sy) >= 0.9 ? "fem.msg.mass_ok" : "fem.msg.mass_low", {std::format("{:.0f}", sx * 100), std::format("{:.0f}", sy * 100)});
                const int rp = static_cast<int>(num(m, "RP", 475));
                const double R = num(m, "R", 1.0);
                const auto spec = bim::seismic::siteSpectrumLot16(rp);
                const double scale = m.count("PGA") ? parseNumber(m.at("PGA")) / std::max(1e-9, spec.sa(0)) : 1.0;
                std::string dirs = m.count("DIR") ? utf8::toUpper(m.at("DIR")) : "XY";
                for (char d : dirs) {
                    const int di = d == 'Y' ? 1 : 0;
                    const auto sp = bim::fem::responseSpectrum(md, modes, [&](double T) { return spec.sa(T) * 9.81 * scale / R; }, di);
                    say("fem.msg.spectrum", {std::string(1, d), std::to_string(rp), std::format("{:.0f}", sp.baseShear), std::format("{:.1f}", sp.roof * 1000),
                                             std::format("{:.1f}", totMass > 0 ? sp.baseShear / (totMass * 9.81) * 100 : 0.0)});
                    if (sh) sh->add({xlsx::Cell::s(loc_.tr("fem.xl.shear") + " " + std::string(1, d)), xlsx::Cell::n(sp.baseShear, 0), xlsx::Cell::s(loc_.tr("fem.xl.roof")),
                                     xlsx::Cell::n(sp.roof * 1000, 1)});
                }
            }
        } catch (const std::runtime_error& e) {
            throw Error("fem.err.solve", {e.what()});
        }
        if (sh) { xlsx::save(wb, m.at("XLSX")); say("dev.msg.saved", {m.at("XLSX")}); }
        if (draw) {   // sterjenlar chizmaga: FEM_USTUN (vertikal) va FEM_RIGEL
            std::vector<EntityId> old;
            for (EntityId id : doc_.ids())
                if (const IEntity* e = doc_.find(id); e && e->layer().rfind("FEM_", 0) == 0) old.push_back(id);
            auto comp = std::make_unique<CompositeCommand>(cmd);
            if (!old.empty()) comp->add(std::make_unique<DeleteCommand>(old));
            for (const char* L : {"FEM_USTUN", "FEM_RIGEL"})
                if (!doc_.layer(L)) comp->add(std::make_unique<SetLayerCommand>(Layer{L}));
            std::vector<AddEntityCommand*> adds;
            for (const auto& b : md.beams) {
                const auto& p1 = md.nodes[static_cast<std::size_t>(md.index(b.n1))].p;
                const auto& p2 = md.nodes[static_cast<std::size_t>(md.index(b.n2))].p;
                auto e = std::make_unique<LineEntity>(p1, p2);
                e->setLayer(std::abs(p2.z - p1.z) > 0.5 * (p2 - p1).length() ? "FEM_USTUN" : "FEM_RIGEL");
                auto c = std::make_unique<AddEntityCommand>(std::move(e), cmd);
                adds.push_back(c.get());
                comp->add(std::move(c));
            }
            stack_.execute(std::move(comp));
            last_.clear();
            for (auto* c : adds) last_.push_back(c->entityId());
        }
        return true;
    }
    if (cmd == "SEISMIC") {
        if (a.empty()) throw usage();
        std::ifstream f(a[0], std::ios::binary);
        if (!f) throw Error("acad.err.open", {a[0]});
        std::stringstream ss;
        ss << f.rdbuf();
        seismic::BuildingSeis b;
        try { b = seismic::readBuildingSeis(ss.str()); } catch (const std::exception& e) { throw Error("seis.err.parse", {e.what()}); }
        std::vector<std::string> flags;
        const auto m = kv(a, 1, &flags);
        const int I = static_cast<int>(num(m, "I", 9));
        const double pga = m.count("PGA") ? parseNumber(m.at("PGA")) : seismic::pgaForIntensity(b.pga9, I);
        std::string dirs = m.count("DIR") ? utf8::toUpper(m.at("DIR")) : "XY";
        const bool sweep = std::find(flags.begin(), flags.end(), "SWEEP") != flags.end();
        xlsx::Workbook wb;
        xlsx::Sheet* sh = nullptr;
        if (m.count("XLSX")) {
            sh = &wb.add(loc_.tr("seis.xl.sheet"));
            sh->add({xlsx::Cell::s(loc_.tr("seis.xl.dir"), 1), xlsx::Cell::s(loc_.tr("seis.xl.int"), 1), xlsx::Cell::s("PGA, g", 1),
                     xlsx::Cell::s(loc_.tr("seis.xl.check"), 1), xlsx::Cell::s(loc_.tr("seis.xl.ratio"), 1), xlsx::Cell::s(loc_.tr("xl.c.status"), 1)});
            sh->widths = {8, 8, 9, 30, 12, 14};
        }
        say("seis.msg.model", {b.name, std::to_string(b.stories.size()), std::format("{:.3f}", b.pga9)});
        for (char d : dirs) {
            const char dl = d == 'Y' ? 'y' : 'x';
            const auto r = seismic::evaluateSeismic(b, pga, dl);
            std::string T;
            for (std::size_t k = 0; k < std::min<std::size_t>(3, r.modes.size()); ++k) T += std::format("{}{:.2f}", k ? " / " : "", r.modes[k].T);
            say("seis.msg.result", {std::string(1, d), std::format("{:.3f}", pga), T, std::format("{:.0f}", r.resp.baseShear / 1000.0),
                                    std::format("{:.1f}", r.resp.roof * 100), std::format("{}", r.resp.maxDrift > 0 ? static_cast<int>(1 / r.resp.maxDrift) : 0)});
            for (const auto& c : r.checks)
                say(c.ratio < 1 ? "seis.msg.ok" : "seis.msg.fail", {loc_.tr("seis.chk." + c.key), std::format("{:.0f}", c.ratio * 100),
                                                                    c.story ? std::to_string(c.story) : std::string("—")});
            if (sweep || sh) {
                for (int i = 6; i <= 12; ++i) {
                    const double p = seismic::pgaForIntensity(b.pga9, i);
                    const auto ri = seismic::evaluateSeismic(b, p, dl);
                    if (sweep) {
                        const auto& w = ri.checks.front();
                        say(ri.ok() ? "seis.msg.sweep_ok" : "seis.msg.sweep_fail", {std::to_string(i), std::format("{:.2f}", p), loc_.tr("seis.chk." + w.key),
                                                                                  std::format("{:.0f}", w.ratio * 100)});
                    }
                    if (sh)
                        for (const auto& c : ri.checks)
                            sh->add({xlsx::Cell::s(std::string(1, d)), xlsx::Cell::n(i, 0), xlsx::Cell::n(p, 3), xlsx::Cell::s(loc_.tr("seis.chk." + c.key)),
                                     xlsx::Cell::n(c.ratio * 100, 0), xlsx::Cell::s(loc_.tr(c.ratio < 1 ? "seis.xl.ok" : "seis.xl.bad"), c.ratio < 1 ? 0 : 6)});
                }
            }
        }
        if (sh) { xlsx::save(wb, m.at("XLSX")); say("dev.msg.saved", {m.at("XLSX")}); }
        return true;
    }
    if (cmd == "PILECAP") {
        const auto m = kv(a, 0);
        geotech::PileInput in;
        in.D = num(m, "D", 0, true);
        in.L = num(m, "L", 0, true);
        in.R = num(m, "R", 0, true);
        in.fAvg = num(m, "F", 0, true);
        in.gRs = num(m, "GRS", 1.5);
        in.gRu = num(m, "GRU", 2.0);
        const auto r = geotech::pileCapacity(in);
        say("geo.msg.pile", {std::format("{:.0f}", r.Qs), std::format("{:.0f}", r.Qb), std::format("{:.0f}", r.Qd), std::format("{:.0f}", r.Fd)});
        if (m.count("LOAD")) {
            const double P = parseNumber(m.at("LOAD"));
            say(P <= r.Qd ? "geo.msg.pile_ok" : "geo.msg.pile_bad", {std::format("{:.0f}", P), std::format("{:.2f}", r.Qd / P)});
        }
        return true;
    }
    if (cmd == "BEARING") {
        const auto m = kv(a, 0);
        geotech::BearingInput in;
        in.B = num(m, "B", 0, true); in.L = num(m, "L", in.B); in.Df = num(m, "DF", 0);
        in.c = num(m, "C", 0); in.phiDeg = num(m, "PHI", 30); in.gamma = num(m, "G", 19); in.gammaAbove = in.gamma; in.FS = num(m, "FS", 3);
        const auto r = geotech::bearingCapacity(in);
        say("geo.msg.bearing", {std::format("{:.2f}", r.Nc), std::format("{:.2f}", r.Nq), std::format("{:.2f}", r.Ng), std::format("{:.0f}", r.qult),
                                std::format("{:.0f}", r.qall)});
        return true;
    }
    if (cmd == "SETTLE") {
        const auto m = kv(a, 0);
        geotech::SettlementInput in;
        in.B = num(m, "B", 0, true); in.L = num(m, "L", in.B); in.Df = num(m, "DF", 0); in.p = num(m, "P", 0, true);
        if (!m.count("LAYERS")) throw usage();
        std::stringstream ls(m.at("LAYERS"));
        std::string tok;
        while (std::getline(ls, tok, ',')) {
            geotech::SoilLayer l;
            const auto p1 = tok.find(':'), p2 = tok.find(':', p1 + 1);
            if (p1 == std::string::npos) throw usage();
            l.h = parseNumber(tok.substr(0, p1));
            l.E = parseNumber(tok.substr(p1 + 1, p2 == std::string::npos ? std::string::npos : p2 - p1 - 1));
            if (p2 != std::string::npos) l.gamma = parseNumber(tok.substr(p2 + 1));
            in.layers.push_back(l);
        }
        const auto r = geotech::settlement(in);
        say("geo.msg.settle", {std::format("{:.1f}", r.s * 1000), std::format("{:.1f}", r.Hc), std::format("{:.0f}", r.p0)});
        return true;
    }
    if (cmd == "PIT") {
        const auto m = kv(a, 0);
        const auto r = geotech::pitCheck(num(m, "C", 0, true), num(m, "T", 0, true), num(m, "L0", 0, true), num(m, "TOL", 1.5));
        say(r.flag ? "geo.msg.pit_bad" : "geo.msg.pit_ok", {std::format("{:.2f}", r.L), std::format("{:+.2f}", -r.shortfall), loc_.tr("geo.pit.q" + std::to_string(r.quality))});
        return true;
    }
    return false;
}

}  // namespace asiacad::cli
