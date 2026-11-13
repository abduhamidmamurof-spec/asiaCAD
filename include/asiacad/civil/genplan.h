#pragma once
// AsiaCAD — genplan (vaziyat rejasi) dan bitta buyruq bilan obodonlashtirish chizmasi:
//  1) zonalar: yopiq polilin va shtrix konturlari qatlam nomi bo'yicha tasniflanadi (yo'l, trotuar, yashil, avtoturargoh,
//     bino, maydoncha, rampa, yo'lak); har qatlam uchun foydalanuvchi o'zgartirishi (MAP=qatlam:ZONA) ustun;
//  2) bordyurlar: zonalar chegarasi qo'shni zonaga qarab — yo'l bordyuri (BR 100.30.15) yoki bog' bordyuri (BR 100.20.8);
//     har chegara bir marta (qattiq tomondan) chiziladi, ketma-ket qirralar bitta polilinga birlashadi, ikki chiziq (eni);
//  3) avtoturargoh joylari: har avtoturargoh zonasida qirralar bo'ylab 2,5×5,0 m joylar (to'liq zona ichida, ustma-ust emas);
//  4) metraj: bordyur uzunliklari turlari bo'yicha, zonalar maydonlari, joylar soni (Excel ham).
// Natija chizmaning alohida qatlamlarida (GP_*) — keyin qo'lda tuzatiladi; qayta ishga tushirish oldingi GP_* larni almashtiradi.
#include <asiacad/civil/generators.h>

#include <map>

namespace asiacad::civil {

enum class GpZone { Unknown = 0, Road, Sidewalk, Green, Parking, Building, Playground, Ramp, Path };
const char* gpZoneKey(GpZone z);                 // "ROAD", "SIDEWALK" …
bool gpZoneFromKey(const std::string& k, GpZone& out);

struct GpRule { std::string pattern; GpZone zone; };   // qatlam nomida (katta-kichik harf farqsiz) uchrasa
std::vector<GpRule> gpDefaultRules();
GpZone gpClassifyLayer(const std::string& layer, const std::vector<GpRule>& rules);

struct GpPoint { double x = 0, y = 0; };
struct GpPoly {
    GpZone zone = GpZone::Unknown;
    std::string layer;
    std::vector<GpPoint> outer;
    std::vector<std::vector<GpPoint>> holes;
    double area = 0;
};

struct GpCurb { std::string name; double width; };
struct GenplanSpec {
    std::vector<GpRule> rules = gpDefaultRules();
    std::map<std::string, GpZone> overrides;     // qatlam → zona (inson tuzatishi)
    GpCurb roadCurb{"BR 100.30.15", 0.15};
    GpCurb gardenCurb{"BR 100.20.8", 0.08};
    bool curbs = true, parking = true;
    double stallW = 2.5, stallL = 5.0;
    double minArea = 0.5;                         // undan kichik konturlar e'tiborsiz
};

struct GenplanResult {
    Entities entities;
    std::vector<GpPoly> zones;
    double roadCurbLen = 0, gardenCurbLen = 0;
    int stalls = 0;
    std::map<GpZone, double> area;
    std::map<std::string, std::pair<GpZone, int>> layers;   // qatlam → (zona, kontur soni)
};

// Chizmadan zonalarni yig'ish (GP_* qatlamlari e'tiborsiz)
std::vector<GpPoly> gpExtractZones(const Document& doc, const GenplanSpec& s, std::map<std::string, std::pair<GpZone, int>>* layers = nullptr);
GenplanResult generateGenplan(const Document& doc, const GenplanSpec& s);
// "qatlam:ROAD,qatlam2:GREEN" → overrides; xato bo'lsa std::invalid_argument
std::map<std::string, GpZone> gpParseMap(const std::string& text);

}  // namespace asiacad::civil
