// AsiaCAD — vektor PDF: TrueType qism-shrift (CIDFontType2, Identity-H), chiziqlar, to'ldirish, matn, kesish.
#include <asiacad/io/pdf.h>

#include <asiacad/core/dimension.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/sheet.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace asiacad::io {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kPt = 72.0 / 25.4;

// ------------------------------------------------------------------ TrueType
std::uint16_t u16(const std::string& d, std::size_t o) { return o + 2 <= d.size() ? static_cast<std::uint16_t>((static_cast<unsigned char>(d[o]) << 8) | static_cast<unsigned char>(d[o + 1])) : 0; }
std::int16_t s16(const std::string& d, std::size_t o) { return static_cast<std::int16_t>(u16(d, o)); }
std::uint32_t u32(const std::string& d, std::size_t o) { return (static_cast<std::uint32_t>(u16(d, o)) << 16) | u16(d, o + 2); }
void put16(std::string& d, std::uint16_t v) { d += static_cast<char>(v >> 8); d += static_cast<char>(v & 0xff); }
void put32(std::string& d, std::uint32_t v) { put16(d, static_cast<std::uint16_t>(v >> 16)); put16(d, static_cast<std::uint16_t>(v & 0xffff)); }

class TtfFont {
public:
    bool load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        d_ = ss.str();
        std::size_t base = 0;
        if (d_.compare(0, 4, "ttcf") == 0) base = u32(d_, 12);  // kolleksiya — birinchi shrift
        const std::uint32_t ver = u32(d_, base);
        if (ver != 0x00010000 && ver != 0x74727565) return false;  // CFF (OTTO) — qo'llab-quvvatlanmaydi
        const int n = u16(d_, base + 4);
        for (int i = 0; i < n; ++i) {
            const std::size_t r = base + 12 + static_cast<std::size_t>(i) * 16;
            tables_[d_.substr(r, 4)] = {u32(d_, r + 8), u32(d_, r + 12)};
        }
        for (const char* t : {"head", "hhea", "maxp", "hmtx", "loca", "glyf", "cmap"})
            if (!tables_.count(t)) return false;
        const auto head = tables_["head"].first;
        upem_ = u16(d_, head + 18);
        longLoca_ = s16(d_, head + 50) == 1;
        numGlyphs_ = u16(d_, tables_["maxp"].first + 4);
        numHMetrics_ = u16(d_, tables_["hhea"].first + 34);
        ascent_ = s16(d_, tables_["hhea"].first + 4);
        descent_ = s16(d_, tables_["hhea"].first + 6);
        capHeight_ = static_cast<int>(upem_ * 0.716);
        if (tables_.count("OS/2")) {
            const auto os2 = tables_["OS/2"].first;
            if (u16(d_, os2) >= 2 && s16(d_, os2 + 88) > 0) capHeight_ = s16(d_, os2 + 88);
        }
        bbox_[0] = s16(d_, head + 36); bbox_[1] = s16(d_, head + 38); bbox_[2] = s16(d_, head + 40); bbox_[3] = s16(d_, head + 42);
        if (!parseCmap()) return false;
        name_ = std::filesystem::path(path).stem().string();
        std::string clean;
        for (char c : name_) if (std::isalnum(static_cast<unsigned char>(c))) clean += c;
        name_ = clean.empty() ? "Font" : clean;
        return upem_ > 0;
    }
    std::uint16_t gid(char32_t c) const {
        auto it = cmap_.find(c);
        return it == cmap_.end() ? 0 : it->second;
    }
    int advance(std::uint16_t g) const {
        const auto h = tables_.at("hmtx").first;
        const std::uint16_t k = g < numHMetrics_ ? g : static_cast<std::uint16_t>(numHMetrics_ - 1);
        return u16(d_, h + static_cast<std::size_t>(k) * 4);
    }
    int upem() const { return upem_; }
    int capHeight() const { return capHeight_; }
    int ascent() const { return ascent_; }
    int descent() const { return descent_; }
    const int* bbox() const { return bbox_; }
    const std::string& name() const { return name_; }

    // Qism-shrift: ishlatilmagan glyflar bo'sh (gid raqamlari o'zgarmaydi)
    std::string subset(std::set<std::uint16_t> used) const {
        used.insert(0);
        const auto [glyfOff, glyfLen] = tables_.at("glyf");
        const auto locaOff = tables_.at("loca").first;
        auto loc = [&](std::uint32_t g) -> std::uint32_t { return longLoca_ ? u32(d_, locaOff + g * 4) : 2u * u16(d_, locaOff + g * 2); };
        // tarkibiy glyflar komponentlari
        std::vector<std::uint16_t> stack(used.begin(), used.end());
        while (!stack.empty()) {
            const std::uint16_t g = stack.back();
            stack.pop_back();
            if (g >= numGlyphs_) continue;
            const std::uint32_t a = loc(g), b = loc(g + 1u);
            if (b <= a) continue;
            std::size_t p = glyfOff + a;
            if (s16(d_, p) >= 0) continue;
            p += 10;
            for (int guard = 0; guard < 64; ++guard) {
                const std::uint16_t flags = u16(d_, p), cg = u16(d_, p + 2);
                if (used.insert(cg).second) stack.push_back(cg);
                p += 4 + ((flags & 1) ? 4 : 2);
                if (flags & 8) p += 2;
                else if (flags & 0x40) p += 4;
                else if (flags & 0x80) p += 8;
                if (!(flags & 0x20)) break;
            }
        }
        std::string glyf, loca;
        for (std::uint32_t g = 0; g < static_cast<std::uint32_t>(numGlyphs_); ++g) {
            put32(loca, static_cast<std::uint32_t>(glyf.size()));
            if (!used.count(static_cast<std::uint16_t>(g))) continue;
            const std::uint32_t a = loc(g), b = loc(g + 1u);
            if (b > a && glyfOff + b <= d_.size() && b - a <= glyfLen) glyf += d_.substr(glyfOff + a, b - a);
            while (glyf.size() % 4) glyf += '\0';
        }
        put32(loca, static_cast<std::uint32_t>(glyf.size()));
        std::map<std::string, std::string> out;
        auto raw = [&](const char* t) { const auto [o, l] = tables_.at(t); return d_.substr(o, l); };
        out["head"] = raw("head");
        out["head"][50] = 0; out["head"][51] = 1;                                // uzun loca
        for (int k = 8; k < 12; ++k) out["head"][static_cast<std::size_t>(k)] = 0;  // checkSumAdjustment
        out["hhea"] = raw("hhea");
        out["maxp"] = raw("maxp");
        out["hmtx"] = raw("hmtx");
        out["loca"] = loca;
        out["glyf"] = glyf;
        for (const char* t : {"cvt ", "fpgm", "prep"}) if (tables_.count(t)) out[t] = raw(t);
        std::string f;
        const auto n = static_cast<std::uint16_t>(out.size());
        int es = 0;
        while ((2 << es) <= n) ++es;
        put32(f, 0x00010000);
        put16(f, n);
        put16(f, static_cast<std::uint16_t>(16 << es));
        put16(f, static_cast<std::uint16_t>(es));
        put16(f, static_cast<std::uint16_t>(n * 16 - (16 << es)));
        std::size_t off = 12 + static_cast<std::size_t>(n) * 16;
        std::string body;
        for (auto& [tag, data] : out) {
            std::string padded = data;
            while (padded.size() % 4) padded += '\0';
            std::uint32_t sum = 0;
            for (std::size_t i = 0; i < padded.size(); i += 4) sum += u32(padded, i);
            f += tag;
            put32(f, sum);
            put32(f, static_cast<std::uint32_t>(off + body.size()));
            put32(f, static_cast<std::uint32_t>(data.size()));
            body += padded;
        }
        return f + body;
    }

