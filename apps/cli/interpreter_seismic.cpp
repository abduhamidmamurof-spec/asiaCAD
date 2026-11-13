// AsiaCAD CLI — seysmik va geotexnik hisoblar (formulalar: bim/seismic.h, bim/geotech.h).
//   SEISMIC fayl.seis [I=6..12 | PGA=g] [DIR=X|Y] [SWEEP] [XLSX=f]   bino: modal, spektral, tekshiruvlar, 6–12 ball
//   PILECAP D=m L=m R=kPa F=kPa [GRS=1.5] [GRU=2] [LOAD=kN]          burg'ilangan qoziq ko'tarish qobiliyati
//   BEARING B=m L=m DF=m C=kPa PHI=° [G=kN/m³] [FS=3]                 sayoz poydevor zamini (Vesić)
//   SETTLE B=m L=m DF=m P=kPa LAYERS=h:E:γ,…                          cho'kish (qatlamlab yig'ish)
//   PIT C=m/s T=s L0=m [TOL=m]                                         qoziq butunligi (uzunlik, sifat)
#include "interpreter.h"

#include <asiacad/bim/geotech.h>
#include <asiacad/civil/axes.h>
#include <asiacad/bim/seismic.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/xlsx.h>

#include <cmath>
#include <format>
#include <fstream>
#include <map>
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
