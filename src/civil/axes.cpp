// AsiaCAD — aks (koordinata o'qlari) chizish.
#include <asiacad/civil/axes.h>

#include <asiacad/core/dimension.h>
#include <asiacad/core/entities.h>

#include <cmath>
#include <format>
#include <stdexcept>

namespace asiacad::civil {

namespace {
constexpr double kPI = 3.14159265358979323846;
constexpr const char* kLayer = "AKS";
constexpr const char* kMark = "AKS_MARKA";
constexpr const char* kDim = "AKS_OLCHAM";

template <class T>
void put(Entities& out, std::unique_ptr<T> e, const char* layer) {
    e->setLayer(layer);
    out.push_back(std::move(e));
}
void bubble(Entities& out, const Vec3& c, double r, double h, const std::string& s) {
    put(out, std::make_unique<CircleEntity>(c, r), kMark);
    put(out, std::make_unique<TextEntity>(c, h, s, 0.0, TextHAlign::Center, TextVAlign::Middle), kMark);
}
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

std::string axisLetter(int i, bool latin) {
    if (!latin) return gridLetter(i);
    static const char* L[] = {"A", "B", "C", "D", "E", "F", "G", "H", "J", "K", "L", "M", "N", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    constexpr int n = 24;
    if (i < n) return L[i];
    return std::string(L[i % n]) + std::to_string(i / n);
}

Entities generateAxes(const AxesSpec& s) {
    Entities out;
    const double r = s.bubbleR, h = s.textH > 0 ? s.textH : 0.7 * r;
    const double dimH = std::max(0.25, h * 0.8);
    auto label = [&](bool numeric, int k) { return numeric ? std::to_string(s.numStart + k) : axisLetter(s.letStart + k, s.latin); };
    if (!s.radial) {
        if (s.dx.empty() && s.dy.empty()) throw std::invalid_argument("axes.err.empty");
        std::vector<double> X{0}, Y{0};
        for (double d : s.dx) X.push_back(X.back() + d);
        for (double d : s.dy) Y.push_back(Y.back() + d);
        const double c = std::cos(s.angleDeg * kPI / 180), sn = std::sin(s.angleDeg * kPI / 180);
        auto P = [&](double u, double v) { return Vec3{s.x0 + u * c - v * sn, s.y0 + u * sn + v * c, 0}; };
        const double u0 = X.front(), u1 = X.back(), v0 = Y.front(), v1 = Y.back();
        // bir yo'nalishdagi o'qlar chizilmasa (masalan, faqat X berilgan), boshqa yo'nalish uzunligi — 6 m
        const double vlo = s.dy.empty() ? -3 : v0, vhi = s.dy.empty() ? 3 : v1;
        const double ulo = s.dx.empty() ? -3 : u0, uhi = s.dx.empty() ? 3 : u1;
        const double e = s.ext;
        // X bo'yicha qatorlanuvchi o'qlar (vertikal chiziqlar)
        for (std::size_t i = 0; i < X.size() && !s.dx.empty(); ++i) {
                put(out, std::make_unique<LineEntity>(P(X[i], vlo - e), P(X[i], vhi + e)), kLayer);
                const std::string t = label(!s.swap, static_cast<int>(i));
                bubble(out, P(X[i], vlo - e - r), r, h, t);
                if (s.bothEnds) bubble(out, P(X[i], vhi + e + r), r, h, t);
        }
        for (std::size_t j = 0; j < Y.size() && !s.dy.empty(); ++j) {
            put(out, std::make_unique<LineEntity>(P(ulo - e, Y[j]), P(uhi + e, Y[j])), kLayer);
            const std::string t = label(s.swap, static_cast<int>(j));
            bubble(out, P(ulo - e - r, Y[j]), r, h, t);
            if (s.bothEnds) bubble(out, P(uhi + e + r, Y[j]), r, h, t);
        }
        if (s.dims) {
            // zanjir: markalar bilan o'q chizig'i uchi orasida; umumiy: undan tashqarida
            const double g1 = e * 0.45, g2 = e * 0.8;
            if (X.size() > 1) {
                for (std::size_t i = 0; i + 1 < X.size(); ++i)
                    put(out, std::make_unique<DimensionEntity>(DimKind::Aligned, P(X[i], vlo), P(X[i + 1], vlo), P(X[i], vlo - g1), dimH), kDim);
                if (X.size() > 2) put(out, std::make_unique<DimensionEntity>(DimKind::Aligned, P(u0, vlo), P(u1, vlo), P(u0, vlo - g2), dimH), kDim);
            }
            if (Y.size() > 1) {
                for (std::size_t j = 0; j + 1 < Y.size(); ++j)
                    put(out, std::make_unique<DimensionEntity>(DimKind::Aligned, P(ulo, Y[j]), P(ulo, Y[j + 1]), P(ulo - g1, Y[j]), dimH), kDim);
                if (Y.size() > 2) put(out, std::make_unique<DimensionEntity>(DimKind::Aligned, P(ulo, v0), P(ulo, v1), P(ulo - g2, v0), dimH), kDim);
            }
        }
        return out;
    }
    // ---- radial: radial o'qlar (raqamli) + yoy o'qlar (harfli)
    if (s.da.empty() && s.dr.empty()) throw std::invalid_argument("axes.err.empty");
    std::vector<double> A{s.a0}, R{s.r0};
    for (double d : s.da) A.push_back(A.back() + d);
    for (double d : s.dr) R.push_back(R.back() + d);
    const Vec3 O{s.x0, s.y0, 0};
    auto pol = [&](double rad, double deg) { return Vec3{O.x + rad * std::cos(deg * kPI / 180), O.y + rad * std::sin(deg * kPI / 180), 0}; };
    const double rin = std::max(0.0, R.front() - s.ext), rout = R.back() + s.ext;
    for (std::size_t i = 0; i < A.size(); ++i) {
        put(out, std::make_unique<LineEntity>(pol(rin, A[i]), pol(rout, A[i])), kLayer);
        const std::string t = label(!s.swap, static_cast<int>(i));
        bubble(out, pol(rout + r, A[i]), r, h, t);
        if (s.bothEnds && rin > 2 * r + 0.5) bubble(out, pol(rin - r, A[i]), r, h, t);
    }
    for (std::size_t j = 0; j < R.size(); ++j) {
        if (R[j] <= 1e-9) continue;
        const double ex = s.ext / R[j] * 180 / kPI;
        const double s0 = A.front() - ex, s1 = A.back() + ex;
        put(out, std::make_unique<ArcEntity>(O, R[j], s0 * kPI / 180, s1 * kPI / 180), kLayer);
        const std::string t = label(s.swap, static_cast<int>(j));
        const double eb = r / R[j] * 180 / kPI;
        bubble(out, pol(R[j], s0 - eb), r, h, t);
        if (s.bothEnds) bubble(out, pol(R[j], s1 + eb), r, h, t);
    }
    if (s.dims) {
        for (std::size_t j = 0; j + 1 < R.size(); ++j) {   // radius qadamlari — birinchi radial o'q bo'ylab
            const double off = -(r + s.ext * 0.4);
            const Vec3 n{std::cos((A.front() - 90) * kPI / 180), std::sin((A.front() - 90) * kPI / 180), 0};
            const Vec3 p1 = pol(R[j], A.front()), p2 = pol(R[j + 1], A.front());
            put(out, std::make_unique<DimensionEntity>(DimKind::Aligned, p1, p2, Vec3{p1.x - n.x * off, p1.y - n.y * off, 0}, dimH), kDim);
        }
        for (std::size_t i = 0; i + 1 < A.size(); ++i) {   // burchak qadamlari — yozuv
            const double am = (A[i] + A[i + 1]) / 2, rr = rout - s.ext * 0.4;
            const Vec3 q = pol(rr, am);
            put(out, std::make_unique<TextEntity>(q, dimH, std::format("{:g}\xC2\xB0", A[i + 1] - A[i]), (am - 90) * kPI / 180, TextHAlign::Center,
                                                  TextVAlign::Middle),
                kDim);
        }
    }
    return out;
}

AxesSpec parseAxesArgs(const std::vector<std::string>& args) {
    AxesSpec s;
    auto num = [](const std::string& v) {
        std::string t = v;
        for (char& ch : t) if (ch == ',') ch = '.';
        std::size_t k = 0;
        const double d = std::stod(t, &k);
        if (k != t.size()) throw std::invalid_argument("axes.err.value");
        return d;
    };
    auto pt = [&](const std::string& v, double& x, double& y) {
        const auto c = v.find(',');
        if (c == std::string::npos) throw std::invalid_argument("axes.err.value");
        x = num(v.substr(0, c));
        y = num(v.substr(c + 1));
    };
    auto list = [](const std::string& v) {
        auto r = parseSpacing(v);
        if (r.empty()) throw std::invalid_argument("axes.err.value");
        return r;
    };
    // "r0;d1,d2" — boshlang'ich qiymat ; qadamlar
    auto startList = [&](const std::string& v, double& st, std::vector<double>& steps) {
        const auto sc = v.find(';');
        if (sc == std::string::npos) { st = 0; steps = list(v); return; }
        st = num(v.substr(0, sc));
        if (sc + 1 < v.size()) steps = list(v.substr(sc + 1));
    };
    for (const auto& a : args) {
        const auto eq = a.find('=');
        const std::string k = upper(eq == std::string::npos ? a : a.substr(0, eq));
        const std::string v = eq == std::string::npos ? std::string() : a.substr(eq + 1);
        try {
            if (k == "X") s.dx = list(v);
            else if (k == "Y") s.dy = list(v);
            else if (k == "O" || k == "ORIGIN") pt(v, s.x0, s.y0);
            else if (k == "ANG" || k == "ANGLE") s.angleDeg = num(v);
            else if (k == "LET") {
                const std::string u = upper(v);
                if (u == "LAT" || u == "LATIN" || u == "A") s.latin = true;
                else if (u == "CYR" || u == "KIR" || u == "GOST") s.latin = false;
                else throw std::invalid_argument("axes.err.value");
            } else if (k == "SWAP") s.swap = true;
            else if (k == "START") {
                const auto c = v.find(',');
                s.numStart = static_cast<int>(num(c == std::string::npos ? v : v.substr(0, c)));
                if (c != std::string::npos) {
                    const std::string l = v.substr(c + 1);
                    int found = -1;
                    for (int i = 0; i < 200 && found < 0; ++i)
                        if (axisLetter(i, true) == upper(l) || axisLetter(i, false) == l) { found = i; s.latin = axisLetter(i, true) == upper(l); }
                    if (found < 0) throw std::invalid_argument("axes.err.value");
                    s.letStart = found;
                }
            } else if (k == "EXT") s.ext = num(v);
            else if (k == "R") s.bubbleR = num(v);
            else if (k == "H") s.textH = num(v);
            else if (k == "NODIM") s.dims = false;
            else if (k == "ONE") s.bothEnds = false;
            else if (k == "RAD") { s.radial = true; startList(v, s.r0, s.dr); }
            else if (k == "ANGS") { s.radial = true; startList(v, s.a0, s.da); }
            else throw std::invalid_argument("axes.err.arg");
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument(a);
        } catch (const std::out_of_range&) {
            throw std::invalid_argument(a);
        }
    }
    if (s.bubbleR <= 0 || s.ext < 0) throw std::invalid_argument("R/EXT");
    return s;
}

}  // namespace asiacad::civil
