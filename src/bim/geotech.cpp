// AsiaCAD — geotexnika formulalari (qoziq, sayoz poydevor, cho'kish, plita bosimi, PIT, suyuqlanish).
#include <asiacad/bim/geotech.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace asiacad::bim::geotech {

namespace {
constexpr double kPi = std::numbers::pi;
double rad(double d) { return d * kPi / 180.0; }
}  // namespace

PileCapacity pileCapacity(const PileInput& in) {
    PileCapacity r;
    r.A = kPi * in.D * in.D / 4;
    r.u = kPi * in.D;
    double side = 0;
    if (in.side.empty()) side = in.fAvg * in.L;
    else for (const auto& l : in.side) side += l.f * l.h;
    r.Qs = r.u * side;
    r.Qb = in.R * r.A;
    r.Qd = r.Qs / in.gRs + r.Qb / in.gRu;
    r.Fd = in.gc * (in.gcR * in.R * r.A + r.u * in.gcf * side);
    return r;
}

Bearing bearingCapacity(const BearingInput& in) {
    Bearing b;
    const double ph = rad(in.phiDeg), t = std::tan(ph);
    b.Nq = std::exp(kPi * t) * std::pow(std::tan(kPi / 4 + ph / 2), 2);
    b.Nc = in.phiDeg > 0.01 ? (b.Nq - 1) / t : 5.14;
    b.Ng = 2 * (b.Nq + 1) * t;
    const double BL = std::min(in.B, in.L) / std::max(in.B, in.L);
    const double B = std::min(in.B, in.L);
    b.sc = 1 + BL * b.Nq / b.Nc;
    b.sq = 1 + BL * t;
    b.sg = std::max(0.6, 1 - 0.4 * BL);
    const double k = in.Df / B <= 1 ? in.Df / B : std::atan(in.Df / B);
    b.dq = 1 + 2 * t * std::pow(1 - std::sin(ph), 2) * k;
    b.dc = in.phiDeg > 0.01 ? b.dq - (1 - b.dq) / (b.Nc * t) : 1 + 0.4 * k;
    b.dg = 1;
    const double q = in.gammaAbove * in.Df;
    b.qult = in.c * b.Nc * b.sc * b.dc + q * b.Nq * b.sq * b.dq + 0.5 * in.gamma * B * b.Ng * b.sg * b.dg;
    b.qall = b.qult / in.FS;
    return b;
}

double boussinesqCorner(double B, double L, double z) {
    if (z <= 1e-9) return 0.25;
    const double m = B / z, n = L / z, m2 = m * m, n2 = n * n, s = std::sqrt(m2 + n2 + 1);
    const double a = 2 * m * n * s / (m2 + n2 + m2 * n2 + 1) * (m2 + n2 + 2) / (m2 + n2 + 1);
    double at = std::atan2(2 * m * n * s, m2 + n2 + 1 - m2 * n2);
    if (at < 0) at += kPi;
    return (a + at) / (4 * kPi);
}
double boussinesqCenter(double B, double L, double z) { return 4 * boussinesqCorner(B / 2, L / 2, z); }

Settlement settlement(const SettlementInput& in) {
    Settlement r;
    r.p0 = std::max(0.0, in.p - in.gammaAbove * in.Df);
    double z = 0, szg = in.gammaAbove * in.Df;
    for (const auto& l : in.layers) {
        const int n = std::max(1, static_cast<int>(std::ceil(l.h / in.dz)));
        const double hz = l.h / n;
        for (int i = 0; i < n; ++i) {
            const double zm = z + hz / 2;
            const double szp = boussinesqCenter(in.B, in.L, zm) * r.p0;
            const double g = szg + l.gamma * hz / 2;
            r.z.push_back(zm); r.szp.push_back(szp); r.szg.push_back(g);
            if (szp <= in.kLimit * g) { r.Hc = z; return r; }
            r.s += in.beta * szp * hz / (l.E * 1000.0);
            z += hz;
            szg += l.gamma * hz;
        }
    }
    r.Hc = z;
    return r;
}

Contact raftContact(const std::vector<seismic::P2>& outline, double N, double Mx, double My) {
    Contact c;
    const auto s = seismic::polygonSection(outline);
    c.A = s.A;
    c.qavg = N / s.A;
    c.qmin = 1e300; c.qmax = -1e300;
    for (const auto& p : outline) {
        const double q = N / s.A + Mx * (p.y - s.cy) / s.Ix + My * (p.x - s.cx) / s.Iy;
        c.qmin = std::min(c.qmin, q);
        c.qmax = std::max(c.qmax, q);
    }
    c.uplift = c.qmin < 0;
    return c;
}
double slidingSafety(double N, double V, double phiDeg) { return V > 0 ? N * std::tan(rad(phiDeg)) / V : 1e9; }

int pitQuality(double c) { return c < 3000 ? 0 : c < 3500 ? 1 : 2; }
PitResult pitCheck(double c, double t, double design, double tolL) {
    PitResult r;
    r.L = c * t / 2;
    r.shortfall = design - r.L;
    r.quality = pitQuality(c);
    r.flag = r.shortfall > tolL || r.quality == 0;
    return r;
}

Liq liquefaction(const LiqInput& in) {
    Liq r;
    r.rd = in.z <= 9.15 ? 1 - 0.00765 * in.z : 1.174 - 0.0267 * in.z;
    r.CSR = 0.65 * in.amax * in.sigV / in.sigV1 * r.rd;
    const double N = in.N160cs;
    r.CRR = N >= 30 ? 2.0 : 1 / (34 - N) + N / 135 + 50 / std::pow(10 * N + 45, 2) - 1.0 / 200;
    r.MSF = std::pow(10.0, 2.24) / std::pow(in.Mw, 2.56);
    r.FS = r.CSR > 0 ? r.CRR * r.MSF / r.CSR : 1e9;
    return r;
}

}  // namespace asiacad::bim::geotech
