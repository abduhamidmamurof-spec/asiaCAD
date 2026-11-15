// AsiaCAD — 3D sahna eksporti (OBJ/MTL, glTF 2.0, markerlar, quyosh).
#include <asiacad/bim/scene_export.h>

#include <asiacad/core/error.h>

#include <algorithm>
#include <cctype>
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
        {"AC_Panel", {0.32, 0.33, 0.35}, 0.45, 0.6, 1.0},
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
    // "IFC_RRGGBB" / "IFC_RRGGBB_G" (shaffof) — IFC rangidan
    if (name.size() >= 10 && name.rfind("IFC_", 0) == 0) {
        static std::map<std::string, SceneMaterial> cache;
        auto it = cache.find(name);
        if (it == cache.end()) {
            SceneMaterial m;
            m.name = name;
            const unsigned v = static_cast<unsigned>(std::strtoul(name.substr(4, 6).c_str(), nullptr, 16));
            m.rgb = {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
            const bool glass = name.size() > 10 && name.substr(10) == "_G";
            m.roughness = glass ? 0.05 : 0.7;
            m.opacity = glass ? 0.4 : 1.0;
            it = cache.emplace(name, m).first;
        }
        return it->second;
    }
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

std::vector<bool> ifcShellMask(const IfcModel& m, double cell) {
    const std::size_t n = m.elements.size();
    std::vector<bool> keep(n, false);
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (const auto& e : m.elements)
        for (const auto& p : e.parts)
            for (const auto& t : p.tris)
                for (const auto& q : t) { x0 = std::min(x0, q.x); y0 = std::min(y0, q.y); x1 = std::max(x1, q.x); y1 = std::max(y1, q.y); }
    if (x0 > x1) return keep;
    x0 -= 2 * cell; y0 -= 2 * cell; x1 += 2 * cell; y1 += 2 * cell;
    const int W = static_cast<int>(std::ceil((x1 - x0) / cell)) + 1, H = static_cast<int>(std::ceil((y1 - y0) / cell)) + 1;
    if (static_cast<long long>(W) * H > 40'000'000) { std::fill(keep.begin(), keep.end(), true); return keep; }   // juda katta hudud — filtrsiz
    auto drop = [&](const IfcElement& e) {
        const std::string c = ifcCategory(e.type);
        return c == "FURNITURE" || c == "MEP" || c == "SPACE" || c == "OPENING" || e.type == "IFCFURNISHINGELEMENT" || e.type.rfind("IFCFLOW", 0) == 0 ||
               e.type == "IFCDISTRIBUTIONFLOWELEMENT" || e.type == "IFCSPACE" || e.type == "IFCOPENINGELEMENT";
    };
    // qavatlar bo'yicha (qavatsiz elementlar — hammasi bilan)
    std::map<int, std::vector<std::size_t>> byStorey;
    for (std::size_t i = 0; i < n; ++i)
        if (!drop(m.elements[i])) byStorey[m.elements[i].storey].push_back(i);
    std::vector<unsigned char> g(static_cast<std::size_t>(W) * H);
    auto cellsOf = [&](const IfcElement& e, auto&& fn) {
        for (const auto& p : e.parts)
            for (const auto& t : p.tris) {
                const double ax = std::min({t[0].x, t[1].x, t[2].x}), bx = std::max({t[0].x, t[1].x, t[2].x});
                const double ay = std::min({t[0].y, t[1].y, t[2].y}), by = std::max({t[0].y, t[1].y, t[2].y});
                const int i0 = static_cast<int>((ax - x0) / cell), i1 = static_cast<int>((bx - x0) / cell);
                const int j0 = static_cast<int>((ay - y0) / cell), j1 = static_cast<int>((by - y0) / cell);
                // uzun qiya uchburchak (diagonal devor) qutisi butun hududni to'ldirmasin: katta qutini tashlab, uchlarini belgilash
                if ((i1 - i0) * (j1 - j0) > 400) {
                    for (const auto& q : t) fn(static_cast<int>((q.x - x0) / cell), static_cast<int>((q.y - y0) / cell));
                    continue;
                }
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i) fn(i, j);
            }
    };
    const std::vector<std::size_t>* all = nullptr;
    if (auto it = byStorey.find(-1); it != byStorey.end()) all = &it->second;
    for (const auto& [st, ids] : byStorey) {
        std::fill(g.begin(), g.end(), 0);   // 0 bo'sh, 1 band, 2 tashqi
        auto mark = [&](int i, int j) { if (i >= 0 && j >= 0 && i < W && j < H) g[static_cast<std::size_t>(j) * W + i] = 1; };
        for (std::size_t id : ids) cellsOf(m.elements[id], mark);
        if (all && st != -1)
            for (std::size_t id : *all) cellsOf(m.elements[id], mark);
        std::vector<int> q;
        for (int i = 0; i < W; ++i) { q.push_back(i); q.push_back((H - 1) * W + i); }
        for (int j = 0; j < H; ++j) { q.push_back(j * W); q.push_back(j * W + W - 1); }
        for (int k : q) g[static_cast<std::size_t>(k)] = g[static_cast<std::size_t>(k)] ? g[static_cast<std::size_t>(k)] : 2;
        for (std::size_t h = 0; h < q.size(); ++h) {
            const int k = q[h], i = k % W, j = k / W;
            if (g[static_cast<std::size_t>(k)] != 2) continue;
            for (auto [di, dj] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                const int a = i + di, b = j + dj;
                if (a < 0 || b < 0 || a >= W || b >= H) continue;
                auto& c = g[static_cast<std::size_t>(b) * W + a];
                if (c == 0) { c = 2; q.push_back(b * W + a); }
            }
        }
        for (std::size_t id : ids) {
            bool ext = false;
            cellsOf(m.elements[id], [&](int i, int j) {
                if (ext) return;
                for (int dj = -2; dj <= 2 && !ext; ++dj)
                    for (int di = -2; di <= 2 && !ext; ++di) {
                        const int a = i + di, b = j + dj;
                        if (a >= 0 && b >= 0 && a < W && b < H && g[static_cast<std::size_t>(b) * W + a] == 2) ext = true;
                    }
            });
            if (ext) keep[id] = true;
        }
    }
    return keep;
}

