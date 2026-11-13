#pragma once
// AsiaCAD — seysmik hisob (yuqori qavatli monolit bino, konsol "egilish + siljish + rama" modeli).
//
// Manbalar (formulalar shu hujjatlardan; band raqamlari izohlarda):
//  * QMQ 2.01.03-19 "Seysmik hududlarda qurilish" — 2.6 (chiziqli-spektral usul), 2.18 (2.6) formulasi (η),
//    2.22 (noelastik deformatsiya koeffitsiyenti), 2.13 (hisobiy seysmik yuklama).
//  * MTSh (СТУ) 4.2.14: S_ik = K0·Kп·Kэт·S0_ik,  S0_ik = α·Q_k·W_i·Kδ·η_ik;  4.2.13: dinamik modullar Vs/Vp dan;
//    4.1.8 — chegaraviy nisbiy deformatsiya 1/70 (tavsiya 1/100); 4.1.11 — ag'darilishga zaxira 1,7.
//  * Seysmologiya instituti hisobotlari: maydon spektri PSA(T) (5% so'nish) va sintetik akselerogrammalar (Ilova-1).
//  * Seysmik qattiqliklar usuli (Medvedev): ΔI = 1,67·lg(ρ0·V0 / (ρ·V)).
//
// Birliklar: uzunlik m, massa t, kuch kN, kuchlanish kPa, vaqt s, tezlanish m/s² (spektr — g ulushida).
#include <string>
#include <utility>
#include <vector>

namespace asiacad::bim::seismic {

constexpr double kG = 9.81;

struct P2 { double x = 0, y = 0; };

// Ko'pburchak kesim (teshiklar bilan): A, og'irlik markazi, markaz bo'yicha inersiya momentlari.
//   Ix = ∫y²dA (x o'qi atrofida — y yo'nalishidagi egilish), Iy = ∫x²dA
struct Section {
    double A = 0, cx = 0, cy = 0, Ix = 0, Iy = 0;
};
Section polygonSection(const std::vector<P2>& outer, const std::vector<std::vector<P2>>& holes = {});
// Bir nechta kesimni birlashtirish (umumiy og'irlik markaziga Shteyner)
Section combine(const std::vector<Section>& parts);

// Qavat (pastdan yuqoriga). Qavat massasi uning ustki sathida to'plangan.
struct Story {
    double h = 3.0;         // qavat balandligi, m
    double mass = 0;        // t
    double EI = 0;          // perdalar egilish bikrligi (yo'nalish bo'yicha), kN·m²
    double GA = 0;          // perdalar siljish bikrligi, kN (0 — hisobga olinmaydi)
    double kFrame = 0;      // rama (ustun-rigel) qavat siljish bikrligi, kN/m
};
// Rama qavat bikrligi: Σ 12·E·Ic·a / h³ (Muto; a ≈ k̄/(2+k̄), rigellar bikr bo'lsa → 1)
double frameStoryStiffness(double E, const std::vector<double>& Ic, double h, double a = 0.4);

struct Mode {
    double T = 0;                 // davr, s
    double gamma = 0;             // ishtirok koeffitsiyenti Γ = φᵀM1 / φᵀMφ
    double meff = 0;              // samarali massa, t
    std::vector<double> phi;      // shakl (qavatlar sathida)
};
// Konsol modeli: Timoshenko elementlari + rama prujinalari, burilish erkinliklari kondensatsiya qilinadi.
std::vector<Mode> modalAnalysis(const std::vector<Story>& st, int nModes = 6);
// Simmetrik xos qiymatlar (Yakobi). A — n×n qator bo'yicha; natija o'sish tartibida, vektorlar ustunlarda.
void jacobiEigen(std::vector<double> A, int n, std::vector<double>& w, std::vector<double>& V);

// Spektr: (T, Sa/g) jadvali, T bo'yicha log-chiziqli; jadvaldan keyin — doimiy siljish (Sa ~ 1/T²).
struct Spectrum {
    std::vector<std::pair<double, double>> pts;
    double sa(double T) const;  // g ulushida
};
// Seysmologiya instituti, Toshkent (Lot-16), II toifa grunt: 475 / 1000 / 2475 / 5000 yil (P = 0,90…0,99)
Spectrum siteSpectrumLot16(int returnPeriod);

struct Response {
    std::vector<double> disp, drift, storyShear, force;  // qavatlar bo'yicha
    double baseShear = 0, baseMoment = 0, roof = 0, maxDrift = 0;
    int maxDriftStory = 0;  // 1 dan
};
// Chiziqli-spektral usul (QMQ 2.01.03-19 2.6). R — kamaytirish (1/K1); comb: "SRSS" yoki "CQC" (Der Kiureghian).
Response responseSpectrum(const std::vector<Story>& st, const std::vector<Mode>& modes, const Spectrum& sp, double R = 1.0,
                          const std::string& comb = "CQC", double zeta = 0.05);

// MTSh 4.2.14 / QMQ 2.01.03-19 (2.6): i-shakl uchun k-nuqtadagi seysmik kuch, kN.
struct KmkParams {
    double K0 = 1.0, Kp = 1.2, Ket = 1.5, alpha = 0.5, Kdelta = 1.1;
    double W = 0.2;  // spektral koeffitsiyent (MTSh: 0,2) — yoki spektrdan
};
std::vector<double> kmkModeForces(const std::vector<Story>& st, const Mode& m, const KmkParams& p);
// η_ik = X_i(x_k) · Σ Q_j·X_i(x_j) / Σ Q_j·X_i²(x_j)  (QMQ 2.01.03-19, 2.6 formula)
std::vector<double> etaCoefficients(const std::vector<Story>& st, const Mode& m);

// Vaqt sohasi: Nyumark (o'rtacha tezlanish, γ=1/2, β=1/4) — modal superpozitsiya
std::vector<double> newmarkSdof(double T, double zeta, const std::vector<double>& ag, double dt);
struct History {
    double dt = 0;
    std::vector<std::vector<double>> disp;  // [vaqt][qavat], m
    std::vector<double> baseShear;          // kN
    double roofMax = 0, driftMax = 0, baseShearMax = 0;
    std::vector<double> driftEnv;           // qavat bo'yicha maksimum
};
History timeHistory(const std::vector<Story>& st, const std::vector<Mode>& modes, const std::vector<double>& ag, double dt, double zeta = 0.05);
// Akselerogramma matni: ustunlar "t a1 t a2 …" (sm/s²) — column (0 dan) bo'yicha m/s² ga; dt — birinchi ikki vaqtdan
std::vector<double> readAccelerogram(const std::string& text, int column, double* dt = nullptr);
// Sun'iy akselerogramma (m/s²): Kanai–Tajimi spektri (ωg, ζg) bo'yicha tasodifiy fazali sinusoidalar yig'indisi,
// Saragoni–Hart tipidagi o'rov; eng katta qiymati pgaG·g ga keltiriladi. seed — takrorlanuvchi natija uchun.
std::vector<double> syntheticAccelerogram(double pgaG, double dt = 0.01, double duration = 20.0, unsigned seed = 9,
                                          double wg = 15.0, double zg = 0.6);

// MTSh 4.2.13: ν = (Vp² − 2Vs²) / (2(Vp² − Vs²)),  E = 2ρVs²(1+ν)  (ρ t/m³, V m/s → kPa)
double dynamicPoisson(double vp, double vs);
double dynamicModulus(double rho, double vs, double nu);
// Seysmik qattiqliklar usuli: intensivlik orttirmasi, ball
double intensityIncrement(double rho0, double v0, double rho, double v);

// Temirbeton devor (perda) kesishdagi ko'tarish qobiliyati, kN:
//   φ·[Acv·(0,17·√f'c + ρh·fy) + 0,8·Acol·0,17·√f'c],  chegarasi Acv·0,66·√f'c (MPa → kN: ×1000)
double wallShearCapacity(double Acv, double Acol, double fcMPa, double rhoH, double fyMPa, double phi = 0.75);
// ρh — talab qilinadigan gorizontal armatura ulushi (V kN)
double requiredRhoH(double V, double Acv, double Acol, double fcMPa, double fyMPa, double phi = 0.75);
// Ag'darilish: ushlab turuvchi moment / ag'daruvchi (MTSh 4.1.11 talabi ≥ 1,7)
double overturningSafety(double W, double arm, double M, double reduce = 0.9);

}  // namespace asiacad::bim::seismic

