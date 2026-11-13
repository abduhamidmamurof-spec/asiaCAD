// AsiaCAD — 3D sahna eksporti (OBJ/MTL, glTF 2.0, markerlar, quyosh).
#include <asiacad/bim/scene_export.h>

#include <asiacad/core/error.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>

namespace asiacad::bim {

namespace {
constexpr double kPI = std::numbers::pi;

std::string stem(const std::string& path) { return std::filesystem::path(path).stem().string(); }
std::string sibling(const std::string& path, const std::string& ext) {
    auto p = std::filesystem::path(path);
    p.replace_extension(ext);
    return p.string();
}
Vec3 cross(const Vec3& a, const Vec3& b, const Vec3& c) { return (b - a).cross(c - a); }
}  // namespace

const std::vector<SceneMaterial>& sceneMaterials() {
    static const std::vector<SceneMaterial> m = {
        {"AC_Beton", {0.62, 0.61, 0.58}, 0.85, 0.0, 1.0},
        {"AC_Beton_Temel", {0.50, 0.50, 0.48}, 0.9, 0.0, 1.0},
        {"AC_Gazoblok", {0.86, 0.84, 0.80}, 0.9, 0.0, 1.0},
        {"AC_Shisha", {0.55, 0.68, 0.78}, 0.05, 0.0, 0.35},
        {"AC_Alyuminiy", {0.75, 0.76, 0.78}, 0.35, 1.0, 1.0},
        {"AC_Tosh", {0.80, 0.76, 0.68}, 0.7, 0.0, 1.0},
        {"AC_Metall", {0.55, 0.56, 0.58}, 0.4, 1.0, 1.0},
        {"AC_Asfalt", {0.18, 0.18, 0.19}, 0.95, 0.0, 1.0},
        {"AC_Plitka", {0.66, 0.62, 0.56}, 0.8, 0.0, 1.0},
        {"AC_Chim", {0.30, 0.48, 0.20}, 0.95, 0.0, 1.0},
        {"AC_Grunt", {0.52, 0.42, 0.30}, 1.0, 0.0, 1.0},
        {"AC_Suv", {0.20, 0.40, 0.55}, 0.05, 0.0, 0.6},
        {"AC_Boshqa", {0.70, 0.70, 0.70}, 0.8, 0.0, 1.0},
    };
    return m;
}
const SceneMaterial& sceneMaterial(const std::string& name) {
    for (const auto& m : sceneMaterials())
        if (m.name == name) return m;
    return sceneMaterials().back();
}

void Scene::add(const std::string& material, const std::array<Vec3, 3>& t) {
    for (auto& m : meshes)
        if (m.material == material) { m.tris.push_back(t); return; }
    meshes.push_back({material, {t}});
}
std::size_t Scene::triangleCount() const {
    std::size_t n = 0;
    for (const auto& m : meshes) n += m.tris.size();
    return n;
}

void addPrism(Scene& s, const std::string& material, const std::vector<Vec3>& outline, const std::vector<std::vector<Vec3>>& holes, double z0, double z1) {
    if (outline.size() < 3 || z1 - z0 < 1e-6) return;
    auto at = [](const std::vector<Vec3>& loop, double z) {
        std::vector<Vec3> r;
        for (const auto& p : loop) r.push_back({p.x, p.y, z});
        return r;
    };
    // tashqi kontur CCW bo'lsin (teshiklar — CW)
    auto area = [](const std::vector<Vec3>& l) {
        double a = 0;
        for (std::size_t i = 0; i < l.size(); ++i) a += l[i].x * l[(i + 1) % l.size()].y - l[(i + 1) % l.size()].x * l[i].y;
        return a;
    };
    std::vector<Vec3> o = outline;
    if (area(o) < 0) std::reverse(o.begin(), o.end());
    std::vector<std::vector<Vec3>> hs = holes;
    for (auto& h : hs)
        if (area(h) > 0) std::reverse(h.begin(), h.end());
    // ust (yuqoriga qaragan) va ost
    std::vector<std::vector<Vec3>> htop, hbot;
    for (const auto& h : hs) { htop.push_back(at(h, z1)); hbot.push_back(at(h, z0)); }
    for (const auto& t : triangulatePolygon(at(o, z1), htop)) {
        if (cross(t[0], t[1], t[2]).z < 0) s.add(material, {t[0], t[2], t[1]});
        else s.add(material, t);
    }
    for (const auto& t : triangulatePolygon(at(o, z0), hbot)) {
        if (cross(t[0], t[1], t[2]).z > 0) s.add(material, {t[0], t[2], t[1]});
        else s.add(material, t);
    }
    // yon yuzlar (tashqariga): CCW tashqi va CW teshik uchun bir xil qoida
    auto sides = [&](const std::vector<Vec3>& l) {
        for (std::size_t i = 0; i < l.size(); ++i) {
            const Vec3& a = l[i];
            const Vec3& b = l[(i + 1) % l.size()];
            const Vec3 a0{a.x, a.y, z0}, b0{b.x, b.y, z0}, a1{a.x, a.y, z1}, b1{b.x, b.y, z1};
            s.add(material, {a0, b0, b1});
            s.add(material, {a0, b1, a1});
        }
    };
    sides(o);
    for (const auto& h : hs) sides(h);
}

void addBuilding(Scene& s, const BuildingModel& m) {
    for (const auto& p : m.prisms) {
        std::string mat = "AC_Beton";
        switch (p.kind) {
            case ElemKind::Lean:
            case ElemKind::FoundationSlab:
            case ElemKind::StripFooting: mat = "AC_Beton_Temel"; break;
            case ElemKind::Infill: mat = "AC_Gazoblok"; break;
            case ElemKind::Opening:
            case ElemKind::Outline:
            case ElemKind::Ignore: continue;
            default: break;
        }
        addPrism(s, mat, p.outline, p.holes, p.z0, p.z1);
    }
    for (const auto& g : m.glass) {
        if (g.quad.size() < 4) continue;
        s.add("AC_Shisha", {g.quad[0], g.quad[1], g.quad[2]});
        s.add("AC_Shisha", {g.quad[0], g.quad[2], g.quad[3]});
        // ramka (alyuminiy) — chekka bo'ylab ingichka chiziq o'rniga shishaning orqa yuzi
        s.add("AC_Shisha", {g.quad[0], g.quad[2], g.quad[1]});
        s.add("AC_Shisha", {g.quad[0], g.quad[3], g.quad[2]});
    }
}

void addIfc(Scene& s, const IfcModel& m) {
    for (const auto& e : m.elements) {
        const std::string cat = ifcCategory(e.type);
        std::string mat = "AC_Beton";
        if (cat == "WINDOW" || cat == "PLATE" || cat == "CURTAINWALL") mat = "AC_Shisha";
        else if (cat == "RAILING" || cat == "MEMBER") mat = "AC_Metall";
        else if (cat == "SPACE" || cat == "OPENING" || cat == "REBAR") continue;
        else if (cat == "FOOTING" || cat == "PILE") mat = "AC_Beton_Temel";
        else if (cat == "PROXY") {
            const std::string n = e.name;
            if (n.find("Dolgu") != std::string::npos || n.find("dolgu") != std::string::npos) mat = "AC_Grunt";
            else if (n.find("Su ") != std::string::npos || n.find("suv") != std::string::npos) mat = "AC_Suv";
            else mat = "AC_Beton_Temel";
        }
        for (const auto& p : e.parts) {
            const std::string pm = p.alpha < 0.7 && mat == "AC_Beton" ? "AC_Shisha" : mat;
            for (const auto& t : p.tris) s.add(pm, t);
        }
    }
}

void centerScene(Scene& s) {
    double x0 = 1e300, y0 = 1e300, z0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (const auto& m : s.meshes)
        for (const auto& t : m.tris)
            for (const auto& p : t) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); z0 = std::min(z0, p.z); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    if (x0 > x1) return;
    s.origin = {(x0 + x1) / 2, (y0 + y1) / 2, z0};
}

