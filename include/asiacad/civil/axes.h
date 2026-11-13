#pragma once
// AsiaCAD — aks (koordinata o'qlari) chizish: berilgan oraliqlar bo'yicha to'g'ri burchakli (istalgan burilish burchagida)
// yoki radial (markaz + burchak qadamlari + radius qadamlari) o'qlar; har o'q uchida doirachali marka (raqam / harf),
// o'qlar orasidagi o'lcham zanjiri va umumiy o'lcham (haqiqiy DIMENSION obyektlari — keyin tahrirlanadi).
//   GRIDAXES X=6,6,7.5 Y=8x3 [O=x,y] [ANG=°] [LET=LAT|CYR] [SWAP] [START=1,A] [EXT=m] [R=m] [H=m] [NODIM] [ONE]
//   AXES RAD=r0;dr,dr… ANGS=a0;da,da… [O=x,y] …
#include <asiacad/civil/generators.h>

namespace asiacad::civil {

struct AxesSpec {
    std::vector<double> dx, dy;   // to'g'ri burchakli: o'qlar orasidagi masofalar, m
    double x0 = 0, y0 = 0;        // birinchi o'qlar kesishmasi (yoki radial markaz)
    double angleDeg = 0;          // to'r burilishi
    bool latin = true;            // harflar: A B C … (I, O tashlab) yoki А Б В … (GOST)
    bool swap = false;            // false: X yo'nalishidagi o'qlar raqamli (1, 2 …), Y — harfli
    int numStart = 1, letStart = 0;
    double ext = 2.0;             // o'q chizig'ining eng chetki o'qdan chiqishi
    double bubbleR = 0.5;         // marka doirasi radiusi
    double textH = 0.35;          // marka yozuvi balandligi (0 — avto: 0,7·R)
    bool dims = true;             // o'lcham zanjiri + umumiy o'lcham
    bool bothEnds = true;         // markalar ikki uchda
    // radial
    bool radial = false;
    double r0 = 0;                // birinchi yoy radiusi
    std::vector<double> dr;       // radiuslar qadamlari
    double a0 = 0;                // birinchi radial o'q burchagi, °
    std::vector<double> da;       // burchak qadamlari, °
};

std::string axisLetter(int i, bool latin);   // 0 → "A"; lotinda I va O ishlatilmaydi; 24+ → "A1" …
Entities generateAxes(const AxesSpec& s);
// "X=…", "Y=…" kabi argumentlardan; xato bo'lsa std::invalid_argument (matn — kalit)
AxesSpec parseAxesArgs(const std::vector<std::string>& args);

}  // namespace asiacad::civil