namespace asiacad::bim::seismic {

// ---- Bino seysmik modeli fayli (.seis, matn): satrlar
//   NAME <nom>
//   PGA9 0.518                 9 ball uchun maydon PGA si, g (MSK-64: har ball ×2)
//   SPECTRUM T:Sa T:Sa ...     maydon spektri (Sa — g); yo'q bo'lsa siteSpectrumLot16(5000)
//   FOOT x,y x,y ...           poydevor plitasi konturi (m)
//   WFOUND <kN>                poydevor og'irligi (ag'darilish/ko'tarilish uchun)
//   PHI <°>                    plita ostidagi to'ldirma ishqalanish burchagi (siljish)
//   R <qiymat>  GN <qiymat>    kamaytirish koeffitsiyenti (1/K1) va mas'uliyat koeffitsiyenti γn
//   S n h mass EIx EIy GAx GAy kFx kFy Acvx Acvy Acol fc     qavatlar (pastdan), birliklar: m t kN·m² kN kN/m m² m² m² MPa
struct BuildingSeis {
    std::string name;
    double pga9 = 0.518, Wfound = 0, phi = 35, R = 4.0, gn = 1.2, rhoH = 0.0025, fy = 400;
    Spectrum spectrum;
    std::vector<P2> foot;
    std::vector<std::vector<P2>> plans;     // qavat rejasi konturi (PLAN n …); bo'sh — FOOT
    struct S { double h, mass, EIx, EIy, GAx, GAy, kFx, kFy, Acvx, Acvy, Acol, fc; };
    std::vector<S> stories;
    std::vector<Story> dir(char d) const;   // 'x' yoki 'y'
};
BuildingSeis readBuildingSeis(const std::string& text);   // xato — std::runtime_error
std::string writeBuildingSeis(const BuildingSeis& b);

double pgaForIntensity(double pga9, int I);                // MSK-64: pga9 · 2^(I−9)

struct Check {
    std::string key;     // "uplift", "wallshear", "overturn", "sliding", "drift"
    double ratio = 0;    // talab / sig'im (≥ 1 — chegara oshdi)
    int story = 0;       // devor/siljish uchun qavat (1 dan)
};
struct SeisResult {
    double pga = 0;
    std::vector<Mode> modes;
    Response resp;                 // elastik (R = 1)
    std::vector<double> wallRatio; // qavat bo'yicha perda kesish ishlatilishi
    std::vector<Check> checks;     // kamayish tartibida
    bool ok() const;
};
SeisResult evaluateSeismic(const BuildingSeis& b, double pga, char dir, int nModes = 6);
// Har bir tekshiruv uchun chegara PGA (g): ratio(pga)=1 bo'ladigan qiymat (chiziqli)
std::vector<std::pair<std::string, double>> limitPga(const BuildingSeis& b, char dir);

}  // namespace asiacad::bim::seismic
