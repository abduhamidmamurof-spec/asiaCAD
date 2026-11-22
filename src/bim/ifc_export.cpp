// AsiaCAD — IFC4 eksport (STEP): bino modeli, arxitektura devorlari, teshiklar, deraza/eshik, material, Qto.
#include <asiacad/bim/ifc_export.h>

#include <asiacad/core/error.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <map>
#include <set>

namespace asiacad::bim {

namespace {

std::string R(double v) {
    if (!std::isfinite(v)) v = 0;
    if (std::fabs(v) < 5e-10) return "0.";
    std::string s = std::format("{:.6f}", v);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') return s;
    if (s.find('.') == std::string::npos) s += ".";
    return s;
}

struct Writer {
    std::vector<std::string> lines;
    int add(const std::string& body) {
        lines.push_back(body);
        return static_cast<int>(lines.size());
    }
    static std::string ref(int id) { return "#" + std::to_string(id); }
    static std::string list(const std::vector<int>& ids) {
        std::string s = "(";
        for (std::size_t i = 0; i < ids.size(); ++i) s += (i ? "," : "") + ref(ids[i]);
        return s + ")";
    }
};

const char* kindIfc(ElemKind k, const char*& predef, const char*& name) {
    switch (k) {
    case ElemKind::Lean: predef = ".BASESLAB."; name = "Tayyorlov qatlami"; return "IFCSLAB";
    case ElemKind::FoundationSlab: predef = ".BASESLAB."; name = "Fundament plitasi"; return "IFCSLAB";
    case ElemKind::StripFooting: predef = ".STRIP_FOOTING."; name = "Lenta poydevor"; return "IFCFOOTING";
    case ElemKind::Column: predef = ".COLUMN."; name = "Ustun"; return "IFCCOLUMN";
    case ElemKind::Wall: predef = ".STANDARD."; name = "Devor (monolit)"; return "IFCWALL";
    case ElemKind::Slab: predef = ".FLOOR."; name = "Plita"; return "IFCSLAB";
    case ElemKind::Roof: predef = ".ROOF."; name = "Tom plitasi"; return "IFCSLAB";
    case ElemKind::Stair: predef = ".NOTDEFINED."; name = "Zina"; return "IFCSTAIR";
    case ElemKind::Shaft: predef = ".SHEAR."; name = "Shaxta devori"; return "IFCWALL";
    case ElemKind::Infill: predef = ".STANDARD."; name = "To'siq devor"; return "IFCWALL";
    default: return nullptr;
    }
}

bool kindIfcOk(ElemKind k) {
    const char* a = nullptr;
    const char* b = nullptr;
    return kindIfc(k, a, b) != nullptr;
}

Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 unit(const Vec3& v) {
    const double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 1e-12 ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0, 0, 1};
}

} // namespace

std::string ifcEncodeString(const std::string& s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (c == '\'') out += "''";
            else if (c == '\\') out += "\\\\";
            else out += static_cast<char>(c);
            ++i;
            continue;
        }
        // UTF-8 -> kod nuqta
        std::uint32_t cp = 0;
        int n = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
        else { ++i; continue; }
        if (i + n >= s.size()) break;
        for (int k = 1; k <= n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n + 1;
        std::vector<std::uint16_t> u;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            u.push_back(static_cast<std::uint16_t>(0xD800 + (cp >> 10)));
            u.push_back(static_cast<std::uint16_t>(0xDC00 + (cp & 0x3FF)));
        } else u.push_back(static_cast<std::uint16_t>(cp));
        out += "\\X2\\";
        for (auto w : u) out += std::format("{:04X}", w);
        out += "\\X0\\";
    }
    return out;
}

std::string ifcGuid(const std::string& key) {
    auto fnv = [](const std::string& s, std::uint64_t h) {
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        return h;
    };
    const std::uint64_t hi = fnv(key, 1469598103934665603ULL), lo = fnv(key + "#", 0x9E3779B97F4A7C15ULL);
    static const char* A = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$";
    std::string g;
    g += A[(hi >> 62) & 0x3];                       // birinchi belgi — 2 bit
    // qolgan 126 bit: hi ning past 62 biti (yuqori) + lo ning 64 biti (past); __int128 siz (MSVC)
    const std::uint64_t h62 = hi & ((1ULL << 62) - 1);
    auto bit = [&](int i) -> unsigned { return i < 64 ? static_cast<unsigned>((lo >> i) & 1U) : static_cast<unsigned>((h62 >> (i - 64)) & 1U); };
    for (int k = 20; k >= 0; --k) {
        unsigned v = 0;
        for (int j = 0; j < 6; ++j) v |= bit(6 * k + j) << j;
        g += A[v];
    }
    return g;
}

