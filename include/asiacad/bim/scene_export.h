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

struct Scene {
    std::vector<SceneMesh> meshes;        // material bo'yicha bittadan
    std::vector<SceneMarker> markers;
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
// Markazni (XY o'rtasi, eng past Z) origin ga yozadi
void centerScene(Scene& s);

// Yozuvchilar (xato bo'lsa Error "export.err.write")
void writeObj(const Scene& s, const std::string& objPath);           // .obj + yonida .mtl
void writeGltf(const Scene& s, const std::string& gltfPath);         // .gltf + yonida .bin
void writeMarkersCsv(const Scene& s, const std::string& csvPath);    // kind;x;y;z;height;rot

// Quyosh holati (NOAA soddalashtirilgan): kenglik/uzunlik (°), mahalliy vaqt, UTC siljishi → azimut (shimoldan, soat yo'nalishi), balandlik (°)
struct SunPos { double azimuthDeg = 0, elevationDeg = 0; };
SunPos sunPosition(double latDeg, double lonDeg, int year, int month, int day, double localHour, double utcOffset);

}  // namespace asiacad::bim
