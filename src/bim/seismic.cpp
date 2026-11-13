// AsiaCAD — seysmik hisob: kesim, modal tahlil, spektral va vaqt sohasidagi javob, QMQ 2.01.03-19 / MTSh formulalari.
#include <asiacad/bim/seismic.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace asiacad::bim::seismic {

namespace {
// shoelace integrallari: A, Sx=∫x, Sy=∫y, Ixx=∫y², Iyy=∫x² (koordinata boshiga nisbatan), ishora bilan
struct Raw { double A = 0, Sx = 0, Sy = 0, Iyy0 = 0, Ixx0 = 0; };
Raw ring(const std::vector<P2>& p) {
    Raw r;
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        const P2 a = p[i], b = p[(i + 1) % n];
        const double c = a.x * b.y - b.x * a.y;
        r.A += c / 2;
        r.Sx += (a.x + b.x) * c / 6;
        r.Sy += (a.y + b.y) * c / 6;
        r.Ixx0 += (a.y * a.y + a.y * b.y + b.y * b.y) * c / 12;
        r.Iyy0 += (a.x * a.x + a.x * b.x + b.x * b.x) * c / 12;
    }
    if (r.A < 0) { r.A = -r.A; r.Sx = -r.Sx; r.Sy = -r.Sy; r.Ixx0 = -r.Ixx0; r.Iyy0 = -r.Iyy0; }
    return r;
}
// zich chiziqli tizim: A·X = B (A n×n, B n×m), qisman bosh element bilan
void solve(std::vector<double> A, int n, std::vector<double>& B, int m) {
    for (int c = 0; c < n; ++c) {
        int piv = c;
        for (int r = c + 1; r < n; ++r)
            if (std::abs(A[r * n + c]) > std::abs(A[piv * n + c])) piv = r;
        if (std::abs(A[piv * n + c]) < 1e-300) throw std::runtime_error("seismic: singular matrix");
        if (piv != c) {
            for (int k = 0; k < n; ++k) std::swap(A[c * n + k], A[piv * n + k]);
            for (int k = 0; k < m; ++k) std::swap(B[c * m + k], B[piv * m + k]);
        }
        for (int r = 0; r < n; ++r) {
            if (r == c) continue;
            const double f = A[r * n + c] / A[c * n + c];
            if (f == 0) continue;
            for (int k = c; k < n; ++k) A[r * n + k] -= f * A[c * n + k];
            for (int k = 0; k < m; ++k) B[r * m + k] -= f * B[c * m + k];
        }
    }
    for (int r = 0; r < n; ++r)
        for (int k = 0; k < m; ++k) B[r * m + k] /= A[r * n + r];
}
double cqcRho(double wi, double wj, double z) {
    const double r = wi / wj;
    const double d = (1 - r * r) * (1 - r * r) + 4 * z * z * r * (1 + r) * (1 + r);
    return 8 * z * z * (1 + r) * std::pow(r, 1.5) / d;
}
double combine(const std::vector<double>& v, const std::vector<double>& w, const std::string& comb, double z) {
    double s = 0;
    for (std::size_t i = 0; i < v.size(); ++i)
        for (std::size_t j = 0; j < v.size(); ++j) {
            if (comb != "CQC" && i != j) continue;
            s += (i == j ? 1.0 : cqcRho(w[i], w[j], z)) * v[i] * v[j];
        }
    return std::sqrt(std::max(0.0, s));
}
}  // namespace

Section polygonSection(const std::vector<P2>& outer, const std::vector<std::vector<P2>>& holes) {
    Raw t = ring(outer);
    for (const auto& h : holes) {
        const Raw r = ring(h);
        t.A -= r.A; t.Sx -= r.Sx; t.Sy -= r.Sy; t.Ixx0 -= r.Ixx0; t.Iyy0 -= r.Iyy0;
    }
    Section s;
    s.A = t.A;
    if (t.A <= 0) return s;
    s.cx = t.Sx / t.A;
    s.cy = t.Sy / t.A;
    s.Ix = t.Ixx0 - t.A * s.cy * s.cy;
    s.Iy = t.Iyy0 - t.A * s.cx * s.cx;
    return s;
}

