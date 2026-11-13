#pragma once
// AsiaCAD — geotexnika: qoziq ko'tarish qobiliyati, sayoz poydevor zamini, cho'kish, plita ostidagi bosim, PIT, suyuqlanish.
//
// Manbalar:
//  * ShNQ 2.01.06-25 / SP 24.13330: burg'ilangan qoziq — Fd = γc·(γcR·R·A + u·Σ γcf·fi·hi);
//    loyiha (Rev C) varianti: Qd = Qs/γRs + Qb/γRu (γRs = 1,5 yon sirt, γRu = 2,0 uch).
//  * Sayoz poydevor zamini: q_u = c·Nc·sc·dc + q·Nq·sq·dq + ½·γ·B·Nγ·sγ·dγ (Vesić koeffitsiyentlari).
//  * Cho'kish — qatlamlab yig'ish: s = β·Σ σzp,i·hi / Ei, σzp = α·p0 (Bussinesk, to'g'ri to'rtburchak markazi),
//    siqiladigan qatlam chegarasi σzp ≤ k·σzg (k = 0,2; kuchsiz grunt — 0,5).
//  * Bikr plita ostidagi bosim: q = N/A ± Mx·y/Ix ± My·x/Iy; siljish — μ·N / V.
//  * PIT (ASTM D5882): L = c·t/2; sifat — c < 3000 m/s past, 3000–3500 o'rta, ≥ 3500 yaxshi (hisobot jadvali).
//  * Suyuqlanish (Seed–Idriss, Youd 2001): CSR = 0,65·amax/g·σv/σv'·rd; CRR7,5((N1)60cs); MSF = 10^2,24 / Mw^2,56.
// Birliklar: m, kN, kPa, MPa (E), kN/m³.
#include <asiacad/bim/seismic.h>

#include <string>
#include <vector>

namespace asiacad::bim::geotech {

// ---- qoziq
struct PileLayer { double h = 0, f = 0; };  // qatlam qalinligi m, yon qarshilik kPa
struct PileInput {
    double D = 1.2, L = 24;                  // diametr, uzunlik, m
    double R = 0;                            // uch ostidagi qarshilik, kPa
    std::vector<PileLayer> side;             // bo'sh — butun uzunlik bo'yicha fAvg
    double fAvg = 0;
    double gRs = 1.5, gRu = 2.0;             // ishonchlilik koeffitsiyentlari (Rev C)
    double gc = 1.0, gcR = 1.0, gcf = 0.7;   // SP 24.13330 ish sharoiti koeffitsiyentlari (burg'ilangan, quruq)
};
struct PileCapacity {
    double A = 0, u = 0;
    double Qs = 0, Qb = 0;                   // chegaraviy yon / uch
    double Qd = 0;                           // hisobiy: Qs/γRs + Qb/γRu
    double Fd = 0;                           // SP 24.13330 formulasi
};
PileCapacity pileCapacity(const PileInput& in);

// ---- sayoz poydevor
struct BearingInput {
    double B = 10, L = 10, Df = 2;           // m
    double c = 0, phiDeg = 30, gamma = 19;   // kPa, °, kN/m³
    double gammaAbove = 19;                  // poydevor tagidan yuqoridagi grunt
    double FS = 3.0;
};
struct Bearing {
    double Nc = 0, Nq = 0, Ng = 0, sc = 1, sq = 1, sg = 1, dc = 1, dq = 1, dg = 1;
    double qult = 0, qall = 0;
};
Bearing bearingCapacity(const BearingInput& in);

// ---- cho'kish
struct SoilLayer { double h = 1, E = 20, gamma = 19; };  // m, MPa, kN/m³ (suv ostida — γ')
struct SettlementInput {
    double B = 10, L = 10, Df = 2;           // poydevor o'lchami, chuqurligi
    double p = 200;                          // tag ostidagi o'rtacha bosim, kPa
    std::vector<SoilLayer> layers;           // poydevor tagidan pastga
    double beta = 0.8, kLimit = 0.2, dz = 0.5;
    double gammaAbove = 19;                  // qazilgan grunt (p0 = p − γ·Df)
};
struct Settlement {
    double s = 0, Hc = 0, p0 = 0;            // m, siqiladigan qatlam qalinligi m
    std::vector<double> z, szp, szg;         // profil
};
double boussinesqCorner(double B, double L, double z);   // to'g'ri to'rtburchak burchagi ostida I_σ
double boussinesqCenter(double B, double L, double z);   // markaz ostida α = 4·I(B/2, L/2)
Settlement settlement(const SettlementInput& in);

// ---- bikr plita
struct Contact { double A = 0, qmin = 0, qmax = 0, qavg = 0; bool uplift = false; };
Contact raftContact(const std::vector<seismic::P2>& outline, double N, double Mx, double My);
double slidingSafety(double N, double V, double phiDeg);

// ---- PIT
struct PitResult { double L = 0, shortfall = 0; int quality = 0; bool flag = false; };  // quality: 0 past, 1 o'rta, 2 yaxshi
PitResult pitCheck(double c, double t, double design, double tolL = 1.5);
int pitQuality(double c);

// ---- suyuqlanish
struct LiqInput { double z = 5, sigV = 95, sigV1 = 70, N160cs = 15, amax = 0.3, Mw = 7.0; };  // amax — g ulushida
struct Liq { double rd = 1, CSR = 0, CRR = 0, MSF = 1, FS = 0; };
Liq liquefaction(const LiqInput& in);

}  // namespace asiacad::bim::geotech
