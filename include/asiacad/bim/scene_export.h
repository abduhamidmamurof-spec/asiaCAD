#pragma once
// AsiaCAD — 3D sahna eksporti renderlar uchun (D5 Render, Lumion, Twinmotion, Unreal, 3ds Max):
//  * material bo'yicha guruhlangan uchburchaklar (standart nomlar "AC_Beton", "AC_Shisha" … — render dasturida bir marta moslanadi);
//  * OBJ + MTL (barcha dasturlar) va glTF 2.0 (.gltf + .bin, PBR; Y yuqoriga) yozuvchilari;
//  * daraxt / odam / avto joylari (markerlar, CSV) — render dasturining o'z kutubxonasidan real aktivlar qo'yish uchun;
//  * kamera va quyosh (Toshkent, sana/soat) — JSON.
#include <asiacad/bim/building.h>
#include <asiacad/bim/ifc.h>

#include <array>
#include <map>
#include <string>
#include <vector>

namespace asiacad::bim {

struct SceneMaterial {
    std::string name;            // "AC_Beton"
    std::array<double, 3> rgb{0.7, 0.7, 0.7};
    double roughness = 0.8, metallic = 0.0, opacity = 1.0;
};
// Standart materiallar kutubxonasi (nom → PBR qiymatlar)
const std::vector<SceneMaterial>& sceneMaterials();
const SceneMaterial& sceneMaterial(const std::string& name);

struct SceneMesh {
    std::string material;
    std::vector<std::array<Vec3, 3>> tris;
};

struct SceneMarker {
    std::string kind;            // "TREE", "SHRUB", "PERSON", "CAR"
    Vec3 p;
    double height = 0, rotDeg = 0;
};

struct SceneLabel {               // Unreal: lot/bino yorliqlari (chizmadagi matnlardan)
    std::string text;
    Vec3 p;
};

struct Scene {
    std::vector<SceneMesh> meshes;        // material bo'yicha bittadan
    std::vector<SceneMarker> markers;
    std::vector<SceneLabel> labels;
    Vec3 origin{0, 0, 0};                 // eksportda ayiriladi (katta koordinatalar render dasturlarida titraydi)
    void add(const std::string& material, const std::array<Vec3, 3>& t);
    std::size_t triangleCount() const;
};

// Prizma (kontur + teshiklar, z0..z1) → yopiq jism
void addPrism(Scene& s, const std::string& material, const std::vector<Vec3>& outline, const std::vector<std::vector<Vec3>>& holes, double z0, double z1);
// Bino modeli: element turi → material (beton / gazoblok / shisha …)
void addBuilding(Scene& s, const BuildingModel& m);
// IFC modeli: toifa va rang bo'yicha material
void addIfc(Scene& s, const IfcModel& m);
// Render uchun IFC eksporti sozlamalari:
//  shell — faqat tashqi qobiq (fasad): har qavatda elementlar rejasi to'rga (0.4 m) yoziladi, tashqaridan to'ldirish (flood fill)
//          bilan tashqi bo'shliq topiladi; unga 0.8 m dan yaqin elementlar qoladi. Mebel, muhandislik jihozlari, xonalar — tashlanadi.
//          Revit kvartira blokida uchburchaklar ~10 barobar kamayadi — yuzlab nusxa render dasturiga sig'adi.
//  colors — material IFC rangi bo'yicha ("IFC_RRGGBB"; render dasturida rang bo'yicha moslash), aks holda AC_* toifalar
struct IfcExportOptions {
    bool shell = false;
    bool colors = false;
};
void addIfc(Scene& s, const IfcModel& m, const IfcExportOptions& o);
// Tashqi qobiq niqobi (elements bilan parallel): true — tashqaridan ko'rinadi
std::vector<bool> ifcShellMask(const IfcModel& m, double cell = 0.4);
// Markazni (XY o'rtasi, eng past Z) origin ga yozadi
void centerScene(Scene& s);

// Yozuvchilar (xato bo'lsa Error "export.err.write")
void writeObj(const Scene& s, const std::string& objPath);           // .obj + yonida .mtl
void writeGltf(const Scene& s, const std::string& gltfPath);         // .gltf + yonida .bin
void writeMarkersCsv(const Scene& s, const std::string& csvPath);    // kind;x;y;z;height;rot

// 3ds Max uchun MaxScript (Corona / V-Ray / standart Physical): OBJ importi, fizik materiallar (AC_* nomlari bo'yicha),
// quyosh + osmon (sana/soat bo'yicha yo'nalish), ikki kamera (inson ko'zi balandligi va dron), markerlar o'rniga nuqtalar
// (AC_TREE_…, balandlik nomda) — yonidagi aktivlar jadvali (kind;fayl.max) bo'lsa proksi obyektlar shu nuqtalarga nusxalanadi;
// oxirida render buyrug'i (3dsmaxcmd) izohda.
enum class MaxRenderer { Corona, VRay, Physical };
struct MaxScriptOptions {
    MaxRenderer renderer = MaxRenderer::Corona;
    double sunAzimuthDeg = 220, sunElevationDeg = 35;
    std::string objFile;              // import qilinadigan OBJ (skript yonida, nisbiy nom)
    std::string assetsCsv;            // aktivlar jadvali nomi (kind;fayl.max;masshtab_balandligi), bo'lmasa — faqat nuqtalar
    int width = 3840, height = 2160;
};
bool maxRendererFromKey(const std::string& key, MaxRenderer& out);
void writeMaxScript(const Scene& s, const std::string& msPath, const MaxScriptOptions& o);

// Unreal Engine 5 uchun Python skripti (Editor: Tools → Execute Python Script): glTF importi (Interchange), AC_* materiallar —
// bitta asosiy materialdan instansiyalar (rang, g'adir-budurlik, metall, shisha — shaffof), SunSky (Toshkent, sana/soat),
// osmon atmosferasi, hajmiy bulutlar, balandlik tumani, post-process; ikki CineCamera (odam ko'zi, dron); markerlar o'rniga
// aktivlar jadvalidagi (kind;/Game/... mesh yo'li;balandlik) Static Mesh lar — har tur bitta ISM (instanced) aktorida;
// yorliqlar — TextRender aktorlari (lot nomlari); Cesium bo'lsa — georeferens (GEO=lat,lon) va Google 3D shahar plitalari.
// Koordinatalar: UE = (x, −y, z) · 100 sm (glTF importi bilan bir xil).
struct UnrealOptions {
    std::string gltfFile;             // skript yonidagi glTF (nisbiy nom)
    std::string assetsCsv;            // kind;/Game/…/SM_Tree.SM_Tree;balandlik_m
    double sunAzimuthDeg = 220, sunElevationDeg = 35;
    double lat = 41.3111, lon = 69.2797;   // Toshkent (Cesium georeferensi va SunSky)
    int year = 2026, month = 6, day = 21;
    double hour = 16;
    double originAltitude = 450;           // m — dengiz sathidan (Cesium uchun)
};
void writeUnrealScript(const Scene& s, const std::string& pyPath, const UnrealOptions& o);

// Quyosh holati (NOAA soddalashtirilgan): kenglik/uzunlik (°), mahalliy vaqt, UTC siljishi → azimut (shimoldan, soat yo'nalishi), balandlik (°)
struct SunPos { double azimuthDeg = 0, elevationDeg = 0; };
SunPos sunPosition(double latDeg, double lonDeg, int year, int month, int day, double localHour, double utcOffset);

}  // namespace asiacad::bim