Section combine(const std::vector<Section>& parts) {
    Section s;
    for (const auto& p : parts) { s.A += p.A; s.cx += p.A * p.cx; s.cy += p.A * p.cy; }
    if (s.A <= 0) return s;
    s.cx /= s.A;
    s.cy /= s.A;
    for (const auto& p : parts) {
        s.Ix += p.Ix + p.A * (p.cy - s.cy) * (p.cy - s.cy);
        s.Iy += p.Iy + p.A * (p.cx - s.cx) * (p.cx - s.cx);
    }
    return s;
}

double frameStoryStiffness(double E, const std::vector<double>& Ic, double h, double a) {
    double s = 0;
    for (double I : Ic) s += 12 * E * I * a / (h * h * h);
    return s;
}

void jacobiEigen(std::vector<double> A, int n, std::vector<double>& w, std::vector<double>& V) {
    V.assign(static_cast<std::size_t>(n * n), 0.0);
    for (int i = 0; i < n; ++i) V[i * n + i] = 1;
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0, diag = 0;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) (i == j ? diag : off) += A[i * n + j] * A[i * n + j];
        if (off <= 1e-22 * std::max(diag, 1e-300)) break;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) {
                const double apq = A[p * n + q];
                if (std::abs(apq) < 1e-300) continue;
                const double th = (A[q * n + q] - A[p * n + p]) / (2 * apq);
                const double t = (th >= 0 ? 1 : -1) / (std::abs(th) + std::sqrt(th * th + 1));
                const double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < n; ++k) {
                    const double akp = A[k * n + p], akq = A[k * n + q];
                    A[k * n + p] = c * akp - s * akq;
                    A[k * n + q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const double apk = A[p * n + k], aqk = A[q * n + k];
                    A[p * n + k] = c * apk - s * aqk;
                    A[q * n + k] = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const double vkp = V[k * n + p], vkq = V[k * n + q];
                    V[k * n + p] = c * vkp - s * vkq;
                    V[k * n + q] = s * vkp + c * vkq;
                }
            }
    }
    std::vector<int> idx(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return A[a * n + a] < A[b * n + b]; });
    w.assign(static_cast<std::size_t>(n), 0);
    std::vector<double> V2(static_cast<std::size_t>(n * n));
    for (int k = 0; k < n; ++k) {
        w[k] = A[idx[k] * n + idx[k]];
        for (int i = 0; i < n; ++i) V2[i * n + k] = V[i * n + idx[k]];
    }
    V = std::move(V2);
}

std::vector<Mode> modalAnalysis(const std::vector<Story>& st, int nModes) {
    const int n = static_cast<int>(st.size());
    if (n == 0) return {};
    const int N = 2 * n;  // (u, θ) har sathda; asos qotirilgan
    std::vector<double> K(static_cast<std::size_t>(N * N), 0.0);
    for (int e = 0; e < n; ++e) {
        const Story& s = st[e];
        const double L = s.h, EI = std::max(s.EI, 1e-9);
        const double ph = s.GA > 0 ? 12 * EI / (s.GA * L * L) : 0.0;
        const double c = EI / (L * L * L * (1 + ph));
        double k[4][4] = {{12, 6 * L, -12, 6 * L},
                          {6 * L, (4 + ph) * L * L, -6 * L, (2 - ph) * L * L},
                          {-12, -6 * L, 12, -6 * L},
                          {6 * L, (2 - ph) * L * L, -6 * L, (4 + ph) * L * L}};
        for (auto& r : k)
            for (double& v : r) v *= c;
        k[0][0] += s.kFrame; k[2][2] += s.kFrame; k[0][2] -= s.kFrame; k[2][0] -= s.kFrame;
        const int dof[4] = {2 * e - 2, 2 * e - 1, 2 * e, 2 * e + 1};  // e=0: pastki tugun — asos
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b)
                if (dof[a] >= 0 && dof[b] >= 0) K[dof[a] * N + dof[b]] += k[a][b];
    }
    // kondensatsiya: Kc = Ktt − Ktr·Krr⁻¹·Krt
    std::vector<double> Krr(static_cast<std::size_t>(n * n)), Krt(static_cast<std::size_t>(n * n)), Kc(static_cast<std::size_t>(n * n));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            Krr[i * n + j] = K[(2 * i + 1) * N + 2 * j + 1];
            Krt[i * n + j] = K[(2 * i + 1) * N + 2 * j];
        }
    std::vector<double> X = Krt;
    solve(Krr, n, X, n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            double s = K[(2 * i) * N + 2 * j];
            for (int k = 0; k < n; ++k) s -= K[(2 * i) * N + 2 * k + 1] * X[k * n + j];
            Kc[i * n + j] = s;
        }
    // M^-1/2 Kc M^-1/2
    std::vector<double> mi(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) mi[i] = 1 / std::sqrt(std::max(st[i].mass, 1e-9));
    std::vector<double> A(static_cast<std::size_t>(n * n));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) A[i * n + j] = 0.5 * (Kc[i * n + j] + Kc[j * n + i]) * mi[i] * mi[j];
    std::vector<double> w, V;
    jacobiEigen(A, n, w, V);
    std::vector<Mode> out;
    for (int k = 0; k < std::min(nModes, n); ++k) {
        Mode m;
        m.T = 2 * std::numbers::pi / std::sqrt(std::max(w[k], 1e-12));
        m.phi.resize(static_cast<std::size_t>(n));
        double top = 0;
        for (int i = 0; i < n; ++i) m.phi[i] = V[i * n + k] * mi[i];
        top = m.phi[n - 1] != 0 ? m.phi[n - 1] : 1;
        for (double& v : m.phi) v /= top;  // tepasi = 1
        double mn = 0, l = 0;
        for (int i = 0; i < n; ++i) { mn += st[i].mass * m.phi[i] * m.phi[i]; l += st[i].mass * m.phi[i]; }
        m.gamma = l / mn;
        m.meff = l * l / mn;
        out.push_back(std::move(m));
    }
    return out;
}