private:
    bool parseCmap() {
        const auto c = tables_["cmap"].first;
        const int n = u16(d_, c + 2);
        std::size_t best = 0;
        int bestFmt = 0;
        for (int i = 0; i < n; ++i) {
            const std::size_t r = c + 4 + static_cast<std::size_t>(i) * 8;
            const int pid = u16(d_, r), eid = u16(d_, r + 2);
            const std::size_t sub = c + u32(d_, r + 4);
            const int fmt = u16(d_, sub);
            if (pid == 3 && eid == 10 && fmt == 12) { best = sub; bestFmt = 12; }
            else if (((pid == 3 && eid == 1) || pid == 0) && fmt == 4 && bestFmt != 12) { best = sub; bestFmt = 4; }
        }
        if (bestFmt == 12) {
            const std::uint32_t groups = u32(d_, best + 12);
            for (std::uint32_t g = 0; g < groups; ++g) {
                const std::size_t r = best + 16 + g * 12;
                const std::uint32_t s = u32(d_, r), e = u32(d_, r + 4), id = u32(d_, r + 8);
                for (std::uint32_t ch = s; ch <= e && ch - s < 70000; ++ch) cmap_[ch] = static_cast<std::uint16_t>(id + (ch - s));
            }
            return true;
        }
        if (bestFmt == 4) {
            const int segX2 = u16(d_, best + 6);
            const std::size_t ends = best + 14, starts = ends + segX2 + 2, deltas = starts + segX2, ranges = deltas + segX2;
            for (int i = 0; i < segX2 / 2; ++i) {
                const std::uint16_t e = u16(d_, ends + i * 2), s = u16(d_, starts + i * 2);
                const std::int16_t dl = s16(d_, deltas + i * 2);
                const std::uint16_t ro = u16(d_, ranges + i * 2);
                for (std::uint32_t ch = s; ch <= e && ch != 0xFFFF; ++ch) {
                    std::uint16_t g;
                    if (ro == 0) g = static_cast<std::uint16_t>(ch + dl);
                    else {
                        const std::size_t ga = ranges + i * 2 + ro + (ch - s) * 2;
                        g = u16(d_, ga);
                        if (g) g = static_cast<std::uint16_t>(g + dl);
                    }
                    if (g) cmap_[ch] = g;
                }
            }
            return true;
        }
        return false;
    }
    std::string d_;
    std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> tables_;
    std::map<char32_t, std::uint16_t> cmap_;
    int upem_ = 1000, numGlyphs_ = 0, numHMetrics_ = 0, ascent_ = 800, descent_ = -200, capHeight_ = 716;
    int bbox_[4] = {0, -200, 1000, 800};
    bool longLoca_ = false;
    std::string name_;
};

