#pragma once
// AsiaCAD — arxitektura (АР) chizmalaridan bitta buyruq bilan 3D bino (yo'l xaritasi, 3-band).
//
// Bitta DWG/DXF faylida qavat rejalari va kesimlar model fazosida yonma-yon (har biri o'z masshtabida):
//  * Ko'rinishlar — o'q pufakchalari to'plami (KJ bilan bir xil: detectKjViews), "1"/"А" o'qlari bo'yicha ustma-ust tekislanadi.
//  * Reja sarlavhasidan qavat(lar): "План 1-го этажа", "План типового этажа (2-9)", "3-qavat rejasi", "2-5 qavatlar rejasi",
//    "План 2-9 этажей", "Floor plan 3". Yerto'la, tom va boshqa ko'rinishlar 3D ga kirmaydi.
//  * Kesim / fasad ("Разрез 1-1", "Kesim", "Фасад") belgilaridan qavat balandligi va qavatlar soni: ±0.000 dan boshlab
//    teng qadamli zanjir (+3.300, +6.600, …) — oxirgi a'zo tom plitasi usti.
//  * Har reja o'z qatlamlari bo'yicha (autoLayerMap: devor, ustun, to'siq devor, zinapoya, lift …) PlanInput ga aylanadi;
//    berilmagan qavatlar uchun pastdagi eng yaqin reja (tipik) ishlatiladi. Natija — buildModel (beton, to'siq devor, derazalar).
//  * Deraza / eshik bloklari ("ОК-1", "ДВ-2", "WIN", "DOOR", "DERAZA", "ESHIK") — qavat bo'yicha sanaladi (hisobot).
#include <asiacad/bim/building.h>
#include <asiacad/bim/kj_project.h>

namespace asiacad::bim {

enum class ArViewKind { Plan, Section, Facade, Roof, Basement, Other };
const char* arViewKey(ArViewKind k);  // i18n: ar.view.<key>

struct ArView {
    ArViewKind kind = ArViewKind::Other;
    std::string title;
    int floorFrom = 0, floorTo = 0;       // reja: qavatlar (1-asosli); 0 — aniqlanmagan
    BBox box;                             // chizma koordinatasida
    double scale = 1;                     // chizma birligi -> m (DIMLFAC × birlik)
    Vec3 origin;                          // "1"/"А" kesishmasi (chizmada)
    bool aligned = false;
    std::vector<double> levels;           // ko'rinishdagi belgilar (nisbiy, m)
    int walls = 0, columns = 0, infill = 0, windows = 0, doors = 0;   // reja: topilgan elementlar
    bool use = true;
};

struct ArParams {
    int floors = 0;                       // 0 — kesimdan / rejalardan
    double floorHeight = 0;               // 0 — kesimdan (topilmasa 3.0)
    BuildingParams building;              // qolgan parametrlar (plita, fundament, fasad …)
};

struct ArProject {
    std::vector<ArView> views;
    int floors = 0;
    double floorHeight = 0;
    bool heightFromSection = false;       // qavat balandligi kesim / fasad belgilaridan
    std::vector<double> sectionLevels;    // kesim / fasaddagi barcha belgilar (o'sish tartibida)
    std::map<int, int> windowsByFloor, doorsByFloor;
    BuildingParams params;                // qo'llanilgan
    BuildingModel bm;
    std::vector<BuildingIssue> issues;    // ar.chk.* (bm.issues dan tashqari)
};

// Sarlavhadan ko'rinish turi va qavatlar oralig'i ("План типового этажа (2-9)" -> Plan, 2..9)
ArViewKind classifyArTitle(const std::string& title, int& floorFrom, int& floorTo);
// Belgilar ro'yxatidan qavat balandligi va qavatlar soni: ±0.000 dan teng qadamli zanjir (qadam 2.4…6.0 m)
bool floorChain(const std::vector<double>& levels, double& height, int& floors);

std::vector<ArView> detectArViews(const Document& doc, const std::vector<DxfDimInfo>& dims);
ArProject buildArProject(const Document& doc, const std::vector<DxfDimInfo>& dims, const ArParams& p,
                         const std::vector<ArView>* views = nullptr);

} // namespace asiacad::bim