double Spectrum::sa(double T) const {
    if (pts.empty()) return 0;
    if (T <= pts.front().first) return pts.front().second;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const auto [t0, s0] = pts[i];
        const auto [t1, s1] = pts[i + 1];
        if (T <= t1) {
            if (t0 <= 0 || s0 <= 0 || s1 <= 0) return s0 + (s1 - s0) * (T - t0) / (t1 - t0);
            const double u = std::log(T / t0) / std::log(t1 / t0);
            return std::exp(std::log(s0) + u * (std::log(s1) - std::log(s0)));
        }
    }
    const auto [tl, sl] = pts.back();
    return sl * (tl / T) * (tl / T);
}

Spectrum siteSpectrumLot16(int rp) {
    // Seysmologiya instituti hisoboti, 13-jadval (II toifa grunt): PGA va PSA(T), g
    static const double T[] = {0.0, 0.05, 0.1, 0.2, 0.3, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0};
    static const double S475[] = {0.233, 0.341, 0.491, 0.560, 0.472, 0.301, 0.192, 0.131, 0.076, 0.049, 0.025};
    static const double S1000[] = {0.310, 0.435, 0.653, 0.745, 0.650, 0.412, 0.262, 0.185, 0.107, 0.068, 0.035};
    static const double S2475[] = {0.420, 0.598, 0.873, 1.050, 0.912, 0.603, 0.386, 0.271, 0.157, 0.102, 0.053};
    static const double S5000[] = {0.518, 0.720, 1.090, 1.300, 1.170, 0.762, 0.498, 0.357, 0.207, 0.134, 0.069};
    const double* s = rp <= 475 ? S475 : rp <= 1000 ? S1000 : rp <= 2475 ? S2475 : S5000;
    Spectrum sp;
    for (int i = 0; i < 11; ++i) sp.pts.emplace_back(T[i], s[i]);
    return sp;
}

