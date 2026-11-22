#pragma once
// AsiaCAD — 3D sterjenli chekli elementlar (FEM) yechuvchisi: har tugunda 6 erkinlik (ux uy uz rx ry rz), Eyler–Bernulli
// fazoviy rigel/ustun elementi, statik hisob (siljishlar, reaksiyalar, element uchlaridagi kuchlar), modal tahlil
// (subfazoviy iteratsiya, to'plangan massalar) va javob spektri (CQC) — ShNQ 2.01.03-19 spektri bilan.
// Matn formati (.fem):
//   NODE id x y z [FIX=111111]        — 1: erkinlik mahkamlangan (ux uy uz rx ry rz tartibida)
//   MAT id E G                        — kPa (masalan beton B25: E = 3.0e7, G = 1.2e7)
//   SEC id A Iy Iz J                  — m², m⁴ (Iy — lokal y o'qi atrofida, Iz — lokal z atrofida)
//   BEAM id n1 n2 mat sec [VX VY VZ]  — lokal z o'qi uchun yo'naltiruvchi vektor (berilmasa avtomatik)
//   MASS node m                       — t (gorizontal X, Y va vertikal Z)
//   LOAD node fx fy fz [mx my mz]     — kN, kN·m
//   QLOAD beam qx qy qz               — sterjen bo'ylab tekis taqsimlangan yuk, global o'qlarda, kN/m
//   SHELL id n1 n2 n3 n4 mat t        — to'rt burchakli yassi qobiq (MITC4: membrana + plastina), t — qalinlik, m;
//                                       ν = E/(2G) − 1. Lokal z — n1→n3 × n2→n4 yo'nalishida
//   PRESS shell p                     — qobiqqa tik bosim (lokal z bo'yicha), kPa
//   SMASS shell m                     — qobiq massasi, t/m² (tugunlarga teng bo'lib)
#include <asiacad/core/math.h>

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace asiacad::bim::fem {

struct Node { int id = 0; Vec3 p; std::array<bool, 6> fix{}; };
struct Material { double E = 3.0e7, G = 1.25e7; };
struct Section { double A = 0.16, Iy = 2.13e-3, Iz = 2.13e-3, J = 3.6e-3; };
struct Beam { int id = 0, n1 = 0, n2 = 0, mat = 0, sec = 0; Vec3 vz{0, 0, 0}; };
struct Shell { int id = 0; std::array<int, 4> n{}; int mat = 0; double t = 0.2; };

struct Model {
    std::vector<Node> nodes;
    std::map<int, Material> mats;
    std::map<int, Section> secs;
    std::vector<Beam> beams;
    std::map<int, double> mass;                        // tugun → t
    std::map<int, std::array<double, 6>> loads;        // tugun → kN, kN·m
    std::map<int, Vec3> qloads;                        // sterjen id → taqsimlangan yuk (global), kN/m
    std::vector<Shell> shells;
    std::map<int, double> press;                       // qobiq id → bosim (lokal z), kPa
    std::map<int, double> smass;                       // qobiq id → t/m²
    int index(int nodeId) const;                       // nodes ichidagi o'rni (-1 — yo'q)
};

Model parseModel(const std::string& text);             // xato — std::runtime_error ("fem: line N")
std::string writeModel(const Model& m);

struct BeamForces { std::array<double, 12> f{}; };     // lokal o'qlarda: N1 Vy1 Vz1 T1 My1 Mz1 | N2 Vy2 Vz2 T2 My2 Mz2

// Qobiq markazidagi ichki kuchlar (lokal o'qlarda): kN/m va kN·m/m
struct ShellForces { double Nx = 0, Ny = 0, Nxy = 0, Mx = 0, My = 0, Mxy = 0, Qx = 0, Qy = 0; Vec3 ex, ey, ez; };

struct StaticResult {
    std::vector<std::array<double, 6>> disp;           // tugunlar tartibida
    std::map<int, std::array<double, 6>> reactions;    // mahkamlangan tugunlar
    std::vector<BeamForces> forces;
    std::vector<ShellForces> shell;                    // shells tartibida
    double maxDisp = 0;
};
StaticResult solveStatic(const Model& m);              // xato — std::runtime_error (beqaror / bog'lanmagan)

struct ModeShape { double T = 0, omega = 0; std::array<double, 3> mpf{}; std::array<double, 3> meffRatio{}; std::vector<std::array<double, 6>> phi; };
std::vector<ModeShape> solveModes(const Model& m, int count);

// Javob spektri: Sa(T) (m/s²) funksiyasi; dir 0 — X, 1 — Y. CQC (zeta). Natija: tugun siljishlari (m), poydevor kesuvchi kuchi (kN)
struct SpectrumResult {
    std::vector<std::array<double, 6>> disp;
    double baseShear = 0, roof = 0;
    std::vector<BeamForces> forces;   // element uch kuchlari (CQC, mutlaq qiymatlar), beams tartibida
};
SpectrumResult responseSpectrum(const Model& m, const std::vector<ModeShape>& modes, const std::function<double(double)>& sa, int dir, double zeta = 0.05);

// Sterjen bo'ylab ichki kuchlar (lokal): x = 0..L da N, Vy, Vz, T, My, Mz (uch kuchlari va taqsimlangan yukdan)
std::array<double, 6> internalForces(const Model& m, std::size_t beamIndex, const BeamForces& f, double x);
double beamLength(const Model& m, std::size_t beamIndex);

// Temirbeton oldindan o'lchamlash (ShNQ 2.03.01 / SP 63.13330 soddalashtirilgan): ustun — N + qiyshiq egilish (simmetrik armatura,
// chiziqli o'zaro ta'sir), rigel — egilish (αm ≤ αR, kerakli As, μ ≤ 2,5%) va kesuvchi kuch (Q ≤ 0,3·Rb·b·h0).
// Kesim to'g'ri to'rtburchak deb olinadi: b = √(12·Iz/A), h = √(12·Iy/A).
struct RcParams {
    double Rb = 14500, Rbt = 1050;     // kPa (B25)
    double Rs = 435000;                // kPa (A500C)
    double cover = 0.05;               // armatura markazigacha, m
    double rhoMax = 0.04;              // ustunda eng ko'p armatura ulushi
    double alphaR = 0.39;              // rigel: chegaraviy nisbiy balandlik bo'yicha
};
bool rcConcreteClass(const std::string& cls, RcParams& p);   // "B20".."B60"
struct RcCheck {
    bool column = false;
    double b = 0, h = 0, L = 0;
    double N = 0, My = 0, Mz = 0, Q = 0;   // hisobiy (kN, kN·m) — eng noqulay
    double asReq = 0;                      // kerakli armatura, sm² (ustun — jami, rigel — cho'zilgan zona)
    double rho = 0;                        // % (b·h0 yoki b·h ga)
    double util = 0;                       // foydalanish koeffitsienti (>1 — kesim yetmaydi)
};
// Bitta kesim (kuchlar berilgan): N > 0 — siqilish
RcCheck rcSection(bool column, double b, double h, double N, double My, double Mz, double Q, const RcParams& p);
// static — doimiy yuk, seis (bo'lsa) — spektral kuchlar (mutlaq): kombinatsiya static ± seis
std::vector<RcCheck> designRc(const Model& m, const StaticResult& st, const std::vector<BeamForces>* seis, const RcParams& p);

// Ramka + qavat plitalari (qobiq): har oraliq div×div bo'lakka, rigellar ham bo'linadi; og'irlik — plitaga bosim (PRESS),
// massa — SMASS. slabT — plita qalinligi, m
Model frameWithSlabs(const std::vector<double>& dx, const std::vector<double>& dy, int floors, double h, double colB, double colH, double beamB,
                     double beamH, double E, double massPerArea, double gravityPerArea, double slabT, int div);

// Ramka generatori: o'qlar oraliqlari (X, Y), qavatlar, balandlik; ustun b×h, rigel b×h, beton E; qavat massasi (t/m²)
Model frameGenerator(const std::vector<double>& dx, const std::vector<double>& dy, int floors, double h, double colB, double colH, double beamB,
                     double beamH, double E, double massPerArea, double gravityPerArea, bool lineLoads = false);
// lineLoads — og'irlik yuki rigellarga taqsimlangan (QLOAD, har panel yukining yarmi X, yarmi Y rigellariga), aks holda tugunlarga

}  // namespace asiacad::bim::fem