void writeObj(const Scene& s, const std::string& objPath) {
    const std::string mtl = sibling(objPath, ".mtl");
    {
        std::ofstream f(mtl, std::ios::binary);
        if (!f) throw Error("export.err.write", {mtl});
        f << "# AsiaCAD materiallari — render dasturida shu nomlar bo'yicha moslang\n";
        for (const auto& mm : s.meshes) {
            const auto& m = sceneMaterial(mm.material);
            f << "newmtl " << mm.material << "\nKd " << m.rgb[0] << ' ' << m.rgb[1] << ' ' << m.rgb[2] << "\nKa 0 0 0\nKs " << (m.metallic > 0.5 ? 0.6 : 0.1)
              << ' ' << (m.metallic > 0.5 ? 0.6 : 0.1) << ' ' << (m.metallic > 0.5 ? 0.6 : 0.1) << "\nNs " << std::format("{:.0f}", (1 - m.roughness) * 900 + 10)
              << "\nd " << m.opacity << "\nillum 2\n\n";
        }
    }
    std::ofstream f(objPath, std::ios::binary);
    if (!f) throw Error("export.err.write", {objPath});
    f << "# AsiaCAD 3D eksport (Z yuqoriga, metr). Boshlanish nuqtasi: " << std::format("{:.3f} {:.3f} {:.3f}", s.origin.x, s.origin.y, s.origin.z) << "\n";
    f << "mtllib " << std::filesystem::path(mtl).filename().string() << "\n";
    std::size_t base = 1;
    for (const auto& mm : s.meshes) {
        f << "o " << mm.material << "\ng " << mm.material << "\nusemtl " << mm.material << "\n";
        for (const auto& t : mm.tris)
            for (const auto& p : t) f << std::format("v {:.4f} {:.4f} {:.4f}\n", p.x - s.origin.x, p.y - s.origin.y, p.z - s.origin.z);
        for (std::size_t i = 0; i < mm.tris.size(); ++i, base += 3) f << "f " << base << ' ' << base + 1 << ' ' << base + 2 << "\n";
    }
    if (!f) throw Error("export.err.write", {objPath});
}