Response responseSpectrum(const std::vector<Story>& st, const std::vector<Mode>& modes, const Spectrum& sp, double R, const std::string& comb,
                          double zeta) {
    const std::size_t n = st.size(), nm = modes.size();
    std::vector<double> om(nm);
    std::vector<std::vector<double>> D(nm, std::vector<double>(n)), Dr = D, Vs = D, F = D;
    std::vector<double> Vb(nm), Mb(nm);
    std::vector<double> z(n);
    double acc = 0;
    for (std::size_t i = 0; i < n; ++i) { acc += st[i].h; z[i] = acc; }
    for (std::size_t k = 0; k < nm; ++k) {
        const Mode& m = modes[k];
        om[k] = 2 * std::numbers::pi / m.T;
        const double a = sp.sa(m.T) * kG / R;
        for (std::size_t i = 0; i < n; ++i) {
            D[k][i] = m.gamma * m.phi[i] * a / (om[k] * om[k]);
            F[k][i] = st[i].mass * m.gamma * m.phi[i] * a;
        }
        double cum = 0;
        for (std::size_t i = n; i-- > 0;) { cum += F[k][i]; Vs[k][i] = cum; }
        for (std::size_t i = 0; i < n; ++i) {
            Dr[k][i] = (D[k][i] - (i ? D[k][i - 1] : 0)) / st[i].h;
            Mb[k] += F[k][i] * z[i];
        }
        Vb[k] = Vs[k][0];
    }
    Response r;
    r.disp.resize(n); r.drift.resize(n); r.storyShear.resize(n); r.force.resize(n);
    std::vector<double> col(nm);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < nm; ++k) col[k] = D[k][i];
        r.disp[i] = combine(col, om, comb, zeta);
        for (std::size_t k = 0; k < nm; ++k) col[k] = Dr[k][i];
        r.drift[i] = combine(col, om, comb, zeta);
        for (std::size_t k = 0; k < nm; ++k) col[k] = Vs[k][i];
        r.storyShear[i] = combine(col, om, comb, zeta);
        for (std::size_t k = 0; k < nm; ++k) col[k] = F[k][i];
        r.force[i] = combine(col, om, comb, zeta);
        if (r.drift[i] > r.maxDrift) { r.maxDrift = r.drift[i]; r.maxDriftStory = static_cast<int>(i) + 1; }
    }
    r.baseShear = combine(Vb, om, comb, zeta);
    r.baseMoment = combine(Mb, om, comb, zeta);
    r.roof = n ? r.disp[n - 1] : 0;
    return r;
}

std::vector<double> etaCoefficients(const std::vector<Story>& st, const Mode& m) {
    double num = 0, den = 0;
    for (std::size_t j = 0; j < st.size(); ++j) {
        const double Q = st[j].mass * kG;
        num += Q * m.phi[j];
        den += Q * m.phi[j] * m.phi[j];
    }
    std::vector<double> eta(st.size());
    for (std::size_t k = 0; k < st.size(); ++k) eta[k] = den > 0 ? m.phi[k] * num / den : 0;
    return eta;
}

std::vector<double> kmkModeForces(const std::vector<Story>& st, const Mode& m, const KmkParams& p) {
    const auto eta = etaCoefficients(st, m);
    std::vector<double> S(st.size());
    for (std::size_t k = 0; k < st.size(); ++k) {
        const double Qk = st[k].mass * kG;
        const double S0 = p.alpha * Qk * p.W * p.Kdelta * eta[k];
        S[k] = p.K0 * p.Kp * p.Ket * S0;
    }
    return S;
}

std::vector<double> newmarkSdof(double T, double zeta, const std::vector<double>& ag, double dt) {
    const std::size_t n = ag.size();
    std::vector<double> u(n, 0.0), v(n, 0.0), a(n, 0.0);
    if (n == 0) return u;
    const double w = 2 * std::numbers::pi / T, k = w * w, c = 2 * zeta * w, be = 0.25, ga = 0.5;
    const double kh = k + ga / (be * dt) * c + 1 / (be * dt * dt);
    a[0] = -ag[0];
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double dp = -(ag[i + 1] - ag[i]) + (1 / (be * dt) * v[i] + 0.5 / be * a[i]) + c * (1 / (2 * be) * v[i] + dt * (ga / (2 * be) - 1) * a[i]);
        const double du = dp / kh;
        const double dv = ga / (be * dt) * du - ga / be * v[i] + dt * (1 - ga / (2 * be)) * a[i];
        u[i + 1] = u[i] + du;
        v[i + 1] = v[i] + dv;
        a[i + 1] = -ag[i + 1] - c * v[i + 1] - k * u[i + 1];
    }
    return u;
}