std::string ifcExportText(const IfcExportInput& in, IfcExportStats* statsOut) {
    Writer W;
    IfcExportStats st;
    int seq = 0;
    auto guid = [&](const std::string& k) { return "'" + ifcGuid(in.project + "|" + k + "|" + std::to_string(++seq)) + "'"; };
    auto S = [](const std::string& s) { return "'" + ifcEncodeString(s) + "'"; };
    auto pt3 = [&](double x, double y, double z) { return W.add(std::format("IFCCARTESIANPOINT(({},{},{}))", R(x), R(y), R(z))); };
    auto pt2 = [&](double x, double y) { return W.add(std::format("IFCCARTESIANPOINT(({},{}))", R(x), R(y))); };

    const int person = W.add(std::format("IFCPERSON($,$,{},$,$,$,$,$)", S(in.author)));
    const int org = W.add(std::format("IFCORGANIZATION($,{},$,$,$)", S(in.organization.empty() ? "AsiaCAD" : in.organization)));
    const int po = W.add(std::format("IFCPERSONANDORGANIZATION(#{},#{},$)", person, org));
    const int app = W.add(std::format("IFCAPPLICATION(#{},'0.39','AsiaCAD','AsiaCAD')", org));
    const int oh = W.add(std::format("IFCOWNERHISTORY(#{},#{},$,.ADDED.,$,$,$,0)", po, app));
    const std::string OH = Writer::ref(oh);
    const int uL = W.add("IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)");
    const int uA = W.add("IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.)");
    const int uV = W.add("IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.)");
    const int uR = W.add("IFCSIUNIT(*,.PLANEANGLEUNIT.,$,.RADIAN.)");
    const int units = W.add("IFCUNITASSIGNMENT(" + Writer::list({uL, uA, uV, uR}) + ")");
    const int o0 = pt3(0, 0, 0);
    const int dz = W.add("IFCDIRECTION((0.,0.,1.))");
    const int dx = W.add("IFCDIRECTION((1.,0.,0.))");
    const int ax0 = W.add(std::format("IFCAXIS2PLACEMENT3D(#{},#{},#{})", o0, dz, dx));
    const int ctx = W.add(std::format("IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-05,#{},$)", ax0));
    const int body = W.add(std::format("IFCGEOMETRICREPRESENTATIONSUBCONTEXT('Body','Model',*,*,*,*,#{},$,.MODEL_VIEW.,$)", ctx));
    const int project = W.add(std::format("IFCPROJECT({},{},{},$,$,$,$,(#{}),#{})", guid("project"), OH, S(in.project), ctx, units));
    const int sitePl = W.add(std::format("IFCLOCALPLACEMENT($,#{})", ax0));
    const int site = W.add(std::format("IFCSITE({},{},'Uchastka',$,$,#{},$,$,.ELEMENT.,$,$,$,$,$)", guid("site"), OH, sitePl));
    const int bPl = W.add(std::format("IFCLOCALPLACEMENT(#{},#{})", sitePl, ax0));
    const int bldg = W.add(std::format("IFCBUILDING({},{},{},$,$,#{},$,$,.ELEMENT.,$,$,$)", guid("building"), OH, S(in.building), bPl));
    W.add(std::format("IFCRELAGGREGATES({},{},$,$,#{},(#{}))", guid("ra1"), OH, project, site));
    W.add(std::format("IFCRELAGGREGATES({},{},$,$,#{},(#{}))", guid("ra2"), OH, site, bldg));

    // ---- qavatlar
    struct Storey { int id = 0, pl = 0; double elev = 0; std::vector<int> elems; };
    std::map<int, Storey> storeys;   // daraja -> qavat
    std::map<int, double> elevOf;
    if (in.model)
        for (const auto& p : in.model->prisms) {
            if (!kindIfcOk(p.kind)) continue;
            auto it = elevOf.find(p.level);
            if (it == elevOf.end() || p.z0 < it->second) elevOf[p.level] = p.z0;
        }
    if (in.model)
        for (const auto& g : in.model->glass)
            if (!elevOf.count(g.level) && !g.quad.empty()) elevOf[g.level] = g.quad[0].z;
    if (!in.walls.empty() && !elevOf.count(1)) {
        double z = in.walls.front().a.z;
        for (const auto& w : in.walls) z = std::min(z, w.a.z);
        elevOf[1] = z;
    }
    std::vector<int> storeyIds;
    for (const auto& [lvl, z] : elevOf) {
        Storey s;
        s.elev = z;
        const int sp = pt3(0, 0, z);
        const int sax = W.add(std::format("IFCAXIS2PLACEMENT3D(#{},#{},#{})", sp, dz, dx));
        s.pl = W.add(std::format("IFCLOCALPLACEMENT(#{},#{})", bPl, sax));
        const std::string nm = lvl <= 0 ? "Fundament" : std::to_string(lvl) + "-qavat";
        s.id = W.add(std::format("IFCBUILDINGSTOREY({},{},{},$,$,#{},$,$,.ELEMENT.,{})", guid("storey" + std::to_string(lvl)), OH, S(nm), s.pl, R(z)));
        storeyIds.push_back(s.id);
        storeys[lvl] = s;
    }
    if (!storeyIds.empty()) W.add(std::format("IFCRELAGGREGATES({},{},$,$,#{},{})", guid("ra3"), OH, bldg, Writer::list(storeyIds)));
    st.storeys = static_cast<int>(storeyIds.size());

    std::map<std::string, std::vector<int>> byMaterial;
    auto shapeOf = [&](int item, const char* type) {
        const int rep = W.add(std::format("IFCSHAPEREPRESENTATION(#{},'Body','{}',(#{}))", body, type, item));
        return W.add(std::format("IFCPRODUCTDEFINITIONSHAPE($,$,(#{}))", rep));
    };
    // kontur (+teshiklar) -> z0..z1 ekstruziya (lokal: qavat belgisiga nisbatan)
    auto extruded = [&](const std::vector<Vec3>& outline, const std::vector<std::vector<Vec3>>& holes, double z0, double z1, double elev) {
        auto poly = [&](const std::vector<Vec3>& pts) {
            std::vector<int> ids;
            for (const auto& p : pts) ids.push_back(pt2(p.x, p.y));
            ids.push_back(ids.front());
            return W.add("IFCPOLYLINE(" + Writer::list(ids) + ")");
        };
        const int outer = poly(outline);
        int prof;
        if (holes.empty()) prof = W.add(std::format("IFCARBITRARYCLOSEDPROFILEDEF(.AREA.,$,#{})", outer));
        else {
            std::vector<int> inner;
            for (const auto& h : holes)
                if (h.size() >= 3) inner.push_back(poly(h));
            prof = W.add(std::format("IFCARBITRARYPROFILEDEFWITHVOIDS(.AREA.,$,#{},{})", outer, Writer::list(inner)));
        }
        const int p0 = pt3(0, 0, z0 - elev);
        const int pos = W.add(std::format("IFCAXIS2PLACEMENT3D(#{},#{},#{})", p0, dz, dx));
        const int solid = W.add(std::format("IFCEXTRUDEDAREASOLID(#{},#{},#{},{})", prof, pos, dz, R(std::max(z1 - z0, 1e-4))));
        return shapeOf(solid, "SweptSolid");
    };
    auto qto = [&](int elem, const char* set, double volume, double area) {
        std::vector<int> q;
        q.push_back(W.add(std::format("IFCQUANTITYVOLUME('NetVolume',$,$,{},$)", R(volume))));
        if (area > 0) q.push_back(W.add(std::format("IFCQUANTITYAREA('NetArea',$,$,{},$)", R(area))));
        const int eq = W.add(std::format("IFCELEMENTQUANTITY({},{},'{}',$,$,{})", guid("qto"), OH, set, Writer::list(q)));
        W.add(std::format("IFCRELDEFINESBYPROPERTIES({},{},$,$,(#{}),#{})", guid("rdp"), OH, elem, eq));
    };
    // yupqa quti (deraza / eshik oynasi): to'rtburchak (4 nuqta) ± qalinlik/2
    auto paneShape = [&](const std::vector<Vec3>& q, double thick, double elev) {
        const Vec3 n = unit(cross(q[1] - q[0], q[3] - q[0]));
        std::string pts;
        for (int side = 0; side < 2; ++side)
            for (const auto& p : q) {
                const double s = (side ? 0.5 : -0.5) * thick;
                pts += std::format("{}({},{},{})", pts.empty() ? "" : ",", R(p.x + n.x * s), R(p.y + n.y * s), R(p.z + n.z * s - elev));
            }
        const int pl = W.add("IFCCARTESIANPOINTLIST3D((" + pts + "),$)");
        const char* F = "((1,3,2),(1,4,3),(5,6,7),(5,7,8),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,4,8),(3,8,7),(4,1,5),(4,5,8))";
        const int fs = W.add(std::format("IFCTRIANGULATEDFACESET(#{},$,.T.,{},$)", pl, F));
        return shapeOf(fs, "Tessellation");
    };

    // ---- bino modeli
    if (in.model) {
        int n = 0;
        for (const auto& p : in.model->prisms) {
            const char* predef = nullptr;
            const char* nm = nullptr;
            const char* cls = kindIfc(p.kind, predef, nm);
            if (!cls || p.outline.size() < 3) continue;
            auto& s = storeys[p.level];
            const int shape = extruded(p.outline, p.holes, p.z0, p.z1, s.elev);
            const std::string tag = std::string(kindKey(p.kind)) + "-" + std::to_string(++n);
            const int e = W.add(std::format("{}({},{},{},$,$,#{},#{},'{}',{})", cls, guid(tag), OH, S(nm), s.pl, shape, tag, predef));
            s.elems.push_back(e);
            const double vol = p.volume();
            st.volume += vol;
            ++st.elements;
            const bool isSlab = std::string(cls) == "IFCSLAB";
            qto(e, isSlab ? "Qto_SlabBaseQuantities" : std::string(cls) == "IFCWALL" ? "Qto_WallBaseQuantities"
                       : std::string(cls) == "IFCCOLUMN" ? "Qto_ColumnBaseQuantities" : std::string(cls) == "IFCFOOTING" ? "Qto_FootingBaseQuantities"
                                                                                                                : "Qto_StairBaseQuantities",
                vol, isSlab ? p.area() : 0);
            std::string mat = p.kind == ElemKind::Infill ? in.wallMaterial : (p.concrete.empty() ? "Beton" : "Beton " + p.concrete);
            byMaterial[mat].push_back(e);
        }
        int gw = 0;
        for (const auto& g : in.model->glass) {
            if (g.quad.size() != 4) continue;
            auto& s = storeys[g.level];
            const double w = g.quad[0].distanceTo(g.quad[1]), h = g.quad[0].distanceTo(g.quad[3]);
            const int shape = paneShape(g.quad, 0.06, s.elev);
            const std::string tag = (g.door ? "D-" : "W-") + std::to_string(++gw);
            const int e = g.door ? W.add(std::format("IFCDOOR({},{},'Eshik',$,$,#{},#{},'{}',{},{},.DOOR.,.NOTDEFINED.,$)", guid(tag), OH, s.pl, shape, tag, R(h), R(w)))
                                 : W.add(std::format("IFCWINDOW({},{},'Deraza',$,$,#{},#{},'{}',{},{},.WINDOW.,.NOTDEFINED.,$)", guid(tag), OH, s.pl, shape, tag, R(h), R(w)));
            s.elems.push_back(e);
            (g.door ? st.doors : st.windows)++;
            byMaterial["Shisha"].push_back(e);
        }
    }
    // ---- arxitektura devorlari (teshiklar bilan)
    if (!in.walls.empty()) {
        auto& s = storeys[1];
        int n = 0;
        for (const auto& w : in.walls) {
            if (w.length() < 1e-6) continue;
            const double z0 = w.a.z, z1 = w.a.z + w.h;
            const Vec3 uu = unit(w.b - w.a);
            const Vec3 nl{-uu.y, uu.x, 0};
            const bool haveCorners = w.la.distanceTo(w.ra) > 1e-6 && w.lb.distanceTo(w.rb) > 1e-6;
            auto off = [&](const Vec3& p, double d) { return Vec3{p.x + nl.x * d, p.y + nl.y * d, p.z}; };
            const std::vector<Vec3> fp = haveCorners ? std::vector<Vec3>{w.ra, w.rb, w.lb, w.la}
                                                     : std::vector<Vec3>{off(w.a, -w.rightOff()), off(w.b, -w.rightOff()), off(w.b, w.leftOff()), off(w.a, w.leftOff())};
            const int shape = extruded(fp, {}, z0, z1, s.elev);
            const std::string tag = w.id.empty() ? "AW-" + std::to_string(++n) : w.id;
            const int e = W.add(std::format("IFCWALL({},{},'Devor',$,$,#{},#{},'{}',.STANDARD.)", guid(tag), OH, s.pl, shape, ifcEncodeString(tag)));
            s.elems.push_back(e);
            ++st.elements;
            double vol = 0;
            {   // hajm: taglik ko'pburchak yuzasi × balandlik − teshiklar
                double A = 0;
                for (std::size_t i = 0; i < fp.size(); ++i) A += fp[i].x * fp[(i + 1) % fp.size()].y - fp[(i + 1) % fp.size()].x * fp[i].y;
                vol = std::fabs(A) * 0.5 * w.h;
            }
            const Vec3 u = unit(w.b - w.a);
            const Vec3 nn{-u.y, u.x, 0};
            const double mid = (w.leftOff() - w.rightOff()) * 0.5;   // devor o'rta chizig'ining o'qdan siljishi (chap +)
            for (const auto& o : w.ops) {
                const double c0 = o.pos - o.w / 2, c1 = o.pos + o.w / 2, half = w.t / 2 + 0.05;
                auto P = [&](double along, double off) { return Vec3{w.a.x + u.x * along + nn.x * (mid + off), w.a.y + u.y * along + nn.y * (mid + off), 0}; };
                const std::vector<Vec3> box{P(c0, -half), P(c1, -half), P(c1, half), P(c0, half)};
                const double oz0 = z0 + o.sill, oz1 = std::min(z1, oz0 + o.h);
                const int oshape = extruded(box, {}, oz0, oz1, s.elev);
                const int op = W.add(std::format("IFCOPENINGELEMENT({},{},'Teshik',$,$,#{},#{},$,.OPENING.)", guid(tag + "o"), OH, s.pl, oshape));
                W.add(std::format("IFCRELVOIDSELEMENT({},{},$,$,#{},#{})", guid(tag + "v"), OH, e, op));
                ++st.openings;
                vol -= o.w * (oz1 - oz0) * w.t;
                const std::vector<Vec3> q{Vec3{P(c0, 0).x, P(c0, 0).y, oz0}, Vec3{P(c1, 0).x, P(c1, 0).y, oz0}, Vec3{P(c1, 0).x, P(c1, 0).y, oz1},
                                          Vec3{P(c0, 0).x, P(c0, 0).y, oz1}};
                const int fshape = paneShape(q, o.kind == 'D' ? 0.05 : 0.07, s.elev);
                const bool door = o.kind == 'D';
                const int f = door ? W.add(std::format("IFCDOOR({},{},'Eshik',$,$,#{},#{},$,{},{},.DOOR.,.NOTDEFINED.,$)", guid(tag + "d"), OH, s.pl, fshape, R(o.h), R(o.w)))
                                   : W.add(std::format("IFCWINDOW({},{},'Deraza',$,$,#{},#{},$,{},{},.WINDOW.,.NOTDEFINED.,$)", guid(tag + "w"), OH, s.pl, fshape, R(o.h), R(o.w)));
                W.add(std::format("IFCRELFILLSELEMENT({},{},$,$,#{},#{})", guid(tag + "f"), OH, op, f));
                s.elems.push_back(f);
                (door ? st.doors : st.windows)++;
                byMaterial[door ? "Yog'och / metall" : "Shisha"].push_back(f);
            }
            vol = std::max(vol, 0.0);
            st.volume += vol;
            qto(e, "Qto_WallBaseQuantities", vol, 0);
            byMaterial[in.wallMaterial].push_back(e);
        }
    }
    for (const auto& [lvl, s] : storeys)
        if (!s.elems.empty())
            W.add(std::format("IFCRELCONTAINEDINSPATIALSTRUCTURE({},{},$,$,{},#{})", guid("cont" + std::to_string(lvl)), OH, Writer::list(s.elems), s.id));
    for (const auto& [name, ids] : byMaterial) {
        const int m = W.add(std::format("IFCMATERIAL({},$,$)", S(name)));
        W.add(std::format("IFCRELASSOCIATESMATERIAL({},{},$,$,{},#{})", guid("mat" + name), OH, Writer::list(ids), m));
    }

    std::string out = "ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('ViewDefinition [ReferenceView_V1.2]'),'2;1');\n";
    out += std::format("FILE_NAME({},'{}',({}),({}),'AsiaCAD 0.39','AsiaCAD 0.39','');\n", S(in.project + ".ifc"), in.timestamp, S(in.author),
                       S(in.organization));
    out += "FILE_SCHEMA(('IFC4'));\nENDSEC;\nDATA;\n";
    for (std::size_t i = 0; i < W.lines.size(); ++i) out += "#" + std::to_string(i + 1) + "=" + W.lines[i] + ";\n";
    out += "ENDSEC;\nEND-ISO-10303-21;\n";
    st.entities = static_cast<int>(W.lines.size());
    if (statsOut) *statsOut = st;
    return out;
}

IfcExportStats writeIfcFile(const IfcExportInput& in, const std::string& path) {
    IfcExportStats st;
    const std::string text = ifcExportText(in, &st);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw Error("ifc.err.write", {path});
    f << text;
    if (!f) throw Error("ifc.err.write", {path});
    return st;
}

} // namespace asiacad::bim
