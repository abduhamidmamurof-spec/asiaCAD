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

struct Model {
    std::vector<Node> nodes;
    std::map<int, Material> mats;
    std::map<int, Section> secs;
    std::vector<Beam> beams;
    std::map<int, double> mass;                        // tugun → t
    std::map<int, std::array<double, 6>> loads;        // tugun → kN, kN·m
    int index(int nodeId) const;                       // nodes ichidagi o'rni (-1 — yo'q)
};

Model parseModel(const std::string& text);             // xato — std::runtime_error ("fem: line N")
std::string writeModel(const Model& m);

struct BeamForces { std::array<double, 12> f{}; };     // lokal o'qlarda: N1 Vy1 Vz1 T1 My1 Mz1 | N2 Vy2 Vz2 T2 My2 Mz2

struct StaticResult {
    std::vector<std::array<double, 6>> disp;           // tugunlar tartibida
    std::map<int, std::array<double, 6>> reactions;    // mahkamlangan tugunlar
    std::vector<BeamForces> forces;
    double maxDisp = 0;
};
StaticResult solveStatic(const Model& m);              // xato — std::runtime_error (beqaror / bog'lanmagan)

struct ModeShape { double T = 0, omega = 0; std::array<double, 3> mpf{}; std::array<double, 3> meffRatio{}; std::vector<std::array<double, 6>> phi; };
std::vector<ModeShape> solveModes(const Model& m, int count);

// Javob spektri: Sa(T) (m/s²) funksiyasi; dir 0 — X, 1 — Y. CQC (zeta). Natija: tugun siljishlari (m), poydevor kesuvchi kuchi (kN)
struct SpectrumResult { std::vector<std::array<double, 6>> disp; double baseShear = 0; double roof = 0; };
SpectrumResult responseSpectrum(const Model& m, const std::vector<ModeShape>& modes, const std::function<double(double)>& sa, int dir, double zeta = 0.05);

// Ramka generatori: o'qlar oraliqlari (X, Y), qavatlar, balandlik; ustun b×h, rigel b×h, beton E; qavat massasi (t/m²)
Model frameGenerator(const std::vector<double>& dx, const std::vector<double>& dy, int floors, double h, double colB, double colH, double beamB,
                     double beamH, double E, double massPerArea, double gravityPerArea);

}  // namespace asiacad::bim::fem