void addIfc(Scene& s, const IfcModel& m, const IfcExportOptions& o) {
    if (!o.shell && !o.colors) { addIfc(s, m); return; }
    std::vector<bool> keep = o.shell ? ifcShellMask(m) : std::vector<bool>(m.elements.size(), true);
    for (std::size_t i = 0; i < m.elements.size(); ++i) {
        if (!keep[i]) continue;
        const auto& e = m.elements[i];
        const std::string cat = ifcCategory(e.type);
        if (cat == "SPACE" || cat == "OPENING" || cat == "REBAR") continue;
        for (const auto& p : e.parts) {
            std::string mat;
            if (o.colors) {
                const std::uint32_t c = p.rgb ? p.rgb : ifcDefaultColor(e.type);
                mat = std::format("IFC_{:06X}", c & 0xffffff);
                if (p.alpha < 0.7 || cat == "WINDOW" && p.alpha < 0.95) mat += "_G";
            } else {
                mat = cat == "WINDOW" || cat == "PLATE" || cat == "CURTAINWALL" || p.alpha < 0.7 ? "AC_Shisha" : cat == "RAILING" || cat == "MEMBER" ? "AC_Metall" : "AC_Beton";
            }
            for (const auto& t : p.tris) s.add(mat, t);
        }
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

bool maxRendererFromKey(const std::string& key, MaxRenderer& out) {
    std::string k;
    for (char ch : key) k += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (k == "CORONA") out = MaxRenderer::Corona;
    else if (k == "VRAY" || k == "V-RAY") out = MaxRenderer::VRay;
    else if (k == "PHYSICAL" || k == "ART" || k == "ARNOLD") out = MaxRenderer::Physical;
    else return false;
    return true;
}

void writeMaxScript(const Scene& s, const std::string& msPath, const MaxScriptOptions& o) {
    std::ofstream f(msPath, std::ios::binary);
    if (!f) throw Error("export.err.write", {msPath});
    const bool corona = o.renderer == MaxRenderer::Corona, vray = o.renderer == MaxRenderer::VRay;
    // sahna chegaralari (origin ayirilgan)
    double x0 = 1e300, y0 = 1e300, z0 = 1e300, x1 = -1e300, y1 = -1e300, z1 = -1e300;
    for (const auto& mm : s.meshes)
        for (const auto& t : mm.tris)
            for (const auto& p : t) {
                x0 = std::min(x0, p.x - s.origin.x); x1 = std::max(x1, p.x - s.origin.x);
                y0 = std::min(y0, p.y - s.origin.y); y1 = std::max(y1, p.y - s.origin.y);
                z0 = std::min(z0, p.z - s.origin.z); z1 = std::max(z1, p.z - s.origin.z);
            }
    if (x0 > x1) { x0 = y0 = z0 = 0; x1 = y1 = z1 = 10; }
    const double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, span = std::max({x1 - x0, y1 - y0, 10.0});
    f << "-- AsiaCAD -> 3ds Max sahna skripti (" << (corona ? "Corona" : vray ? "V-Ray" : "Physical") << ")\n"
      << "-- Ishga tushirish: Scripting > Run Script, yoki 3dsmax.exe -U MAXScript " << std::filesystem::path(msPath).filename().string() << "\n"
      << "-- Birlik: metr. Boshlanish nuqtasi (asl koordinatalar): " << std::format("{:.3f} {:.3f} {:.3f}", s.origin.x, s.origin.y, s.origin.z) << "\n"
      << "(\n"   // butun skript bitta blokda — local o'zgaruvchilar
      << "units.SystemType = #Meters\nunits.DisplayType = #Metric\nunits.MetricType = #Meters\n"
      << "local dir = getFilenamePath (getThisScriptFilename())\n"
      << "fn acTree n = (local r = #(n); for c in n.children do join r (acTree c); r)\n";
    if (!o.objFile.empty())
        f << "importFile (dir + \"" << o.objFile << "\") #noPrompt using:ObjImp\n";
    // materiallar
    f << "fn acMtl nm r g b rough metal opac = (\n";
    if (corona)
        f << "  local m = CoronaPhysicalMtl name:nm\n  m.baseColor = color (r*255) (g*255) (b*255)\n  m.baseRoughness = rough\n"
             "  m.metalnessMode = (if metal > 0.5 then 1 else 0)\n"
             "  if opac < 0.99 do (m.refractionAmount = 1.0 - opac*0.5; m.thinAbsorption = true; m.baseIor = 1.52)\n  m\n";
    else if (vray)
        f << "  local m = VRayMtl name:nm\n  m.diffuse = color (r*255) (g*255) (b*255)\n  m.reflection = color 255 255 255\n"
             "  m.brdf_useRoughness = true\n  m.reflection_glossiness = rough\n  m.reflection_metalness = metal\n"
             "  if opac < 0.99 do (m.refraction = color 230 235 240; m.refraction_ior = 1.52; m.refraction_affectShadows = true)\n  m\n";
    else
        f << "  local m = PhysicalMaterial name:nm\n  m.base_color = color (r*255) (g*255) (b*255)\n  m.roughness = rough\n  m.metalness = metal\n"
             "  if opac < 0.99 do (m.transparency = 1.0 - opac; m.trans_ior = 1.52)\n  m\n";
    f << ")\nlocal acMats = Dictionary #string\n";
    for (const auto& mm : s.meshes) {
        const auto& m = sceneMaterial(mm.material);
        f << std::format("acMats[\"{}\"] = acMtl \"{}\" {:.3f} {:.3f} {:.3f} {:.2f} {:.1f} {:.2f}\n", mm.material, mm.material, m.rgb[0], m.rgb[1], m.rgb[2],
                         m.roughness, m.metallic, m.opacity);
    }
    // obyektlarga (OBJ: obyekt va material nomi bir xil) — nom bo'yicha, ko'p-material bo'lsa sub-materiallar bo'yicha
    f << "for o in geometry do (\n"
         "  local k = o.name\n"
         "  if hasDictValue acMats k then o.material = acMats[k]\n"
         "  else if o.material != undefined and classOf o.material == MultiMaterial then (\n"
         "    for i = 1 to o.material.numsubs do (local sm = o.material[i]; if sm != undefined and hasDictValue acMats sm.name do o.material[i] = acMats[sm.name])\n"
         "  ) else if o.material != undefined and hasDictValue acMats o.material.name do o.material = acMats[o.material.name]\n)\n";
    // quyosh va osmon
    const double az = o.sunAzimuthDeg * kPI / 180, el = std::max(o.sunElevationDeg, 2.0) * kPI / 180, R = span * 2;
    const double sx = cx + R * std::cos(el) * std::sin(az), sy = cy + R * std::cos(el) * std::cos(az), sz = R * std::sin(el);
    f << std::format("-- quyosh: azimut {:.1f} (shimoldan), balandlik {:.1f}\n", o.sunAzimuthDeg, o.sunElevationDeg);
    if (corona)
        f << std::format("local sun = CoronaSun name:\"AC_Quyosh\" pos:[{:.2f},{:.2f},{:.2f}]\n"
                         "sun.dir = normalize (sun.pos - [{:.2f},{:.2f},0])   -- yorug'lik -Z bo'ylab, nishonga\n"
                         "environmentMap = CoronaSky()\nuseEnvironmentMap = true\n", sx, sy, sz, cx, cy);
    else if (vray)
        f << std::format("local sun = VRaySun name:\"AC_Quyosh\" pos:[{:.2f},{:.2f},{:.2f}] target:(Targetobject pos:[{:.2f},{:.2f},0])\n"
                         "environmentMap = VRaySky()\nuseEnvironmentMap = true\n", sx, sy, sz, cx, cy);
    else
        f << std::format("local sun = Directionallight name:\"AC_Quyosh\" pos:[{:.2f},{:.2f},{:.2f}] multiplier:3.0 castShadows:true\n"
                         "sun.dir = normalize ([{:.2f},{:.2f},0] - sun.pos)\nenvironmentMap = Physical_Sun___Sky_Environment()\nuseEnvironmentMap = true\n",
                         sx, sy, sz, cx, cy);
    // kameralar: inson ko'zi (1,6 m) — sahna janubi-sharqidan, dron — 35° qiyalik
    const double ex = x1 + span * 0.05, ey = y0 - span * 0.05;
    f << std::format("local camEye = Physical name:\"AC_Kamera_Odam\" pos:[{:.2f},{:.2f},{:.2f}] target:(Targetobject pos:[{:.2f},{:.2f},{:.2f}])\n"
                     "camEye.focal_length_mm = 28\n", ex, ey, z0 + 1.6, cx, cy, z0 + 4.0);
    f << std::format("local camDron = Physical name:\"AC_Kamera_Dron\" pos:[{:.2f},{:.2f},{:.2f}] target:(Targetobject pos:[{:.2f},{:.2f},{:.2f}])\n"
                     "camDron.focal_length_mm = 35\n", cx + span * 0.6, cy - span * 0.6, z1 + span * 0.45, cx, cy, z0);
    // markerlar
    f << "-- markerlar: kind -> fayl (aktivlar jadvali: kind;fayl.max;balandlik)\nlocal assets = Dictionary #string\n";
    if (!o.assetsCsv.empty())
        f << "local af = dir + \"" << o.assetsCsv << "\"\n"
             "if doesFileExist af do (\n"
             "  local fs = openFile af\n"
             "  while not eof fs do (local ln = readLine fs; local p = filterString ln \";\"\n"
             "    if p.count >= 2 and p[1] != \"kind\" and p[1][1] != \"#\" and doesFileExist (dir + p[2]) do (\n"
             "      local before = objects as array\n"
             "      mergeMaxFile (dir + p[2]) #select #mergedups #useSceneMtlDups quiet:true\n"
             "      local src = for x in objects where findItem before x == 0 collect x\n"
             "      if src.count > 0 do (local g = group src name:(\"AC_AKTIV_\" + p[1]); hide (acTree g); assets[p[1]] = #(g, (if p.count >= 3 then (p[3] as float) else 0.0)))))\n"
             "  close fs)\n";
    f << "local markers = #(\n";
    for (std::size_t i = 0; i < s.markers.size(); ++i) {
        const auto& m = s.markers[i];
        f << std::format("  #(\"{}\",[{:.2f},{:.2f},{:.2f}],{:.2f},{:.1f}){}\n", m.kind, m.p.x - s.origin.x, m.p.y - s.origin.y, m.p.z - s.origin.z, m.height,
                         m.rotDeg, i + 1 < s.markers.size() ? "," : "");
    }
    f << ")\n"
         "for i = 1 to markers.count do (\n"
         "  local mk = markers[i]\n"
         "  if hasDictValue assets mk[1] then (\n"
         "    local a = assets[mk[1]]\n"
         "    local nn = #()\n"
         "    maxOps.cloneNodes (acTree a[1]) cloneType:#instance newNodes:&nn\n"
         "    unhide nn\n"
         "    local c = (for x in nn where x.parent == undefined or findItem nn x.parent == 0 collect x)[1]\n"
         "    c.pos = mk[2]\n"
         "    rotate c (angleaxis mk[4] [0,0,1])\n"
         "    if a[2] > 0 and mk[3] > 0 do (local k = mk[3] / a[2]; c.scale = [k,k,k])\n"
         "    c.name = \"AC_\" + mk[1] + \"_\" + (i as string)\n"
         "  ) else (\n"
         "    Point name:(\"AC_\" + mk[1] + \"_\" + (i as string) + \"_h\" + (mk[3] as string)) pos:mk[2] size:(if mk[3] > 0 then mk[3] else 1) box:true cross:false\n"
         "  )\n)\n";
    f << std::format("renderWidth = {}\nrenderHeight = {}\n", o.width, o.height);
    if (corona) f << "try (renderers.current = CoronaRenderer()) catch ()\n";
    else if (vray) f << "try (renderers.current = V_Ray_GPU() ) catch (try (renderers.current = V_Ray()) catch ())\n";
    f << "viewport.setCamera camDron\n"
         "-- avtomatik render (buyruq qatoridan):\n"
         "-- 3dsmaxcmd sahna.max -camera:AC_Kamera_Dron -o:render_dron.png\n"
         "format \"AsiaCAD: sahna tayyor (% marker)\\n\" markers.count\n"
         ")\n";
    if (!f) throw Error("export.err.write", {msPath});
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
