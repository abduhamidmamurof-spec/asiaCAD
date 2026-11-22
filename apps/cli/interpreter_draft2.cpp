// AsiaCAD CLI — qo'shimcha chizma buyruqlari (AutoCAD kabi):
//   MTEXT p h "matn" [W=kenglik] [ROT=°] [J=TL|TC|TR|ML|MC|MR|BL|BC|BR] [LS=1]   ko'p qatorli matn (\P yoki | — yangi qator)
//   DIMCONTINUE p [p ...] [BASE=id]          zanjirli o'lcham (oxirgi chiziqli o'lchamdan davom)
//   DIMBASELINE p [p ...] [BASE=id] [STEP=d] bazaviy o'lcham (bir tayanchdan, qadam bilan)
//   DIVIDE tanlov n [BLOCK=nom] [ALIGN]       egri chiziqni n teng bo'lakka (nuqta yoki blok)
//   MEASURE tanlov d [BLOCK=nom] [ALIGN]      egri chiziq bo'ylab har d masofada
//   PURGE [ALL|LAYERS|BLOCKS] [LIST]          ishlatilmagan qatlam va bloklarni o'chirish (undo qilinadi)
//   PLOT fayl.pdf [A0..A4] [PORTRAIT] [WINDOW=x1,y1,x2,y2] [SCALE=100] [UNITS=mm] [MONO] [LW=0.25] [FONT=f.ttf]
//   PLOT fayl.pdf SHEET=nom|ALL [MONO]          listlarni (ramka, shtamp, ko'rinishlar) PDF ga
#include "interpreter.h"

#include <asiacad/core/commands.h>
#include <asiacad/core/dimension.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/pdf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <sstream>

namespace asiacad::cli {

namespace {
constexpr double kPi = 3.14159265358979323846;

std::map<std::string, std::string> kvOpts(const std::vector<std::string>& a, std::size_t from, std::vector<std::string>& pos, std::set<std::string>& flags) {
    std::map<std::string, std::string> m;
    for (std::size_t i = from; i < a.size(); ++i) {
        const auto eq = a[i].find('=');
        if (eq != std::string::npos && eq > 0 && std::isalpha(static_cast<unsigned char>(a[i][0]))) m[utf8::toUpper(a[i].substr(0, eq))] = a[i].substr(eq + 1);
        else if (!a[i].empty() && std::isalpha(static_cast<unsigned char>(a[i][0])) && a[i].find(',') == std::string::npos) flags.insert(utf8::toUpper(a[i]));
        else pos.push_back(a[i]);
    }
    return m;
}

// Matnni qatorlarga bo'lish: \P, | yoki haqiqiy yangi qator; kenglik berilsa — so'z bo'yicha o'rash (≈0.6·h bir belgi)
std::vector<std::string> wrapLines(const std::string& src, double h, double width) {
    std::vector<std::string> paras;
    std::string cur;
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '\n' || src[i] == '|') { paras.push_back(cur); cur.clear(); continue; }
        if (src[i] == '\\' && i + 1 < src.size() && (src[i + 1] == 'P' || src[i + 1] == 'p')) { paras.push_back(cur); cur.clear(); ++i; continue; }
        cur += src[i];
    }
    paras.push_back(cur);
    if (width <= 0) return paras;
    const std::size_t maxChars = std::max<std::size_t>(1, static_cast<std::size_t>(width / (0.6 * h)));
    std::vector<std::string> out;
    for (const auto& p : paras) {
        std::istringstream ss(p);
        std::string w, line;
        bool any = false;
        while (ss >> w) {
            any = true;
            const std::size_t lw = utf8::length(line), ww = utf8::length(w);
            if (!line.empty() && lw + 1 + ww > maxChars) { out.push_back(line); line.clear(); }
            line += (line.empty() ? "" : " ") + w;
        }
        if (!any || !line.empty()) out.push_back(line);
    }
    return out;
}

// Ishlatilmagan qatlam/bloklarni o'chirish — undo bilan qaytariladi
class PurgeCommand final : public ICommand {
public:
    PurgeCommand(std::vector<Layer> layers, std::vector<BlockPtr> blocks) : layers_(std::move(layers)), blocks_(std::move(blocks)) {}
    void execute(Document& doc) override {
        for (auto& b : blocks_) doc.removeBlock(b->name);
        for (auto& l : layers_) doc.removeLayer(l.name);
    }
    void undo(Document& doc) override {
        for (auto& l : layers_) doc.addLayer(l);
        for (auto& b : blocks_) doc.addBlock(b);
    }
    std::string name() const override { return "PURGE"; }

private:
    std::vector<Layer> layers_;
    std::vector<BlockPtr> blocks_;
};