// ------------------------------------------------------------------ PDF hujjat
struct Pdf {
    std::vector<std::string> pages;  // har sahifa mazmuni (mm koordinatalarda)
    std::vector<std::pair<double, double>> sizes;
    std::string cur;
    TtfFont font;
    bool ttf = false;
    std::set<std::uint16_t> used;
    std::map<std::uint16_t, char32_t> uni;
    // holat
    std::uint32_t rgbStroke = 0xFFFFFFFF, rgbFill = 0xFFFFFFFF;
    double lw = -1;

    static std::string num(double v) {
        if (std::abs(v) < 1e-7) return "0";
        std::string s = std::format("{:.4f}", v);
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
        return s;
    }
    void begin(double w, double h) {
        cur.clear();
        sizes.emplace_back(w, h);
        rgbStroke = rgbFill = 0xFFFFFFFF;
        lw = -1;
        cur += num(kPt) + " 0 0 " + num(kPt) + " 0 0 cm 1 J 1 j\n";
    }
    void end() { pages.push_back(cur); }
    static std::string rgbStr(std::uint32_t c) {
        return num(((c >> 16) & 255) / 255.0) + " " + num(((c >> 8) & 255) / 255.0) + " " + num((c & 255) / 255.0);
    }
    void stroke(std::uint32_t c, double w) {
        if (c != rgbStroke) { cur += rgbStr(c) + " RG\n"; rgbStroke = c; }
        if (std::abs(w - lw) > 1e-9) { cur += num(w) + " w\n"; lw = w; }
    }
    void fillc(std::uint32_t c) { if (c != rgbFill) { cur += rgbStr(c) + " rg\n"; rgbFill = c; } }
    void polyline(const std::vector<std::pair<double, double>>& p, bool closed) {
        if (p.size() < 2) return;
        cur += num(p[0].first) + " " + num(p[0].second) + " m\n";
        for (std::size_t i = 1; i < p.size(); ++i) cur += num(p[i].first) + " " + num(p[i].second) + " l\n";
        cur += closed ? "s\n" : "S\n";
    }
    void fillPaths(const std::vector<std::vector<std::pair<double, double>>>& loops) {
        bool any = false;
        for (auto& p : loops) {
            if (p.size() < 3) continue;
            any = true;
            cur += num(p[0].first) + " " + num(p[0].second) + " m\n";
            for (std::size_t i = 1; i < p.size(); ++i) cur += num(p[i].first) + " " + num(p[i].second) + " l\n";
            cur += "h\n";
        }
        if (any) cur += "f*\n";
    }
    void clipRect(double x0, double y0, double x1, double y1) {
        cur += "q " + num(x0) + " " + num(y0) + " " + num(x1 - x0) + " " + num(y1 - y0) + " re W n\n";
        rgbStroke = rgbFill = 0xFFFFFFFF;
        lw = -1;
    }
    void unclip() { cur += "Q\n"; rgbStroke = rgbFill = 0xFFFFFFFF; lw = -1; }