History timeHistory(const std::vector<Story>& st, const std::vector<Mode>& modes, const std::vector<double>& ag, double dt, double zeta) {
    History h;
    h.dt = dt;
    const std::size_t n = st.size(), nt = ag.size();
    h.disp.assign(nt, std::vector<double>(n, 0.0));
    h.baseShear.assign(nt, 0.0);
    h.driftEnv.assign(n, 0.0);
    for (const auto& m : modes) {
        const auto q = newmarkSdof(m.T, zeta, ag, dt);
        const double w2 = std::pow(2 * std::numbers::pi / m.T, 2);
        double L = 0;
        for (std::size_t i = 0; i < n; ++i) L += st[i].mass * m.gamma * m.phi[i];
        for (std::size_t t = 0; t < nt; ++t) {
            for (std::size_t i = 0; i < n; ++i) h.disp[t][i] += m.gamma * m.phi[i] * q[t];
            h.baseShear[t] += L * w2 * q[t];
        }
    }
    for (std::size_t t = 0; t < nt; ++t) {
        h.roofMax = std::max(h.roofMax, std::abs(h.disp[t][n - 1]));
        h.baseShearMax = std::max(h.baseShearMax, std::abs(h.baseShear[t]));
        for (std::size_t i = 0; i < n; ++i) {
            const double d = std::abs(h.disp[t][i] - (i ? h.disp[t][i - 1] : 0)) / st[i].h;
            h.driftEnv[i] = std::max(h.driftEnv[i], d);
            h.driftMax = std::max(h.driftMax, d);
        }
    }
    return h;
}

std::vector<double> readAccelerogram(const std::string& text, int column, double* dt) {
    std::istringstream in(text);
    std::string line;
    std::vector<double> a, t;
    while (std::getline(in, line)) {
        for (char& ch : line) if (ch == ',') ch = '.';
        std::istringstream ls(line);
        std::vector<double> v;
        double x;
        while (ls >> x) v.push_back(x);
        if (!ls.eof() && v.empty()) continue;
        const std::size_t ia = static_cast<std::size_t>(2 * column + 1);
        if (v.size() <= ia) continue;
        t.push_back(v[ia - 1]);
        a.push_back(v[ia] / 100.0);  // sm/s² → m/s²
    }
    if (dt) *dt = t.size() > 1 ? t[1] - t[0] : 0.01;
    return a;
}

double dynamicPoisson(double vp, double vs) { return (vp * vp - 2 * vs * vs) / (2 * (vp * vp - vs * vs)); }
double dynamicModulus(double rho, double vs, double nu) { return 2 * rho * vs * vs * (1 + nu); }  // t/m³·m²/s² = kPa
double intensityIncrement(double rho0, double v0, double rho, double v) { return 1.67 * std::log10(rho0 * v0 / (rho * v)); }

double wallShearCapacity(double Acv, double Acol, double fc, double rhoH, double fy, double phi) {
    const double vc = 0.17 * std::sqrt(fc);
    const double wall = std::min(Acv * (vc + rhoH * fy), Acv * 0.66 * std::sqrt(fc));
    return phi * (wall + 0.8 * Acol * vc) * 1000.0;
}
double requiredRhoH(double V, double Acv, double Acol, double fc, double fy, double phi) {
    const double vc = 0.17 * std::sqrt(fc);
    const double need = (V / 1000.0 / phi - 0.8 * Acol * vc - Acv * vc) / (Acv * fy);
    return std::max(need, 0.0);
}
double overturningSafety(double W, double arm, double M, double reduce) { return M > 0 ? W * arm * reduce / M : 1e9; }

}  // namespace asiacad::bim::seismic