void collectUse(const IEntity& e, std::set<std::string>& layers, std::set<std::string>& blocks) {
    layers.insert(utf8::toUpper(e.layer()));
    if (auto* r = dynamic_cast<const InsertEntity*>(&e); r && r->block()) {
        const std::string n = utf8::toUpper(r->block()->name);
        if (blocks.insert(n).second)
            for (auto& c : r->block()->entities) collectUse(*c, layers, blocks);
    }
}
}  // namespace

bool Interpreter::dispatchDraft2(const std::string& cmd, const Args& a) {
    if (cmd == "MTEXT") {
        need(a, 3, cmd);
        std::vector<std::string> pos;
        std::set<std::string> flags;
        const auto o = kvOpts(a, 3, pos, flags);
        const Vec3 p = parsePoint(a[0]);
        const double h = parseNumber(a[1]);
        if (h <= 0) throw Error("err.height_positive");
        const double w = o.count("W") ? parseNumber(o.at("W")) : 0.0;
        const double rot = o.count("ROT") ? parseNumber(o.at("ROT")) * kPi / 180.0 : 0.0;
        const double ls = o.count("LS") ? parseNumber(o.at("LS")) : 1.0;
        const std::string j = o.count("J") ? utf8::toUpper(o.at("J")) : "TL";
        if (j.size() != 2 || std::string("TMB").find(j[0]) == std::string::npos || std::string("LCR").find(j[1]) == std::string::npos)
            throw Error("err.bad_value", {"J=" + j});
        const auto lines = wrapLines(a[2], h, w);
        const double step = h * 5.0 / 3.0 * ls;
        const Vec3 down{std::sin(rot), -std::cos(rot), 0}, right{std::cos(rot), std::sin(rot), 0};
        const auto ha = j[1] == 'L' ? TextHAlign::Left : j[1] == 'C' ? TextHAlign::Center : TextHAlign::Right;
        const auto va = j[0] == 'T' ? TextVAlign::Top : j[0] == 'M' ? TextVAlign::Middle : TextVAlign::Bottom;
        const double n = static_cast<double>(lines.size());
        // kenglik berilsa, gorizontal tekislash ramka ichida: markaz / o'ng chet
        const Vec3 base = p + right * (w > 0 ? (j[1] == 'C' ? w / 2 : j[1] == 'R' ? w : 0.0) : 0.0);
        std::vector<std::unique_ptr<IEntity>> es;
        for (std::size_t k = 0; k < lines.size(); ++k) {
            if (lines[k].find_first_not_of(' ') == std::string::npos) continue;
            const double kk = static_cast<double>(k);
            const double shift = j[0] == 'T' ? kk : j[0] == 'M' ? kk - (n - 1) / 2.0 : kk - (n - 1);
            es.push_back(std::make_unique<TextEntity>(base + down * (shift * step), h, lines[k], rot, ha, va));
        }
        if (es.empty()) throw Error("err.bad_value", {"\"\""});
        const std::size_t cnt = es.size();
        addMany("MTEXT", std::move(es));
        say("msg.mtext_created", {std::to_string(cnt)});
        return true;
    }
    if (cmd == "DIMCONTINUE" || cmd == "DIMBASELINE") {
        need(a, 1, cmd);
        std::vector<std::string> pos;
        std::set<std::string> flags;
        const auto o = kvOpts(a, 0, pos, flags);
        if (pos.empty()) throw Error("err.usage", {cmd});
        const DimensionEntity* base = nullptr;
        if (o.count("BASE")) base = dynamic_cast<const DimensionEntity*>(doc_.find(static_cast<EntityId>(parseNumber(o.at("BASE")))));
        else {
            const auto ids = doc_.ids();
            for (auto it = ids.rbegin(); it != ids.rend() && !base; ++it) {
                auto* d = dynamic_cast<const DimensionEntity*>(doc_.find(*it));
                if (d && (d->kind() == DimKind::Rotated || d->kind() == DimKind::Aligned)) base = d;
            }
        }
        if (!base || (base->kind() != DimKind::Rotated && base->kind() != DimKind::Aligned)) throw Error("err.dim_need_linear");
        const DimKind kind = base->kind();
        const double rot = base->rotation();
        Vec3 u = kind == DimKind::Aligned ? (base->p2() - base->p1()) : Vec3{std::cos(rot), std::sin(rot), 0};
        if (u.length2d() < 1e-12) u = {1, 0, 0};
        u = u / u.length2d();
        const Vec3 nrm{-u.y, u.x, 0};
        // o'lcham chizig'ining tayanch nuqtalardan siljishi (ishorasi bilan)
        const double off = (base->location() - base->p1()).dot(nrm);
        const double step = o.count("STEP") ? parseNumber(o.at("STEP")) : base->textHeight() * 3.0;
        const double h = base->textHeight();
        const int dec = base->decimals();
        std::vector<std::unique_ptr<IEntity>> es;
        Vec3 prev = base->p2();
        double level = off;
        const double sgn = off >= 0 ? 1.0 : -1.0;
        for (const auto& s : pos) {
            const Vec3 q = parsePoint(s);
            Vec3 p1 = cmd == "DIMCONTINUE" ? prev : base->p1();
            if (cmd == "DIMBASELINE") level += sgn * step;
            const Vec3 loc = p1 + nrm * (level - (p1 - base->p1()).dot(nrm));
            es.push_back(std::make_unique<DimensionEntity>(kind == DimKind::Aligned ? DimKind::Rotated : kind, p1, q, loc, h, dec,
                                                           kind == DimKind::Aligned ? std::atan2(u.y, u.x) : rot));
            prev = q;
        }
        const std::size_t cnt = es.size();
        addMany(cmd == "DIMCONTINUE" ? "DIMCONTINUE" : "DIMBASELINE", std::move(es));
        say("msg.dims_created", {std::to_string(cnt)});
        return true;
    }
    if (cmd == "DIVIDE" || cmd == "MEASURE") {
        need(a, 2, cmd);
        std::vector<std::string> pos;
        std::set<std::string> flags;
        const auto o = kvOpts(a, 2, pos, flags);
        const bool align = flags.count("ALIGN") || flags.count(utf8::toUpper(loc_.keywordName("ALIGN")));
        BlockPtr blk;
        if (o.count("BLOCK")) {
            blk = doc_.block(o.at("BLOCK"));
            if (!blk) throw Error("err.block_missing", {o.at("BLOCK")});
        }
        const double v = parseNumber(a[1]);
        std::vector<std::unique_ptr<IEntity>> es;
        for (EntityId id : parseSelection(a[0])) {
            auto* c = dynamic_cast<const ICurve*>(doc_.find(id));
            if (!c) continue;
            const double L = c->length();
            std::vector<double> ds;
            if (cmd == "DIVIDE") {
                const int n = static_cast<int>(v);
                if (n < 2 || n > 32767) throw Error("err.bad_value", {a[1]});
                for (int k = 1; k < n; ++k) ds.push_back(L * k / n);
            } else {
                if (v <= 0) throw Error("err.bad_value", {a[1]});
                for (double d = v; d < L - 1e-9; d += v) ds.push_back(d);
                if (ds.size() > 100000) throw Error("err.bad_value", {a[1]});
            }
            for (double d : ds) {
                const Vec3 p = c->pointAtDistance(d);
                if (!blk) { es.push_back(std::make_unique<PointEntity>(p)); continue; }
                double ang = 0;
                if (align) {
                    const Vec3 a1 = c->pointAtDistance(std::max(0.0, d - L * 1e-4)), a2 = c->pointAtDistance(std::min(L, d + L * 1e-4));
                    ang = std::atan2(a2.y - a1.y, a2.x - a1.x);
                }
                es.push_back(std::make_unique<InsertEntity>(blk, InsertEntity::placement(blk->base, p, 1, 1, 1, ang)));
            }
        }
        if (es.empty()) throw Error("err.need_curve");
        const std::size_t cnt = es.size();
        addMany(cmd == "DIVIDE" ? "DIVIDE" : "MEASURE", std::move(es));
        say("msg.marks_created", {std::to_string(cnt)});
        return true;
    }
    if (cmd == "PURGE") {
        std::set<std::string> flags;
        for (auto& s : a) flags.insert(utf8::toUpper(loc_.resolveKeyword(s).empty() ? s : loc_.resolveKeyword(s)));
        const bool doLayers = flags.empty() || flags.count("ALL") || flags.count("LAYERS") || flags.count("LIST") && !flags.count("BLOCKS");
        const bool doBlocks = flags.empty() || flags.count("ALL") || flags.count("BLOCKS") || flags.count("LIST") && !flags.count("LAYERS");
        const bool listOnly = flags.count("LIST") > 0;
        std::vector<Layer> ls;
        std::vector<BlockPtr> bs;
        // bloklar: ichma-ich — ishlatilmaganlar o'chirilgach boshqalari ham bo'shashi mumkin, shuning uchun takrorlanadi
        std::set<std::string> gone;
        for (int pass = 0; pass < 20; ++pass) {
            std::set<std::string> ul, ub;
            doc_.forEach([&](const IEntity& e) { collectUse(e, ul, ub); });
            bool more = false;
            if (doBlocks)
                for (auto& [k, b] : doc_.blocks()) {
                    if (gone.count(k) || ub.count(k)) continue;
                    // boshqa (qolayotgan) blok ichida ishlatilganmi
                    bool inner = false;
                    for (auto& [k2, b2] : doc_.blocks()) {
                        if (k2 == k || gone.count(k2)) continue;
                        for (auto& c : b2->entities)
                            if (auto* r = dynamic_cast<const InsertEntity*>(c.get()); r && r->block() && utf8::toUpper(r->block()->name) == k) inner = true;
                    }
                    if (inner) continue;
                    gone.insert(k);
                    bs.push_back(b);
                    more = true;
                }
            if (!more) break;
        }
        if (doLayers) {
            std::set<std::string> ul, ub;
            doc_.forEach([&](const IEntity& e) { collectUse(e, ul, ub); });
            for (auto& [k, b] : doc_.blocks())
                if (!gone.count(k)) for (auto& c : b->entities) ul.insert(utf8::toUpper(c->layer()));
            for (auto& [n, l] : doc_.layers()) {
                const std::string u = utf8::toUpper(n);
                if (u == "0" || u == "DEFPOINTS" || u == utf8::toUpper(layer_) || ul.count(u)) continue;
                ls.push_back(l);
            }
        }
        std::string names;
        for (auto& l : ls) names += (names.empty() ? "" : ", ") + l.name;
        for (auto& b : bs) names += (names.empty() ? "" : ", ") + b->name;
        if (ls.empty() && bs.empty()) { say("msg.purge_none"); return true; }
        if (listOnly) { say("msg.purge_list", {std::to_string(ls.size()), std::to_string(bs.size()), names}); return true; }
        stack_.execute(std::make_unique<PurgeCommand>(std::move(ls), std::move(bs)));
        say("msg.purged", {names});
        return true;
    }
    if (cmd == "PLOT") {
        need(a, 1, cmd);
        std::vector<std::string> pos;
        std::set<std::string> flags;
        const auto o = kvOpts(a, 1, pos, flags);
        io::PlotOptions po;
        for (const auto& f : flags) {
            const std::string kw = loc_.resolveKeyword(f);
            if (f.size() == 2 && f[0] == 'A' && std::isdigit(static_cast<unsigned char>(f[1]))) po.paper = f;
            else if (kw == "PORTRAIT") po.landscape = false;
            else if (kw == "LANDSCAPE") po.landscape = true;
            else if (f == "MONO" || kw == "MONO") po.mono = true;
        }
        if (o.count("PAPER")) po.paper = utf8::toUpper(o.at("PAPER"));
        if (o.count("SCALE")) {
            std::string sc = o.at("SCALE");
            if (auto c = sc.find(':'); c != std::string::npos) sc = sc.substr(c + 1);
            po.scale = parseNumber(sc);
            if (po.scale <= 0) throw Error("err.bad_value", {"SCALE=" + o.at("SCALE")});
        }
        if (o.count("UNITS")) po.unitMm = parseNumber(o.at("UNITS"));
        if (o.count("LW")) po.lineWidth = parseNumber(o.at("LW"));
        if (o.count("FONT")) po.font = o.at("FONT");
        if (o.count("MARGIN")) po.margin = parseNumber(o.at("MARGIN"));
        if (o.count("WINDOW")) {
            std::vector<double> v;
            std::stringstream ss(o.at("WINDOW"));
            std::string t;
            while (std::getline(ss, t, ',')) v.push_back(parseNumber(t));
            if (v.size() != 4) throw Error("err.bad_value", {"WINDOW=" + o.at("WINDOW")});
            po.hasWindow = true;
            po.w0 = {v[0], v[1], 0};
            po.w1 = {v[2], v[3], 0};
        }
        po.title = currentFile_.empty() ? "AsiaCAD" : currentFile_;
        io::PlotResult r;
        if (o.count("SHEET")) {
            std::vector<int> which;
            const std::string want = o.at("SHEET");
            if (utf8::toUpper(want) != "ALL" && loc_.resolveKeyword(want) != "ALL") {
                const auto& ss = doc_.sheets();
                for (int i = 0; i < static_cast<int>(ss.size()); ++i)
                    if (utf8::toUpper(ss[static_cast<std::size_t>(i)].name) == utf8::toUpper(want)) which.push_back(i);
                if (which.empty()) throw Error("err.bad_value", {"SHEET=" + want});
            }
            r = io::plotSheetsPdf(doc_, a[0], which, po, [this](const std::string& k) { return loc_.tr(k); });
        } else {
            r = io::plotModelPdf(doc_, a[0], po);
        }
        if (!r.ok) throw Error("err.plot", {r.error});
        say("msg.plotted", {a[0], std::to_string(r.pages), r.scale > 0 ? "1:" + std::format("{:g}", std::round(r.scale * 100) / 100) : std::string("—"),
                            r.fontUsed.empty() ? std::string("Helvetica") : std::filesystem::path(r.fontUsed).filename().string()});
        return true;
    }
    return false;
}

}  // namespace asiacad::cli