    // Matn kengligi (em ulushida)
    double widthEm(const std::u32string& s) {
        double w = 0;
        for (char32_t c : s) w += ttf ? static_cast<double>(font.advance(font.gid(c))) / font.upem() : (c == ' ' ? 0.278 : 0.556);
        return w;
    }
    double capRatio() const { return ttf ? static_cast<double>(font.capHeight()) / font.upem() : 0.718; }
    double descRatio() const { return ttf ? -static_cast<double>(font.descent()) / font.upem() : 0.21; }
    // h — bosh harf balandligi (CAD matn balandligi), ha: 0 chap 1 markaz 2 o'ng; va: 0 tayanch 1 past 2 o'rta 3 yuqori
    void text(const std::string& utf8, double x, double y, double h, double rot, int ha, int va, std::uint32_t c) {
        const std::u32string s = utf8::decode(utf8);
        if (s.empty() || h <= 0) return;
        const double size = h / capRatio();
        const double w = widthEm(s) * size;
        double dx = ha == 1 ? -w / 2 : ha == 2 ? -w : 0;
        double dy = va == 1 ? descRatio() * size : va == 2 ? -h / 2 : va == 3 ? -h : 0;
        const double cs = std::cos(rot), sn = std::sin(rot);
        const double ox = x + dx * cs - dy * sn, oy = y + dx * sn + dy * cs;
        fillc(c);
        std::string str;
        if (ttf) {
            str = "<";
            for (char32_t ch : s) {
                const std::uint16_t g = font.gid(ch);
                used.insert(g);
                if (g) uni.emplace(g, ch);
                str += std::format("{:04X}", g);
            }
            str += ">";
        } else {
            str = "(";
            for (char32_t ch : s) {
                char b = ch < 128 ? static_cast<char>(ch) : (ch >= 0xA0 && ch <= 0xFF ? static_cast<char>(ch) : '?');
                if (b == '(' || b == ')' || b == '\\') str += '\\';
                str += b;
            }
            str += ")";
        }
        cur += "BT /F1 " + num(size) + " Tf " + num(cs) + " " + num(sn) + " " + num(-sn) + " " + num(cs) + " " + num(ox) + " " + num(oy) + " Tm " + str + " Tj ET\n";
    }