void writeGltf(const Scene& s, const std::string& gltfPath) {
    const std::string binPath = sibling(gltfPath, ".bin");
    std::string bin;
    auto putF = [&](float v) { char b[4]; std::memcpy(b, &v, 4); bin.append(b, 4); };
    struct Acc { std::size_t posOff, posLen, nrmOff, nrmLen, count; float mn[3], mx[3]; };
    std::vector<Acc> acc;
    for (const auto& mm : s.meshes) {
        Acc a{};
        a.count = mm.tris.size() * 3;
        for (int k = 0; k < 3; ++k) { a.mn[k] = 1e30f; a.mx[k] = -1e30f; }
        a.posOff = bin.size();
        // glTF: Y yuqoriga → (x, z, -y)
        for (const auto& t : mm.tris)
            for (const auto& p : t) {
                const float v[3] = {float(p.x - s.origin.x), float(p.z - s.origin.z), float(-(p.y - s.origin.y))};
                for (int k = 0; k < 3; ++k) { putF(v[k]); a.mn[k] = std::min(a.mn[k], v[k]); a.mx[k] = std::max(a.mx[k], v[k]); }
            }
        a.posLen = bin.size() - a.posOff;
        a.nrmOff = bin.size();
        for (const auto& t : mm.tris) {
            Vec3 n = (t[1] - t[0]).cross(t[2] - t[0]);
            const double l = n.length();
            n = l > 0 ? Vec3{n.x / l, n.y / l, n.z / l} : Vec3{0, 0, 1};
            for (int r = 0; r < 3; ++r) { putF(float(n.x)); putF(float(n.z)); putF(float(-n.y)); }
        }
        a.nrmLen = bin.size() - a.nrmOff;
        acc.push_back(a);
    }
    {
        std::ofstream b(binPath, std::ios::binary);
        if (!b) throw Error("export.err.write", {binPath});
        b.write(bin.data(), static_cast<std::streamsize>(bin.size()));
    }
    std::string j = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"AsiaCAD\"},\"scene\":0,\"scenes\":[{\"nodes\":[";
    for (std::size_t i = 0; i < s.meshes.size(); ++i) j += (i ? "," : "") + std::to_string(i);
    j += "]}],\"nodes\":[";
    for (std::size_t i = 0; i < s.meshes.size(); ++i) j += std::format("{}{{\"name\":\"{}\",\"mesh\":{}}}", i ? "," : "", s.meshes[i].material, i);
    j += "],\"meshes\":[";
    for (std::size_t i = 0; i < s.meshes.size(); ++i)
        j += std::format("{}{{\"name\":\"{}\",\"primitives\":[{{\"attributes\":{{\"POSITION\":{},\"NORMAL\":{}}},\"material\":{}}}]}}", i ? "," : "",
                         s.meshes[i].material, 2 * i, 2 * i + 1, i);
    j += "],\"materials\":[";
    for (std::size_t i = 0; i < s.meshes.size(); ++i) {
        const auto& m = sceneMaterial(s.meshes[i].material);
        j += std::format("{}{{\"name\":\"{}\",\"pbrMetallicRoughness\":{{\"baseColorFactor\":[{:.3f},{:.3f},{:.3f},{:.3f}],\"metallicFactor\":{:.2f},"
                         "\"roughnessFactor\":{:.2f}}}{}{}}}",
                         i ? "," : "", s.meshes[i].material, m.rgb[0], m.rgb[1], m.rgb[2], m.opacity, m.metallic, m.roughness,
                         m.opacity < 1 ? ",\"alphaMode\":\"BLEND\"" : "", ",\"doubleSided\":true");
    }
    j += "],\"accessors\":[";
    for (std::size_t i = 0; i < acc.size(); ++i) {
        const auto& a = acc[i];
        j += std::format("{}{{\"bufferView\":{},\"componentType\":5126,\"count\":{},\"type\":\"VEC3\",\"min\":[{},{},{}],\"max\":[{},{},{}]}},"
                         "{{\"bufferView\":{},\"componentType\":5126,\"count\":{},\"type\":\"VEC3\"}}",
                         i ? "," : "", 2 * i, a.count, a.mn[0], a.mn[1], a.mn[2], a.mx[0], a.mx[1], a.mx[2], 2 * i + 1, a.count);
    }
    j += "],\"bufferViews\":[";
    for (std::size_t i = 0; i < acc.size(); ++i) {
        const auto& a = acc[i];
        j += std::format("{}{{\"buffer\":0,\"byteOffset\":{},\"byteLength\":{},\"target\":34962}},{{\"buffer\":0,\"byteOffset\":{},\"byteLength\":{},\"target\":34962}}",
                         i ? "," : "", a.posOff, a.posLen, a.nrmOff, a.nrmLen);
    }
    j += std::format("],\"buffers\":[{{\"uri\":\"{}\",\"byteLength\":{}}}]}}", std::filesystem::path(binPath).filename().string(), bin.size());
    std::ofstream f(gltfPath, std::ios::binary);
    if (!f) throw Error("export.err.write", {gltfPath});
    f << j;
}

