#pragma once
// AsiaCAD — BIM modeldan smeta (5D): IFC elementlaridan hajm/yuza/uzunlik/son/massa (QTO) ->
// narxlar bazasidagi ish kalimlariga moslash -> to'g'ridan-to'g'ri xarajat (material, ish haqi, mashina),
// ustama, foyda, QQS -> Excel smeta (bo'limlar bo'yicha), hajmlar ro'yxati va moslanmagan elementlar.
//
// Narxlar bazasi — Excel (foydalanuvchi o'z bazasini to'ldiradi), ustunlar tartibi qat'iy (sarlavha tili ixtiyoriy):
//   0 Kod  1 Nomi  2 Bo'lim  3 Birlik  4 O'lchov  5 Koeffitsient  6 Material  7 Ish haqi  8 Mashina
//   9 IFC toifa (WALL|SLAB|…, "|" — yoki)  10 IFC tur (IFCWALL…)  11 Material kaliti  12 Nom kaliti  13 PredefinedType  14 Davom (1 — keyingi kalimlar ham)
// O'lchov: VOLUME|HAJM (m³), AREA|YUZA (yon sirt, bir tomoni, m²), PLAN|REJA (ustki sirt, m²), LENGTH|UZUNLIK (m),
//          COUNT|SONI (dona), MASS|MASSA (armatura, kg), OPENING|TESHIK (deraza/eshik o'lchami eni×bo'yi, m²).
// Ikkinchi varaq — parametrlar (qatorlar tartibi): Nomi, Valyuta, Ustama %, Foyda %, QQS %.
// Kalimlar tartib bilan tekshiriladi: element birinchi mos kelgan kalimga yoziladi (Davom=1 bo'lsa keyingilari ham).
#include <asiacad/bim/ifc.h>
#include <asiacad/io/xlsx.h>

#include <functional>
#include <string>
#include <vector>

namespace asiacad::bim {

enum class SmetaMeasure { Volume, AreaSide, AreaPlan, Length, Count, Mass, Opening };

struct SmetaRule {
    std::string category;    // ifcCategory natijasi: "WALL|SLAB"; bo'sh — har qanday
    std::string ifcType;     // "IFCWALL|IFCWALLSTANDARDCASE"
    std::string material;    // material nomida qism-satr (katta-kichik harf farqsiz): "beton|concrete"
    std::string name;        // nom / ObjectType / Tag da qism-satr
    std::string predefined;  // PredefinedType aniq qiymati
};

struct SmetaItem {
    std::string code, name, section, unit;
    SmetaMeasure measure = SmetaMeasure::Volume;
    double factor = 1.0;                 // miqdorga ko'paytiriladi (isrof 1.02, kg->t 0.001 ...)
    double material = 0, labor = 0, machine = 0;   // birlik narxi
    SmetaRule rule;
    bool cont = false;                   // mos kelsa ham keyingi kalimlarni tekshirish
};

struct SmetaBase {
    std::string title = "Smeta";
    std::string currency = "so'm";
    double overheadPct = 0, profitPct = 0, vatPct = 12;
    std::vector<SmetaItem> items;
};

// Bir element o'lchamlari (IFC Qto bo'lsa — undan, bo'lmasa geometriyadan)
struct SmetaQty {
    double volume = 0, areaSide = 0, areaPlan = 0, length = 0, mass = 0, opening = 0;
    bool fromQto = false;
};
SmetaQty smetaMeasureElement(const IfcElement& e);
double smetaQuantity(const SmetaQty& q, SmetaMeasure m);

struct SmetaLine {
    int item = -1;                       // SmetaBase::items indeksi
    int elements = 0;
    double qty = 0;                      // koeffitsient bilan
    double material = 0, labor = 0, machine = 0, total = 0;
};
struct SmetaUnmatched {
    std::string category;
    int count = 0;
    double volume = 0;
};
struct SmetaElementRow {
    int element = -1;
    int item = -1;                       // -1 — moslanmagan
    double qty = 0;
};
struct SmetaResult {
    std::vector<SmetaLine> lines;        // bazadagi tartibda, faqat miqdori borlari
    std::vector<SmetaUnmatched> unmatched;
    std::vector<SmetaElementRow> rows;
    int elements = 0, matched = 0, fromQto = 0;
    double material = 0, labor = 0, machine = 0, direct = 0, overhead = 0, profit = 0, vat = 0, total = 0;
};

SmetaResult computeSmeta(const IfcModel& m, const SmetaBase& base);

// Namuna baza (narxlar shartli — foydalanuvchi o'z bazasi bilan almashtiradi)
SmetaBase defaultSmetaBase();
using SmetaTr = std::function<std::string(const std::string&)>;
xlsx::Workbook smetaBaseWorkbook(const SmetaBase& b, const SmetaTr& tr);
SmetaBase smetaBaseFromWorkbook(const xlsx::Workbook& wb);      // Error("smeta.err.base") tashlaydi
xlsx::Workbook smetaWorkbook(const IfcModel& m, const SmetaBase& b, const SmetaResult& r, const SmetaTr& tr);
SmetaMeasure smetaMeasureFromText(const std::string& s);        // noma'lum — Volume
const char* smetaMeasureName(SmetaMeasure m);                    // "VOLUME"...

} // namespace asiacad::bim