    bool write(const std::string& path, const std::string& title) {
        std::vector<std::string> objs;  // 1 dan raqamlangan
        auto add = [&](std::string body) { objs.push_back(std::move(body)); return static_cast<int>(objs.size()); };
        // shrift
        int fontObj;
        if (ttf) {
            const std::string sub = font.subset(used);
            const std::string tag = "AQSUBS+" + font.name();
            const int ff = add(std::format("<< /Length {} /Length1 {} >>\nstream\n", sub.size(), sub.size()) + sub + "\nendstream");
            const double k = 1000.0 / font.upem();
            const int* bb = font.bbox();
            const int desc = add(std::format("<< /Type /FontDescriptor /FontName /{} /Flags 32 /FontBBox [{} {} {} {}] /ItalicAngle 0 /Ascent {} /Descent {} "
                                             "/CapHeight {} /StemV 80 /FontFile2 {} 0 R >>",
                                             tag, std::lround(bb[0] * k), std::lround(bb[1] * k), std::lround(bb[2] * k), std::lround(bb[3] * k),
                                             std::lround(font.ascent() * k), std::lround(font.descent() * k), std::lround(font.capHeight() * k), ff));
            std::string w = "[";
            for (auto g : used) w += std::format("{} [{}] ", g, std::lround(font.advance(g) * k));
            w += "]";
            const int cid = add("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" + tag +
                                " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor " + std::to_string(desc) +
                                " 0 R /W " + w + " /CIDToGIDMap /Identity >>");
            std::string cm = "/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def "
                             "/CMapName /Adobe-Identity-UCS def /CMapType 2 def 1 begincodespacerange <0000> <FFFF> endcodespacerange\n";
            std::vector<std::pair<std::uint16_t, char32_t>> m(uni.begin(), uni.end());
            for (std::size_t i = 0; i < m.size(); i += 100) {
                const std::size_t n = std::min<std::size_t>(100, m.size() - i);
                cm += std::to_string(n) + " beginbfchar\n";
                for (std::size_t j = i; j < i + n; ++j) {
                    char32_t u = m[j].second;
                    std::string hex;
                    if (u > 0xFFFF) {
                        u -= 0x10000;
                        hex = std::format("{:04X}{:04X}", 0xD800 + (u >> 10), 0xDC00 + (u & 0x3FF));
                    } else hex = std::format("{:04X}", static_cast<unsigned>(u));
                    cm += std::format("<{:04X}> <{}>\n", m[j].first, hex);
                }
                cm += "endbfchar\n";
            }
            cm += "endcmap CMapName currentdict /CMap defineresource pop end end";
            const int tu = add(std::format("<< /Length {} >>\nstream\n", cm.size()) + cm + "\nendstream");
            fontObj = add("<< /Type /Font /Subtype /Type0 /BaseFont /" + tag + " /Encoding /Identity-H /DescendantFonts [" + std::to_string(cid) +
                          " 0 R] /ToUnicode " + std::to_string(tu) + " 0 R >>");
        } else {
            fontObj = add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
        }
        const int pagesObj = static_cast<int>(objs.size()) + 1 + static_cast<int>(pages.size()) * 2;
        std::vector<int> kids;
        for (std::size_t i = 0; i < pages.size(); ++i) {
            const int content = add(std::format("<< /Length {} >>\nstream\n", pages[i].size()) + pages[i] + "endstream");
            kids.push_back(add(std::format("<< /Type /Page /Parent {} 0 R /MediaBox [0 0 {} {}] /Contents {} 0 R /Resources << /Font << /F1 {} 0 R >> >> >>",
                                           pagesObj, num(sizes[i].first * kPt), num(sizes[i].second * kPt), content, fontObj)));
        }
        std::string k;
        for (int id : kids) k += std::to_string(id) + " 0 R ";
        add(std::format("<< /Type /Pages /Kids [{}] /Count {} >>", k, kids.size()));
        const int catalog = add(std::format("<< /Type /Catalog /Pages {} 0 R >>", pagesObj));
        std::string t;
        for (char ch : title) if (ch != '(' && ch != ')' && ch != '\\' && static_cast<unsigned char>(ch) < 128) t += ch;
        const int info = add("<< /Producer (AsiaCAD) /Creator (AsiaCAD PLOT) /Title (" + t + ") >>");
        std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
        std::vector<std::size_t> offs;
        for (std::size_t i = 0; i < objs.size(); ++i) {
            offs.push_back(out.size());
            out += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
        }
        const std::size_t xref = out.size();
        out += std::format("xref\n0 {}\n0000000000 65535 f \n", objs.size() + 1);
        for (auto o : offs) out += std::format("{:010} 00000 n \n", o);
        out += std::format("trailer\n<< /Size {} /Root {} 0 R /Info {} 0 R >>\nstartxref\n{}\n%%EOF\n", objs.size() + 1, catalog, info, xref);
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        f << out;
        return static_cast<bool>(f);
    }
};

