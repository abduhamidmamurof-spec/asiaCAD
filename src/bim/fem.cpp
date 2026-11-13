// AsiaCAD — 3D sterjenli FEM: lentali (banded) Xolesskiy, subfazoviy iteratsiya, CQC.
#include <asiacad/bim/fem.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace asiacad::bim::fem {

int Model::index(int nodeId) const {
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].id == nodeId) return static_cast<int>(i);
    return -1;
}

Model parseModel(const std::string& text) {
    Model m;
    std::istringstream in(text);
    std::string line;
    int ln = 0;
    while (std::getline(in, line)) {
        ++ln;
        if (auto c = line.find('#'); c != std::string::npos) line.erase(c);
        for (char& ch : line) if (ch == ',') ch = ' ';
        std::istringstream ls(line);
        std::string k;
        if (!(ls >> k)) continue;
        auto bad = [&] { return std::runtime_error("fem: line " + std::to_string(ln)); };
        if (k == "NODE") {
            Node n;
            if (!(ls >> n.id >> n.p.x >> n.p.y >> n.p.z)) throw bad();
            std::string t;
            while (ls >> t)
                if (t.rfind("FIX=", 0) == 0 && t.size() == 10)
                    for (int i = 0; i < 6; ++i) n.fix[static_cast<std::size_t>(i)] = t[4 + static_cast<std::size_t>(i)] == '1';
                else throw bad();
            m.nodes.push_back(n);
        } else if (k == "MAT") {
            int id; Material mt;
            if (!(ls >> id >> mt.E >> mt.G)) throw bad();
            m.mats[id] = mt;
        } else if (k == "SEC") {
            int id; Section s;
            if (!(ls >> id >> s.A >> s.Iy >> s.Iz >> s.J)) throw bad();
            m.secs[id] = s;
        } else if (k == "BEAM") {
            Beam b;
            if (!(ls >> b.id >> b.n1 >> b.n2 >> b.mat >> b.sec)) throw bad();
            ls >> b.vz.x >> b.vz.y >> b.vz.z;
            m.beams.push_back(b);
        } else if (k == "MASS") {
            int n; double v;
            if (!(ls >> n >> v)) throw bad();
            m.mass[n] += v;
        } else if (k == "LOAD") {
            int n;
            std::array<double, 6> f{};
            if (!(ls >> n >> f[0] >> f[1] >> f[2])) throw bad();
            ls >> f[3] >> f[4] >> f[5];
            auto& L = m.loads[n];
            for (int i = 0; i < 6; ++i) L[static_cast<std::size_t>(i)] += f[static_cast<std::size_t>(i)];
        } else throw bad();
    }
    for (const auto& b : m.beams)
        if (m.index(b.n1) < 0 || m.index(b.n2) < 0 || !m.mats.count(b.mat) || !m.secs.count(b.sec)) throw std::runtime_error("fem: beam " + std::to_string(b.id));
    return m;
}

std::string writeModel(const Model& m) {
    std::ostringstream o;
    o.precision(10);
    for (const auto& n : m.nodes) {
        o << "NODE " << n.id << ' ' << n.p.x << ' ' << n.p.y << ' ' << n.p.z;
        if (std::any_of(n.fix.begin(), n.fix.end(), [](bool b) { return b; })) {
            o << " FIX=";
            for (bool b : n.fix) o << (b ? '1' : '0');
        }
        o << '\n';
    }
    for (const auto& [id, mt] : m.mats) o << "MAT " << id << ' ' << mt.E << ' ' << mt.G << '\n';
    for (const auto& [id, s] : m.secs) o << "SEC " << id << ' ' << s.A << ' ' << s.Iy << ' ' << s.Iz << ' ' << s.J << '\n';
    for (const auto& b : m.beams) o << "BEAM " << b.id << ' ' << b.n1 << ' ' << b.n2 << ' ' << b.mat << ' ' << b.sec << '\n';
    for (const auto& [n, v] : m.mass) o << "MASS " << n << ' ' << v << '\n';
    for (const auto& [n, f] : m.loads) o << "LOAD " << n << ' ' << f[0] << ' ' << f[1] << ' ' << f[2] << ' ' << f[3] << ' ' << f[4] << ' ' << f[5] << '\n';
    return o.str();
}