namespace asiacad::bim::seismic {

std::vector<Story> BuildingSeis::dir(char d) const {
    std::vector<Story> v;
    for (const auto& s : stories) {
        Story t;
        t.h = s.h; t.mass = s.mass;
        t.EI = d == 'y' ? s.EIy : s.EIx;
        t.GA = d == 'y' ? s.GAy : s.GAx;
        t.kFrame = d == 'y' ? s.kFy : s.kFx;
        v.push_back(t);
    }
    return v;
}

BuildingSeis readBuildingSeis(const std::string& text) {
    BuildingSeis b;
    std::istringstream in(text);
    std::string line;
    int ln = 0;
    while (std::getline(in, line)) {
        ++ln;
        const auto c = line.find('#');
        if (c != std::string::npos) line.erase(c);
        std::istringstream ls(line);
        std::string k;
        if (!(ls >> k)) continue;
        auto bad = [&] { return std::runtime_error("seis: line " + std::to_string(ln)); };
        if (k == "NAME") { std::getline(ls, b.name); while (!b.name.empty() && b.name.front() == ' ') b.name.erase(0, 1); }
        else if (k == "PGA9") { if (!(ls >> b.pga9)) throw bad(); }
        else if (k == "WFOUND") { if (!(ls >> b.Wfound)) throw bad(); }
        else if (k == "PHI") { if (!(ls >> b.phi)) throw bad(); }
        else if (k == "R") { if (!(ls >> b.R)) throw bad(); }
        else if (k == "GN") { if (!(ls >> b.gn)) throw bad(); }
        else if (k == "RHOH") { if (!(ls >> b.rhoH)) throw bad(); }
        else if (k == "FY") { if (!(ls >> b.fy)) throw bad(); }
        else if (k == "SPECTRUM" || k == "FOOT") {
            std::string tok;
            while (ls >> tok) {
                const auto sep = tok.find(k == "FOOT" ? ',' : ':');
                if (sep == std::string::npos) throw bad();
                const double a = std::stod(tok.substr(0, sep)), v = std::stod(tok.substr(sep + 1));
                if (k == "FOOT") b.foot.push_back({a, v});
                else b.spectrum.pts.emplace_back(a, v);
            }
        } else if (k == "PLAN") {
            int n;
            if (!(ls >> n) || n < 1) throw bad();
            if (b.plans.size() < static_cast<std::size_t>(n)) b.plans.resize(static_cast<std::size_t>(n));
            std::string tok;
            while (ls >> tok) {
                const auto sep = tok.find(',');
                if (sep == std::string::npos) throw bad();
                b.plans[static_cast<std::size_t>(n - 1)].push_back({std::stod(tok.substr(0, sep)), std::stod(tok.substr(sep + 1))});
            }
        } else if (k == "S") {
            int n;
            BuildingSeis::S s{};
            if (!(ls >> n >> s.h >> s.mass >> s.EIx >> s.EIy >> s.GAx >> s.GAy >> s.kFx >> s.kFy >> s.Acvx >> s.Acvy >> s.Acol >> s.fc)) throw bad();
            b.stories.push_back(s);
        } else throw bad();
    }
    if (b.stories.empty()) throw std::runtime_error("seis: no stories");
    if (b.spectrum.pts.empty()) b.spectrum = siteSpectrumLot16(5000);
    return b;
}

std::string writeBuildingSeis(const BuildingSeis& b) {
    std::ostringstream o;
    o.precision(10);
    o << "NAME " << b.name << "\nPGA9 " << b.pga9 << "\nWFOUND " << b.Wfound << "\nPHI " << b.phi << "\nR " << b.R << "\nGN " << b.gn
      << "\nRHOH " << b.rhoH << "\nFY " << b.fy << "\nSPECTRUM";
    for (const auto& [t, s] : b.spectrum.pts) o << ' ' << t << ':' << s;
    o << "\nFOOT";
    for (const auto& p : b.foot) o << ' ' << p.x << ',' << p.y;
    o << "\n";
    for (std::size_t i = 0; i < b.plans.size(); ++i) {
        if (b.plans[i].empty()) continue;
        o << "PLAN " << i + 1;
        for (const auto& p : b.plans[i]) o << ' ' << p.x << ',' << p.y;
        o << "\n";
    }
    int n = 0;
    for (const auto& s : b.stories)
        o << "S " << ++n << ' ' << s.h << ' ' << s.mass << ' ' << s.EIx << ' ' << s.EIy << ' ' << s.GAx << ' ' << s.GAy << ' ' << s.kFx << ' ' << s.kFy
          << ' ' << s.Acvx << ' ' << s.Acvy << ' ' << s.Acol << ' ' << s.fc << "\n";
    return o.str();
}

std::vector<double> syntheticAccelerogram(double pgaG, double dt, double duration, unsigned seed, double wg, double zg) {
    const std::size_t n = static_cast<std::size_t>(std::max(1.0, duration / dt)) + 1;
    std::vector<double> a(n, 0.0);
    std::uint32_t st = seed * 2654435761u + 12345u;
    auto rnd = [&] { st = st * 1664525u + 1013904223u; return (st >> 8) / double(1u << 24); };
    const double w0 = 2 * std::numbers::pi * 0.15, w1 = 2 * std::numbers::pi * 25.0;
    const int K = 240;
    const double dw = (w1 - w0) / K;
    for (int k = 0; k < K; ++k) {
        const double w = w0 + (k + rnd()) * dw;
        const double r = w / wg;
        const double S = (1 + 4 * zg * zg * r * r) / ((1 - r * r) * (1 - r * r) + 4 * zg * zg * r * r);  // Kanai–Tajimi
        const double hp = std::pow(w / (2 * std::numbers::pi * 0.4), 4);                                 // past chastota filtri
        const double amp = std::sqrt(S * hp / (1 + hp) * dw), ph = 2 * std::numbers::pi * rnd();
        for (std::size_t i = 0; i < n; ++i) a[i] += amp * std::cos(w * i * dt + ph);
    }
    double mx = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = i * dt;
        const double env = t < 2.0 ? (t / 2.0) * (t / 2.0) : t < 9.0 ? 1.0 : std::exp(-0.35 * (t - 9.0));   // o'rov
        a[i] *= env;
        mx = std::max(mx, std::abs(a[i]));
    }
    const double k = mx > 0 ? pgaG * kG / mx : 0;
    for (auto& v : a) v *= k;
    return a;
}