// ------------------------------------------------------------------ chizma -> PDF
std::uint32_t aci(int i) {
    static const std::uint32_t basic[] = {0x000000, 0xff0000, 0xffff00, 0x00ff00, 0x00ffff, 0x0000ff, 0xff00ff, 0x000000, 0x808080, 0xc0c0c0};
    if (i <= 0 || i > 255 || i == 7) return 0x000000;
    if (i < 10) return basic[i];
    if (i >= 250) { const auto g = static_cast<std::uint32_t>(51 + (i - 250) * 40); return (g << 16) | (g << 8) | g; }
    const int hueIdx = (i - 10) / 10, var = i % 10;
    static const double val[] = {1.0, 1.0, 0.8, 0.8, 0.6, 0.6, 0.5, 0.5, 0.3, 0.3};
    const double sat = (var % 2 == 0) ? 1.0 : 0.5, v = val[var], h = hueIdx * 15.0 / 60.0;
    const double c = v * sat, x = c * (1 - std::abs(std::fmod(h, 2.0) - 1)), m = v - c;
    double r = 0, g = 0, b = 0;
    if (h < 1) { r = c; g = x; } else if (h < 2) { r = x; g = c; } else if (h < 3) { g = c; b = x; }
    else if (h < 4) { g = x; b = c; } else if (h < 5) { r = x; b = c; } else { r = c; b = x; }
    auto q = [&](double t) { return static_cast<std::uint32_t>(std::clamp((t + m) * 255.0, 0.0, 255.0)); };
    std::uint32_t rgb = (q(r) << 16) | (q(g) << 8) | q(b);
    // oq qog'ozda ko'rinmaydigan juda och ranglar to'qlashtiriladi (sariq, och ko'k)
    const double lum = 0.299 * ((rgb >> 16) & 255) + 0.587 * ((rgb >> 8) & 255) + 0.114 * (rgb & 255);
    if (lum > 200) rgb = ((((rgb >> 16) & 255) * 7 / 10) << 16) | ((((rgb >> 8) & 255) * 7 / 10) << 8) | ((rgb & 255) * 7 / 10);
    return rgb;
}

