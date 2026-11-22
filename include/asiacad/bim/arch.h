#pragma once
// AsiaCAD — arxitektura: aqlli devorlar (eshik/deraza o'rni bilan qayta chiziladi), xonalar (netto / o'qlar bo'yicha /
// ijaraga beriladigan maydon), bino brutto maydoni, kesim va fasad ko'rinishi. Birlik — metr.
// Devor chizmada obyektlar guruhi sifatida saqlanadi: har bo'lakda xdata "aw.wall" (id), "aw.def" (geometriya),
// "aw.ops" (o'rinlar). Shu sababli .acad faylda saqlanadi va buyruqlar bilan qayta yig'iladi.
#include <asiacad/core/document.h>

#include <memory>
#include <string>
#include <vector>

namespace asiacad::bim {

struct ArchOpening {
    char kind = 'D';      // D — eshik, W — deraza
    double pos = 0;       // o'q bo'ylab markaz (devor boshidan), m
    double w = 0.9, h = 2.1, sill = 0;
    bool right = false;   // eshik: oshiq-moshiq o'ng tomonda
    bool out = false;     // eshik ochilishi devorning o'ng tomoniga
};

struct ArchWall {
    std::string id;
    Vec3 a, b;            // o'q
    double t = 0.3, h = 3.0;
    char just = 'C';      // C — o'q markazda, L — devor o'qning chap tomonida, R — o'ng tomonida
    Vec3 la, ra, lb, rb;  // tutashuvdan keyingi burchaklar (chap/o'ng yuz, a va b uchida)
    bool ja = false, jb = false;  // uch boshqa devorga tutashgan (yopuvchi chiziq chizilmaydi)
    std::vector<ArchOpening> ops;
    double length() const { return a.distanceTo(b); }
    // o'qdan chap / o'ng yuzgacha masofa
    double leftOff() const { return just == 'C' ? t / 2 : just == 'L' ? t : 0.0; }
    double rightOff() const { return just == 'C' ? t / 2 : just == 'L' ? 0.0 : t; }
};

// Ketma-ket nuqtalar zanjiri bo'yicha devorlar (burchaklar qiyshiq kesim bilan tutashadi)
std::vector<ArchWall> makeWallChain(const std::vector<Vec3>& pts, double t, double h, char just, bool closed, const std::string& idPrefix);
// T-tutashuv: w ning uchi (atB) boshqa devor o tanasiga tushsa, yuzlari o ning yaqin yuzigacha qisqartiriladi
bool trimEndTo(ArchWall& w, bool atB, const ArchWall& o);
// Yangi devorlarni mavjudlariga (va aksincha) T-tutashtirish; o'zgargan mavjud devorlar indekslari qaytariladi
std::vector<std::size_t> joinWalls(std::vector<ArchWall>& fresh, std::vector<ArchWall>& existing);
// Devor chizmasi: bo'laklar (yopiq polilinlar), eshik/deraza belgilari; xdata bilan
std::vector<std::unique_ptr<IEntity>> wallEntities(const ArchWall& w);
std::string wallDef(const ArchWall& w);
bool parseWall(const std::string& def, const std::string& ops, ArchWall& out);
std::string opsDef(const std::vector<ArchOpening>& ops);
// Chizmadagi devorlar (id bo'yicha)
std::vector<ArchWall> wallsFromDoc(const Document& doc);
// Nuqtaga eng yaqin devor (o'qdan masofa ≤ maxDist); topilmasa -1
int nearestWall(const std::vector<ArchWall>& ws, const Vec3& p, double maxDist, double* along = nullptr);
// Teshik sig'adimi (devor uchlaridan va boshqa teshiklardan kamida 0.1 m)
bool openingFits(const ArchWall& w, const ArchOpening& o, std::string* why = nullptr);

struct RoomResult {
    bool ok = false;
    std::vector<Vec3> axis;   // o'qlar bo'yicha kontur (CCW)
    std::vector<Vec3> net;    // ichki (netto) kontur
    double axisArea = 0, netArea = 0, perimeter = 0;
    Vec3 center;
};
RoomResult findRoom(const std::vector<ArchWall>& ws, const Vec3& p);
// Bino tashqi konturi (o'qlar grafining tashqi chegarasi, devor tashqi yuzi bilan) va brutto maydoni
RoomResult buildingOutline(const std::vector<ArchWall>& ws);
double polygonArea(const std::vector<Vec3>& p);  // ishorali (CCW +)
bool pointInPolygon(const std::vector<Vec3>& poly, const Vec3& p);

// Xona turlari: ijaraga beriladiganmi (yashash, ofis, savdo, mehmonxona)
bool roomRentableByType(const std::string& type);

// 3D: devorlar (teshiklar bilan) va pol plitasi — MeshEntity (qatlam A-3D-WALL, A-3D-SLAB); floors — qavatlar soni
std::vector<std::unique_ptr<IEntity>> archMeshes(const std::vector<ArchWall>& ws, int floors = 1, double slab = 0.2);

struct SectionOptions {
    double depth = 1e9;       // kesim chizig'idan ko'rinadigan chuqurlik (chap tomonga), m
    int floors = 1;
    double slab = 0.2;        // qavatlararo plita, m
    double textH = 0.25;
    std::string title;
};
// Kesim / fasad ko'rinishi: p1→p2 chizig'i, ko'rinish yo'nalishi — chap tomon. origin — chizmadagi chap past (±0.000)
std::vector<std::unique_ptr<IEntity>> sectionEntities(const std::vector<ArchWall>& ws, const Vec3& p1, const Vec3& p2, const Vec3& origin,
                                                      const SectionOptions& o, int* cutCount = nullptr);

}  // namespace asiacad::bim