namespace {
using Mat12 = std::array<std::array<double, 12>, 12>;
using Mat3 = std::array<std::array<double, 3>, 3>;

Mat3 axes(const Vec3& a, const Vec3& b, const Vec3& vzRef, double& L) {
    Vec3 x = b - a;
    L = x.length();
    if (L < 1e-9) throw std::runtime_error("fem: zero-length beam");
    x = x * (1.0 / L);
    Vec3 ref = vzRef;
    if (ref.length() < 1e-9) ref = std::abs(x.z) > 0.95 ? Vec3{1, 0, 0} : Vec3{0, 0, 1};   // ustun — global X, rigel — global Z
    Vec3 z = ref - x * ref.dot(x);
    if (z.length() < 1e-9) z = (std::abs(x.z) > 0.95 ? Vec3{0, 1, 0} : Vec3{0, 0, 1}) - x * 0.0;
    z = z * (1.0 / z.length());
    const Vec3 y = z.cross(x);
    return {{{x.x, x.y, x.z}, {y.x, y.y, y.z}, {z.x, z.y, z.z}}};
}

Mat12 localK(double L, const Material& mt, const Section& s) {
    Mat12 k{};
    const double EA = mt.E * s.A / L, GJ = mt.G * s.J / L;
    const double z12 = 12 * mt.E * s.Iz / (L * L * L), z6 = 6 * mt.E * s.Iz / (L * L), z4 = 4 * mt.E * s.Iz / L, z2 = 2 * mt.E * s.Iz / L;
    const double y12 = 12 * mt.E * s.Iy / (L * L * L), y6 = 6 * mt.E * s.Iy / (L * L), y4 = 4 * mt.E * s.Iy / L, y2 = 2 * mt.E * s.Iy / L;
    auto S = [&](int i, int j, double v) { k[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = v; k[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = v; };
    S(0, 0, EA); S(0, 6, -EA); S(6, 6, EA);
    S(3, 3, GJ); S(3, 9, -GJ); S(9, 9, GJ);
    S(1, 1, z12); S(1, 5, z6); S(1, 7, -z12); S(1, 11, z6);
    S(5, 5, z4); S(5, 7, -z6); S(5, 11, z2);
    S(7, 7, z12); S(7, 11, -z6); S(11, 11, z4);
    S(2, 2, y12); S(2, 4, -y6); S(2, 8, -y12); S(2, 10, -y6);
    S(4, 4, y4); S(4, 8, y6); S(4, 10, y2);
    S(8, 8, y12); S(8, 10, y6); S(10, 10, y4);
    return k;
}
// T (12×12, blok-diagonal R) bilan: Kg = Tᵀ k T
Mat12 globalK(const Mat12& k, const Mat3& R) {
    Mat12 T{};
    for (int b = 0; b < 4; ++b)
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) T[static_cast<std::size_t>(3 * b + i)][static_cast<std::size_t>(3 * b + j)] = R[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
    Mat12 kt{}, g{};
    for (int i = 0; i < 12; ++i)
        for (int j = 0; j < 12; ++j) {
            double s = 0;
            for (int q = 0; q < 12; ++q) s += k[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)] * T[static_cast<std::size_t>(q)][static_cast<std::size_t>(j)];
            kt[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = s;
        }
    for (int i = 0; i < 12; ++i)
        for (int j = 0; j < 12; ++j) {
            double s = 0;
            for (int q = 0; q < 12; ++q) s += T[static_cast<std::size_t>(q)][static_cast<std::size_t>(i)] * kt[static_cast<std::size_t>(q)][static_cast<std::size_t>(j)];
            g[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = s;
        }
    return g;
}

// Lentali simmetrik matritsa: a[i][j], j ∈ [i-bw, i]
struct Band {
    int n = 0, bw = 0;
    std::vector<double> a;
    double& at(int i, int j) { return a[static_cast<std::size_t>(i) * (bw + 1) + (j - i + bw)]; }   // j ≤ i
    double get(int i, int j) const {
        if (j > i) std::swap(i, j);
        if (i - j > bw) return 0;
        return a[static_cast<std::size_t>(i) * (bw + 1) + (j - i + bw)];
    }
    void add(int i, int j, double v) {
        if (j > i) std::swap(i, j);
        at(i, j) += v;
    }
    void factor() {   // L Lᵀ joyida
        for (int i = 0; i < n; ++i) {
            for (int j = std::max(0, i - bw); j <= i; ++j) {
                double s = at(i, j);
                for (int k = std::max({0, i - bw, j - bw}); k < j; ++k) s -= at(i, k) * at(j, k);
                if (i == j) {
                    if (s <= 1e-12 * std::max(1.0, std::abs(at(i, i)))) throw std::runtime_error("fem: unstable (mechanism) at equation " + std::to_string(i));
                    at(i, i) = std::sqrt(s);
                } else at(i, j) = s / at(j, j);
            }
        }
    }
    void solve(std::vector<double>& b) {
        for (int i = 0; i < n; ++i) {
            double s = b[static_cast<std::size_t>(i)];
            for (int k = std::max(0, i - bw); k < i; ++k) s -= at(i, k) * b[static_cast<std::size_t>(k)];
            b[static_cast<std::size_t>(i)] = s / at(i, i);
        }
        for (int i = n - 1; i >= 0; --i) {
            double s = b[static_cast<std::size_t>(i)];
            for (int k = i + 1; k <= std::min(n - 1, i + bw); ++k) s -= at(k, i) * b[static_cast<std::size_t>(k)];
            b[static_cast<std::size_t>(i)] = s / at(i, i);
        }
    }
};

struct Assembled {
    std::vector<int> eq;          // tugun*6+erkinlik → tenglama raqami (-1 — mahkamlangan)
    int neq = 0;
    Band K;
    std::vector<double> M;        // diagonal (tenglamalar bo'yicha)
    std::vector<Mat3> R;
    std::vector<double> L;
    std::vector<Mat12> kl;
};

Assembled assemble(const Model& m) {
    Assembled A;
    const std::size_t nn = m.nodes.size();
    A.eq.assign(nn * 6, -1);
    for (std::size_t i = 0; i < nn; ++i)
        for (int d = 0; d < 6; ++d)
            if (!m.nodes[i].fix[static_cast<std::size_t>(d)]) A.eq[i * 6 + static_cast<std::size_t>(d)] = A.neq++;
    int bw = 0;
    for (const auto& b : m.beams) {
        const int i1 = m.index(b.n1), i2 = m.index(b.n2);
        int lo = 1 << 30, hi = -1;
        for (int nd : {i1, i2})
            for (int d = 0; d < 6; ++d) {
                const int e = A.eq[static_cast<std::size_t>(nd) * 6 + static_cast<std::size_t>(d)];
                if (e >= 0) { lo = std::min(lo, e); hi = std::max(hi, e); }
            }
        if (hi >= 0) bw = std::max(bw, hi - lo);
    }
    A.K.n = A.neq;
    A.K.bw = bw;
    A.K.a.assign(static_cast<std::size_t>(A.neq) * (bw + 1), 0.0);
    for (const auto& b : m.beams) {
        const int i1 = m.index(b.n1), i2 = m.index(b.n2);
        double L;
        const Mat3 R = axes(m.nodes[static_cast<std::size_t>(i1)].p, m.nodes[static_cast<std::size_t>(i2)].p, b.vz, L);
        const Mat12 kl = localK(L, m.mats.at(b.mat), m.secs.at(b.sec));
        const Mat12 kg = globalK(kl, R);
        A.R.push_back(R);
        A.L.push_back(L);
        A.kl.push_back(kl);
        int map[12];
        for (int d = 0; d < 6; ++d) {
            map[d] = A.eq[static_cast<std::size_t>(i1) * 6 + static_cast<std::size_t>(d)];
            map[6 + d] = A.eq[static_cast<std::size_t>(i2) * 6 + static_cast<std::size_t>(d)];
        }
        for (int r = 0; r < 12; ++r)
            for (int c = 0; c < 12; ++c)
                if (map[r] >= 0 && map[c] >= 0 && map[r] >= map[c]) A.K.at(map[r], map[c]) += kg[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
    }
    A.M.assign(static_cast<std::size_t>(A.neq), 0.0);
    for (const auto& [nid, mv] : m.mass) {
        const int i = m.index(nid);
        if (i < 0) continue;
        for (int d = 0; d < 3; ++d)
            if (const int e = A.eq[static_cast<std::size_t>(i) * 6 + static_cast<std::size_t>(d)]; e >= 0) A.M[static_cast<std::size_t>(e)] += mv;
    }
    return A;
}

// kichik simmetrik matritsa — Yakobi
void jacobi(std::vector<std::vector<double>>& a, std::vector<double>& w, std::vector<std::vector<double>>& v) {
    const std::size_t n = a.size();
    v.assign(n, std::vector<double>(n, 0));
    for (std::size_t i = 0; i < n; ++i) v[i][i] = 1;
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0;
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = i + 1; j < n; ++j) off += a[i][j] * a[i][j];
        if (off < 1e-22) break;
        for (std::size_t p = 0; p < n; ++p)
            for (std::size_t q = p + 1; q < n; ++q) {
                if (std::abs(a[p][q]) < 1e-300) continue;
                const double th = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                const double t = (th >= 0 ? 1 : -1) / (std::abs(th) + std::sqrt(th * th + 1));
                const double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (std::size_t k = 0; k < n; ++k) {
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
    }
    w.resize(n);
    for (std::size_t i = 0; i < n; ++i) w[i] = a[i][i];
}

std::vector<std::array<double, 6>> expand(const Model& m, const Assembled& A, const std::vector<double>& u) {
    std::vector<std::array<double, 6>> d(m.nodes.size());
    for (std::size_t i = 0; i < m.nodes.size(); ++i)
        for (int k = 0; k < 6; ++k) {
            const int e = A.eq[i * 6 + static_cast<std::size_t>(k)];
            d[i][static_cast<std::size_t>(k)] = e >= 0 ? u[static_cast<std::size_t>(e)] : 0.0;
        }
    return d;
}
}  // namespace

StaticResult solveStatic(const Model& m) {
    auto A = assemble(m);
    std::vector<double> F(static_cast<std::size_t>(A.neq), 0.0);
    for (const auto& [nid, f] : m.loads) {
        const int i = m.index(nid);
        if (i < 0) continue;
        for (int d = 0; d < 6; ++d)
            if (const int e = A.eq[static_cast<std::size_t>(i) * 6 + static_cast<std::size_t>(d)]; e >= 0) F[static_cast<std::size_t>(e)] += f[static_cast<std::size_t>(d)];
    }
    Band K = A.K;
    K.factor();
    std::vector<double> u = F;
    K.solve(u);
    StaticResult r;
    r.disp = expand(m, A, u);
    for (const auto& d : r.disp) r.maxDisp = std::max(r.maxDisp, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
    // element kuchlari va reaksiyalar
    std::vector<std::array<double, 6>> nodalF(m.nodes.size(), std::array<double, 6>{});
    for (std::size_t bi = 0; bi < m.beams.size(); ++bi) {
        const auto& b = m.beams[bi];
        const int i1 = m.index(b.n1), i2 = m.index(b.n2);
        const Mat3& R = A.R[bi];
        double ug[12], ul[12];
        for (int d = 0; d < 6; ++d) { ug[d] = r.disp[static_cast<std::size_t>(i1)][static_cast<std::size_t>(d)]; ug[6 + d] = r.disp[static_cast<std::size_t>(i2)][static_cast<std::size_t>(d)]; }
        for (int blk = 0; blk < 4; ++blk)
            for (int i = 0; i < 3; ++i) {
                double s = 0;
                for (int j = 0; j < 3; ++j) s += R[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] * ug[3 * blk + j];
                ul[3 * blk + i] = s;
            }
        BeamForces bf;
        for (int i = 0; i < 12; ++i) {
            double s = 0;
            for (int j = 0; j < 12; ++j) s += A.kl[bi][static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] * ul[j];
            bf.f[static_cast<std::size_t>(i)] = s;
        }
        r.forces.push_back(bf);
        // global uch kuchlari → tugunlarga
        for (int blk = 0; blk < 4; ++blk)
            for (int j = 0; j < 3; ++j) {
                double s = 0;
                for (int i = 0; i < 3; ++i) s += R[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] * bf.f[static_cast<std::size_t>(3 * blk + i)];
                const int node = blk < 2 ? i1 : i2;
                nodalF[static_cast<std::size_t>(node)][static_cast<std::size_t>((blk % 2) * 3 + j)] += s;
            }
    }
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        if (std::none_of(m.nodes[i].fix.begin(), m.nodes[i].fix.end(), [](bool b) { return b; })) continue;
        std::array<double, 6> R6{};
        for (int d = 0; d < 6; ++d) {
            double ext = 0;
            if (auto it = m.loads.find(m.nodes[i].id); it != m.loads.end()) ext = it->second[static_cast<std::size_t>(d)];
            R6[static_cast<std::size_t>(d)] = m.nodes[i].fix[static_cast<std::size_t>(d)] ? nodalF[i][static_cast<std::size_t>(d)] - ext : 0.0;
        }
        r.reactions[m.nodes[i].id] = R6;
    }
    return r;
}

std::vector<ModeShape> solveModes(const Model& m, int count) {
    auto A = assemble(m);
    const int n = A.neq;
    int massDofs = 0;
    for (double v : A.M) massDofs += v > 0;
    count = std::min(count, massDofs);
    if (count <= 0) return {};
    const int p = std::min(massDofs, std::max(count + 8, 2 * count));
    Band K = A.K;
    K.factor();
    // boshlang'ich vektorlar: massali erkinliklarda turli naqshlar
    std::vector<std::vector<double>> X(static_cast<std::size_t>(p), std::vector<double>(static_cast<std::size_t>(n), 0.0));
    std::vector<int> md;
    for (int i = 0; i < n; ++i) if (A.M[static_cast<std::size_t>(i)] > 0) md.push_back(i);
    for (int c = 0; c < p; ++c)
        for (std::size_t k = 0; k < md.size(); ++k)
            X[static_cast<std::size_t>(c)][static_cast<std::size_t>(md[k])] = c == 0 ? 1.0 : std::sin((c + 1) * 1.7 * (k + 1) + c);
    std::vector<double> lam(static_cast<std::size_t>(p), 0), lamOld;
    std::vector<std::vector<double>> Y(static_cast<std::size_t>(p));
    for (int it = 0; it < 60; ++it) {
        for (int c = 0; c < p; ++c) {
            std::vector<double> b(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) b[static_cast<std::size_t>(i)] = A.M[static_cast<std::size_t>(i)] * X[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
            K.solve(b);
            Y[static_cast<std::size_t>(c)] = std::move(b);
        }
        // Kr = Yᵀ M X, Mr = Yᵀ M Y
        std::vector<std::vector<double>> Kr(static_cast<std::size_t>(p), std::vector<double>(static_cast<std::size_t>(p))), Mr = Kr;
        for (int a = 0; a < p; ++a)
            for (int b = a; b < p; ++b) {
                double sk = 0, sm = 0;
                for (int i : md) {
                    sk += Y[static_cast<std::size_t>(a)][static_cast<std::size_t>(i)] * A.M[static_cast<std::size_t>(i)] * X[static_cast<std::size_t>(b)][static_cast<std::size_t>(i)];
                    sm += Y[static_cast<std::size_t>(a)][static_cast<std::size_t>(i)] * A.M[static_cast<std::size_t>(i)] * Y[static_cast<std::size_t>(b)][static_cast<std::size_t>(i)];
                }
                Kr[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] = Kr[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)] = sk;
                Mr[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] = Mr[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)] = sm;
            }
        // Mr = L Lᵀ, A = L⁻¹ Kr L⁻ᵀ
        const std::size_t P = static_cast<std::size_t>(p);
        std::vector<std::vector<double>> Lm(P, std::vector<double>(P, 0));
        for (std::size_t i = 0; i < P; ++i)
            for (std::size_t j = 0; j <= i; ++j) {
                double s = Mr[i][j];
                for (std::size_t k = 0; k < j; ++k) s -= Lm[i][k] * Lm[j][k];
                if (i == j) Lm[i][i] = std::sqrt(std::max(s, 1e-300));
                else Lm[i][j] = s / Lm[j][j];
            }
        auto solveL = [&](std::vector<double> b) {   // L x = b
            for (std::size_t i = 0; i < P; ++i) { double s = b[i]; for (std::size_t k = 0; k < i; ++k) s -= Lm[i][k] * b[k]; b[i] = s / Lm[i][i]; }
            return b;
        };
        std::vector<std::vector<double>> T1(P), Am(P, std::vector<double>(P));
        for (std::size_t j = 0; j < P; ++j) { std::vector<double> col(P); for (std::size_t i = 0; i < P; ++i) col[i] = Kr[i][j]; T1[j] = solveL(col); }
        for (std::size_t i = 0; i < P; ++i) { std::vector<double> row(P); for (std::size_t j = 0; j < P; ++j) row[j] = T1[j][i]; const auto r2 = solveL(row); for (std::size_t j = 0; j < P; ++j) Am[i][j] = r2[j]; }
        for (std::size_t i = 0; i < P; ++i) for (std::size_t j = 0; j < i; ++j) Am[i][j] = Am[j][i] = (Am[i][j] + Am[j][i]) / 2;
        std::vector<double> w;
        std::vector<std::vector<double>> V;
        jacobi(Am, w, V);
        // tartib: o'sish bo'yicha (λ = ω²)
        std::vector<std::size_t> ord(P);
        for (std::size_t i = 0; i < P; ++i) ord[i] = i;
        std::sort(ord.begin(), ord.end(), [&](std::size_t a, std::size_t b) { return w[a] < w[b]; });
        // Q = L⁻ᵀ V
        std::vector<std::vector<double>> Q(P, std::vector<double>(P));
        for (std::size_t c = 0; c < P; ++c) {
            std::vector<double> v(P);
            for (std::size_t i = 0; i < P; ++i) v[i] = V[i][ord[c]];
            for (std::size_t i = P; i-- > 0;) { double s = v[i]; for (std::size_t k = i + 1; k < P; ++k) s -= Lm[k][i] * v[k]; v[i] = s / Lm[i][i]; }
            for (std::size_t i = 0; i < P; ++i) Q[i][c] = v[i];
            lam[c] = w[ord[c]];
        }
        for (std::size_t c = 0; c < P; ++c) {
            std::vector<double> x(static_cast<std::size_t>(n), 0.0);
            for (std::size_t k = 0; k < P; ++k) {
                const double q = Q[k][c];
                if (q == 0) continue;
                for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] += Y[k][static_cast<std::size_t>(i)] * q;
            }
            X[c] = std::move(x);
        }
        if (!lamOld.empty()) {
            double err = 0;
            for (int c = 0; c < count; ++c) err = std::max(err, std::abs(lam[static_cast<std::size_t>(c)] - lamOld[static_cast<std::size_t>(c)]) / std::max(1e-12, std::abs(lam[static_cast<std::size_t>(c)])));
            if (err < 1e-10) break;
        }
        lamOld = lam;
    }
    double totM[3] = {0, 0, 0};
    for (const auto& [nid, mv] : m.mass) for (double& t : totM) t += mv;
    std::vector<ModeShape> out;
    for (int c = 0; c < count; ++c) {
        ModeShape ms;
        const double l = lam[static_cast<std::size_t>(c)];   // Am λ = 1/ω²? — subfazoda K⁻¹M: Kr/Mr nisbat = ω²
        ms.omega = std::sqrt(std::max(l, 1e-30));
        ms.T = 2 * std::numbers::pi / ms.omega;
        const auto& x = X[static_cast<std::size_t>(c)];
        double mm = 0;
        for (int i : md) mm += x[static_cast<std::size_t>(i)] * A.M[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)];
        const double sc = 1 / std::sqrt(std::max(mm, 1e-300));   // massaga normallashtirish (φᵀMφ = 1)
        std::vector<double> xn(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) xn[i] = x[i] * sc;
        ms.phi = expand(m, A, xn);
        for (int d = 0; d < 3; ++d) {
            double s = 0;
            for (std::size_t i = 0; i < m.nodes.size(); ++i) {
                const int e = A.eq[i * 6 + static_cast<std::size_t>(d)];
                if (e >= 0) s += A.M[static_cast<std::size_t>(e)] * xn[static_cast<std::size_t>(e)];
            }
            ms.mpf[static_cast<std::size_t>(d)] = s;
            ms.meffRatio[static_cast<std::size_t>(d)] = totM[d] > 0 ? s * s / totM[d] : 0;
        }
        out.push_back(std::move(ms));
    }
    return out;
}

SpectrumResult responseSpectrum(const Model& m, const std::vector<ModeShape>& modes, const std::function<double(double)>& sa, int dir, double zeta) {
    SpectrumResult r;
    const std::size_t nm = modes.size(), nn = m.nodes.size();
    r.disp.assign(nn, std::array<double, 6>{});
    if (nm == 0) return r;
    std::vector<std::vector<std::array<double, 6>>> u(nm, std::vector<std::array<double, 6>>(nn));
    std::vector<double> V(nm);
    for (std::size_t k = 0; k < nm; ++k) {
        const double G = modes[k].mpf[static_cast<std::size_t>(dir)], w = modes[k].omega, A = sa(modes[k].T);
        for (std::size_t i = 0; i < nn; ++i)
            for (int d = 0; d < 6; ++d) u[k][i][static_cast<std::size_t>(d)] = G * modes[k].phi[i][static_cast<std::size_t>(d)] * A / (w * w);
        V[k] = G * G * A;   // samarali massa × Sa (kN, t·m/s²)
    }
    auto rho = [&](std::size_t i, std::size_t j) {
        const double b = modes[j].omega / modes[i].omega;
        return 8 * zeta * zeta * (1 + b) * std::pow(b, 1.5) / ((1 - b * b) * (1 - b * b) + 4 * zeta * zeta * b * (1 + b) * (1 + b));
    };
    for (std::size_t n = 0; n < nn; ++n)
        for (int d = 0; d < 6; ++d) {
            double s = 0;
            for (std::size_t i = 0; i < nm; ++i)
                for (std::size_t j = 0; j < nm; ++j) s += rho(i, j) * u[i][n][static_cast<std::size_t>(d)] * u[j][n][static_cast<std::size_t>(d)];
            r.disp[n][static_cast<std::size_t>(d)] = std::sqrt(std::max(s, 0.0));
        }
    double s = 0;
    for (std::size_t i = 0; i < nm; ++i)
        for (std::size_t j = 0; j < nm; ++j) s += rho(i, j) * V[i] * V[j];
    r.baseShear = std::sqrt(std::max(s, 0.0));
    for (const auto& d : r.disp) r.roof = std::max(r.roof, d[static_cast<std::size_t>(dir)]);
    return r;
}

Model frameGenerator(const std::vector<double>& dx, const std::vector<double>& dy, int floors, double h, double colB, double colH, double beamB,
                     double beamH, double E, double massPerArea, double gravityPerArea) {
    Model m;
    m.mats[1] = {E, E / 2.4};
    auto rect = [](double b, double hh) {
        Section s;
        s.A = b * hh;
        s.Iy = b * hh * hh * hh / 12;
        s.Iz = hh * b * b * b / 12;
        const double a = std::max(b, hh), c = std::min(b, hh);
        s.J = a * c * c * c * (1.0 / 3 - 0.21 * c / a * (1 - std::pow(c / a, 4) / 12));
        return s;
    };
    m.secs[1] = rect(colB, colH);
    m.secs[2] = rect(beamB, beamH);
    std::vector<double> X{0}, Y{0};
    for (double d : dx) X.push_back(X.back() + d);
    for (double d : dy) Y.push_back(Y.back() + d);
    const int nx = static_cast<int>(X.size()), ny = static_cast<int>(Y.size());
    auto id = [&](int k, int j, int i) { return 1 + k * nx * ny + j * nx + i; };
    for (int k = 0; k <= floors; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                Node n;
                n.id = id(k, j, i);
                n.p = {X[static_cast<std::size_t>(i)], Y[static_cast<std::size_t>(j)], k * h};
                if (k == 0) n.fix = {true, true, true, true, true, true};
                m.nodes.push_back(n);
                if (k > 0) {
                    const double tx = ((i > 0 ? X[static_cast<std::size_t>(i)] - X[static_cast<std::size_t>(i - 1)] : 0) + (i + 1 < nx ? X[static_cast<std::size_t>(i + 1)] - X[static_cast<std::size_t>(i)] : 0)) / 2;
                    const double ty = ((j > 0 ? Y[static_cast<std::size_t>(j)] - Y[static_cast<std::size_t>(j - 1)] : 0) + (j + 1 < ny ? Y[static_cast<std::size_t>(j + 1)] - Y[static_cast<std::size_t>(j)] : 0)) / 2;
                    m.mass[n.id] = massPerArea * tx * ty;
                    if (gravityPerArea != 0) m.loads[n.id] = {0, 0, -gravityPerArea * tx * ty, 0, 0, 0};
                }
            }
    int bid = 1;
    for (int k = 1; k <= floors; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                m.beams.push_back({bid++, id(k - 1, j, i), id(k, j, i), 1, 1, {}});
                if (i + 1 < nx) m.beams.push_back({bid++, id(k, j, i), id(k, j, i + 1), 1, 2, {}});
                if (j + 1 < ny) m.beams.push_back({bid++, id(k, j, i), id(k, j + 1, i), 1, 2, {}});
            }
    return m;
}

}  // namespace asiacad::bim::fem