struct Painter {
    Pdf& pdf;
    const Document& doc;
    double k;        // model birligi -> mm
    Vec3 mc;         // model markazi
    double px, py;   // qog'ozdagi markaz (mm)
    bool mono;
    double lw;
    std::pair<double, double> P(const Vec3& v) const { return {px + (v.x - mc.x) * k, py + (v.y - mc.y) * k}; }
    std::uint32_t colorOf(const IEntity& e) const {
        if (mono) return 0;
        int c = e.color();
        if (c == kColorByLayer || c == kColorByBlock) {
            auto l = doc.layer(e.layer());
            c = l ? l->color : 7;
        }
        return aci(std::abs(c));
    }
    bool visible(const IEntity& e) const {
        auto l = doc.layer(e.layer());
        return !l || (l->visible && !l->frozen);
    }
    std::vector<std::pair<double, double>> sample(const ICurve& c, bool& closed) const {
        std::vector<std::pair<double, double>> out;
        closed = c.isClosed();
        if (auto* l = dynamic_cast<const LineEntity*>(&c)) return {P(l->startPoint()), P(l->endPoint())};
        if (auto* p = dynamic_cast<const PolylineEntity*>(&c)) {
            bool straight = true;
            for (double b : p->bulges()) if (std::abs(b) > 1e-12) straight = false;
            if (straight) { for (auto& v : p->vertices()) out.push_back(P(v)); return out; }
        }
        const double L = c.length();
        if (L <= 0) return out;
        // qog'ozda ~0.2 mm qadam, 8..2000 nuqta
        const int n = std::clamp(static_cast<int>(L * k / 0.2), 8, 2000);
        for (int i = 0; i <= n; ++i) {
            if (closed && i == n) break;
            out.push_back(P(c.pointAtDistance(L * i / n)));
        }
        return out;
    }
    void hatch(const HatchEntity& h, std::uint32_t col) {
        std::vector<std::vector<std::pair<double, double>>> loops;
        for (auto& l : h.loops()) { bool cl; loops.push_back(sample(l, cl)); }
        pdf.fillc(col);
        if (h.isSolid()) { pdf.fillPaths(loops); return; }
        pdf.stroke(col, lw * 0.5);
        for (auto& l : loops) if (l.size() > 1) pdf.polyline(l, true);
        // naqsh: burchakli parallel chiziqlar (ANSI31 kabi), qadam 3.175·masshtab model birligida
        const double ang = (h.patternAngle() + (h.pattern() == "ANSI31" || h.pattern().empty() ? 45.0 : 45.0)) * kPi / 180.0;
        double step = std::max(1e-9, 3.175 * h.patternScale() * k);  // mm qog'ozda
        if (step < 0.4) step = 0.4;
        const double ux = std::cos(ang), uy = std::sin(ang), nx = -uy, ny = ux;
        double tmin = 1e300, tmax = -1e300;
        for (auto& l : loops) for (auto& p : l) { const double t = p.first * nx + p.second * ny; tmin = std::min(tmin, t); tmax = std::max(tmax, t); }
        if (tmax <= tmin || (tmax - tmin) / step > 5000) return;
        for (double t = std::ceil(tmin / step) * step; t <= tmax; t += step) {
            std::vector<double> xs;  // chiziq bo'ylab parametr
            for (auto& l : loops)
                for (std::size_t i = 0; i < l.size(); ++i) {
                    const auto& a = l[i];
                    const auto& b = l[(i + 1) % l.size()];
                    const double ta = a.first * nx + a.second * ny - t, tb = b.first * nx + b.second * ny - t;
                    if ((ta > 0) == (tb > 0)) continue;
                    const double f = ta / (ta - tb);
                    const double x = a.first + (b.first - a.first) * f, y = a.second + (b.second - a.second) * f;
                    xs.push_back(x * ux + y * uy);
                }
            std::sort(xs.begin(), xs.end());
            for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
                const double s0 = xs[i], s1 = xs[i + 1];
                pdf.polyline({{s0 * ux + t * nx, s0 * uy + t * ny}, {s1 * ux + t * nx, s1 * uy + t * ny}}, false);
            }
        }
    }
    void draw(const IEntity& e, int depth = 0) {
        if (depth > 16 || !visible(e)) return;
        const std::uint32_t col = colorOf(e);
        switch (e.type()) {
        case EntityType::Insert:
            for (auto& c : static_cast<const InsertEntity&>(e).explode()) draw(*c, depth + 1);
            return;
        case EntityType::Dimension:
            for (auto& c : static_cast<const DimensionEntity&>(e).explode()) {
                c->setLayer(e.layer());
                c->setColor(e.color());
                draw(*c, depth + 1);
            }
            return;
        case EntityType::Hatch: hatch(static_cast<const HatchEntity&>(e), col); return;
        case EntityType::Text: {
            auto& t = static_cast<const TextEntity&>(e);
            const auto p = P(t.insertion());
            const int ha = t.hAlign() == TextHAlign::Center ? 1 : t.hAlign() == TextHAlign::Right ? 2 : 0;
            const int va = t.vAlign() == TextVAlign::Bottom ? 1 : t.vAlign() == TextVAlign::Middle ? 2 : t.vAlign() == TextVAlign::Top ? 3 : 0;
            if (t.height() * k >= 0.3) pdf.text(t.text(), p.first, p.second, t.height() * k, t.rotation(), ha, va, col);
            return;
        }
        case EntityType::Point: {
            const auto p = P(static_cast<const PointEntity&>(e).position());
            pdf.fillc(col);
            pdf.fillPaths({{{p.first - 0.25, p.second - 0.25}, {p.first + 0.25, p.second - 0.25}, {p.first + 0.25, p.second + 0.25}, {p.first - 0.25, p.second + 0.25}}});
            return;
        }
        case EntityType::Mesh: {
            auto& m = static_cast<const MeshEntity&>(e);
            pdf.stroke(col, lw * 0.5);
            for (auto& f : m.faces()) {
                std::vector<std::pair<double, double>> pts;
                for (int id : {f.a, f.b, f.c, f.d}) if (id >= 0 && id < static_cast<int>(m.vertices().size())) pts.push_back(P(m.vertices()[static_cast<std::size_t>(id)]));
                pdf.polyline(pts, true);
            }
            return;
        }
        default: break;
        }
        if (auto* c = dynamic_cast<const ICurve*>(&e)) {
            bool closed = false;
            auto pts = sample(*c, closed);
            pdf.stroke(col, lw);
            pdf.polyline(pts, closed);
        }
    }
    void drawAll(const BBox& clip) {
        doc.forEach([&](const IEntity& e) {
            const BBox b = e.bbox();
            if (b.valid() && clip.valid() && (b.max.x < clip.min.x || b.min.x > clip.max.x || b.max.y < clip.min.y || b.min.y > clip.max.y)) return;
            draw(e);
        });
    }
};

bool setupFont(Pdf& pdf, const PlotOptions& o, PlotResult& r) {
    const std::string f = o.font.empty() ? findSystemFont() : o.font;
    if (!f.empty() && pdf.font.load(f)) { pdf.ttf = true; r.fontUsed = f; return true; }
    if (!o.font.empty()) { r.error = "font: " + o.font; return false; }
    return true;
}
}  // namespace