void writeMarkersCsv(const Scene& s, const std::string& csvPath) {
    std::ofstream f(csvPath, std::ios::binary);
    if (!f) throw Error("export.err.write", {csvPath});
    f << "kind;x;y;z;height;rot\n";
    for (const auto& m : s.markers)
        f << std::format("{};{:.3f};{:.3f};{:.3f};{:.2f};{:.1f}\n", m.kind, m.p.x - s.origin.x, m.p.y - s.origin.y, m.p.z - s.origin.z, m.height, m.rotDeg);
}

SunPos sunPosition(double lat, double lon, int year, int month, int day, double hour, double utcOffset) {
    // kun tartib raqami
    static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int n = cum[(month - 1) % 12] + day;
    if (month > 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) ++n;
    const double g = 2 * kPI / 365.0 * (n - 1 + (hour - 12) / 24);
    const double eqt = 229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) - 0.014615 * std::cos(2 * g) - 0.040849 * std::sin(2 * g));
    const double decl = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) - 0.006758 * std::cos(2 * g) + 0.000907 * std::sin(2 * g) -
                        0.002697 * std::cos(3 * g) + 0.00148 * std::sin(3 * g);
    const double tst = hour * 60 + eqt + 4 * lon - 60 * utcOffset;
    const double ha = (tst / 4 - 180) * kPI / 180;
    const double la = lat * kPI / 180;
    const double cz = std::sin(la) * std::sin(decl) + std::cos(la) * std::cos(decl) * std::cos(ha);
    const double zen = std::acos(std::clamp(cz, -1.0, 1.0));
    double az = std::acos(std::clamp((std::sin(la) * std::cos(zen) - std::sin(decl)) / (std::cos(la) * std::sin(zen)), -1.0, 1.0)) * 180 / kPI;
    az = ha > 0 ? std::fmod(az + 180, 360) : std::fmod(540 - az, 360);
    return {az, 90 - zen * 180 / kPI};
}

}  // namespace asiacad::bim