double pgaForIntensity(double pga9, int I) { return pga9 * std::pow(2.0, I - 9); }

bool SeisResult::ok() const {
    for (const auto& c : checks)
        if (c.ratio >= 1) return false;
    return true;
}

SeisResult evaluateSeismic(const BuildingSeis& b, double pga, char d, int nModes) {
    SeisResult r;
    r.pga = pga;
    const auto st = b.dir(d);
    r.modes = modalAnalysis(st, nModes);
    // spektr shakli 9 ball PGA siga normallashtiriladi, so'ng kerakli PGA ga
    Spectrum sp = b.spectrum;
    const double s0 = sp.pts.empty() ? 1 : sp.pts.front().second;
    const double k = s0 > 0 ? pga / s0 : 1;
    for (auto& p : sp.pts) p.second *= k;
    r.resp = responseSpectrum(st, r.modes, sp, 1.0, "CQC");
    double W = b.Wfound;
    for (const auto& s : st) W += s.mass * kG;
    // perda kesish: (V_el / R) · γn ≤ sig'im
    Check wall{"wallshear", 0, 0};
    for (std::size_t i = 0; i < st.size(); ++i) {
        const auto& s = b.stories[i];
        const double cap = wallShearCapacity(d == 'y' ? s.Acvy : s.Acvx, s.Acol, s.fc, b.rhoH, b.fy);
        const double ratio = cap > 0 ? r.resp.storyShear[i] / b.R * b.gn / cap : 0;
        r.wallRatio.push_back(ratio);
        if (ratio > wall.ratio) { wall.ratio = ratio; wall.story = static_cast<int>(i) + 1; }
    }
    r.checks.push_back(wall);
    // kat siljishi ≤ 1/100
    r.checks.push_back({"drift", r.resp.maxDrift / 0.01, r.resp.maxDriftStory});
    if (b.foot.size() >= 3) {
        const auto sec = polygonSection(b.foot);
        double arm = 0;
        for (const auto& p : b.foot) arm = std::max(arm, std::abs(d == 'y' ? p.y - sec.cy : p.x - sec.cx));
        const double I = d == 'y' ? sec.Ix : sec.Iy;
        // plita chetining ko'tarilishi: M·e/I ≥ W/A
        r.checks.push_back({"uplift", (r.resp.baseMoment * arm / I) / (W / sec.A), 0});
        // ag'darilish: zaxira 1,7 (MTSh 4.1.11)
        r.checks.push_back({"overturn", 1.7 / overturningSafety(W, arm, r.resp.baseMoment, 0.9), 0});
    }
    // siljish: 1,2·V ≤ tanφ·W
    r.checks.push_back({"sliding", 1.2 * r.resp.baseShear / (std::tan(b.phi * std::numbers::pi / 180) * W), 0});
    std::sort(r.checks.begin(), r.checks.end(), [](const Check& a, const Check& c) { return a.ratio > c.ratio; });
    return r;
}

std::vector<std::pair<std::string, double>> limitPga(const BuildingSeis& b, char d) {
    const auto r = evaluateSeismic(b, b.pga9, d);
    std::vector<std::pair<std::string, double>> out;
    for (const auto& c : r.checks) out.emplace_back(c.key, c.ratio > 0 ? b.pga9 / c.ratio : 1e9);
    return out;
}

}  // namespace asiacad::bim::seismic