std::string findSystemFont() {
    const char* cands[] = {"C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/calibri.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                           "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                           "/usr/share/fonts/truetype/freefont/FreeSans.ttf", "/Library/Fonts/Arial.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf"};
    if (const char* w = std::getenv("WINDIR")) {
        const std::string p = std::string(w) + "/Fonts/arial.ttf";
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }
    for (const char* c : cands) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(c, ec)) return c;
    }
    return {};
}

PlotResult plotModelPdf(const Document& doc, const std::string& path, const PlotOptions& o) {
    PlotResult r;
    double W = 420, H = 297;
    if (!paperSize(o.paper, o.landscape, W, H)) { r.error = "paper: " + o.paper; return r; }
    BBox win;
    if (o.hasWindow) { win.expand(o.w0); win.expand(o.w1); }
    else win = doc.extents();
    if (!win.valid() || ((win.max.x - win.min.x) <= 0 && (win.max.y - win.min.y) <= 0)) { r.error = "empty"; return r; }
    const double mw = std::max(1e-9, win.max.x - win.min.x), mh = std::max(1e-9, win.max.y - win.min.y);
    const double aw = W - 2 * o.margin, ah = H - 2 * o.margin;
    double k;  // model birligi -> mm
    if (o.scale > 0) { k = o.unitMm / o.scale; r.scale = o.scale; }
    else { k = std::min(aw / mw, ah / mh); r.scale = o.unitMm / k; }
    Pdf pdf;
    if (!setupFont(pdf, o, r)) return r;
    pdf.begin(W, H);
    pdf.clipRect(o.margin, o.margin, W - o.margin, H - o.margin);
    Painter p{pdf, doc, k, (win.min + win.max) * 0.5, W / 2, H / 2, o.mono, o.lineWidth};
    BBox clip;  // ko'rinadigan model oralig'i
    clip.expand({p.mc.x - aw / 2 / k, p.mc.y - ah / 2 / k, 0});
    clip.expand({p.mc.x + aw / 2 / k, p.mc.y + ah / 2 / k, 0});
    p.drawAll(clip);
    pdf.unclip();
    pdf.end();
    if (!pdf.write(path, o.title)) { r.error = "write: " + path; return r; }
    r.ok = true;
    r.pages = 1;
    return r;
}

PlotResult plotSheetsPdf(const Document& doc, const std::string& path, const std::vector<int>& which, const PlotOptions& o,
                         const std::function<std::string(const std::string&)>& tr) {
    PlotResult r;
    const auto& ss = doc.sheets();
    std::vector<int> idx = which;
    if (idx.empty()) for (int i = 0; i < static_cast<int>(ss.size()); ++i) idx.push_back(i);
    if (idx.empty()) { r.error = "no sheets"; return r; }
    Pdf pdf;
    if (!setupFont(pdf, o, r)) return r;
    for (int i : idx) {
        if (i < 0 || i >= static_cast<int>(ss.size())) continue;
        const Sheet& s = ss[static_cast<std::size_t>(i)];
        double W = 420, H = 297;
        paperSize(s.size, s.landscape, W, H);
        pdf.begin(W, H);
        for (const auto& v : s.views) {
            const PaperRect pr = viewRect(v);
            pdf.clipRect(pr.x0, pr.y0, pr.x1, pr.y1);
            // model birligi — metr: 1 m = 1000/scale mm
            Painter p{pdf, doc, 1000.0 / v.scale, v.center, v.at.x, v.at.y, o.mono, o.lineWidth};
            BBox clip;
            clip.expand({v.center.x - v.modelW() / 2, v.center.y - v.modelH() / 2, 0});
            clip.expand({v.center.x + v.modelW() / 2, v.center.y + v.modelH() / 2, 0});
            p.drawAll(clip);
            pdf.unclip();
        }
        for (const auto& pr : sheetPrims(s, i + 1, static_cast<int>(ss.size()), tr)) {
            if (pr.kind == SheetPrim::Line) {
                pdf.stroke(0, pr.width);
                pdf.polyline({{pr.a.x, pr.a.y}, {pr.b.x, pr.b.y}}, false);
            } else {
                pdf.text(pr.text, pr.a.x, pr.a.y, pr.h, 0, pr.align, 2, 0);
            }
        }
        pdf.end();
        ++r.pages;
    }
    if (!pdf.write(path, o.title)) { r.error = "write: " + path; return r; }
    r.ok = true;
    return r;
}

}  // namespace asiacad::io
