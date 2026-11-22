#pragma once
// AsiaCAD — IFC4 eksport (ISO 16739, STEP): AsiaCAD modeli BIM sifatida topshirish uchun (Revit, ArchiCAD, Tekla,
// BIMcollab, Solibri va boshqa dasturlarda ochiladi).
//  * fazoviy tuzilma: loyiha -> uchastka -> bino -> qavatlar (belgi bilan)
//  * bino modeli (ARMODEL / Monolit bino): plita, devor, ustun, fundament, tom, zina, to'siq devor — IfcExtrudedAreaSolid
//    (kontur + teshiklar), material (beton sinfi), Qto miqdorlari (NetVolume, NetArea) — smeta (5D) uchun
//  * shisha (deraza / eshik) — IfcWindow / IfcDoor (OverallWidth / OverallHeight)
//  * arxitektura devorlari (WALL): IfcWall + teshik (IfcOpeningElement, IfcRelVoidsElement) + IfcWindow / IfcDoor (IfcRelFillsElement)
#include <asiacad/bim/arch.h>
#include <asiacad/bim/building.h>

#include <string>
#include <vector>

namespace asiacad::bim {

struct IfcExportInput {
    std::string project = "AsiaCAD loyiha";
    std::string building = "Bino";
    std::string author, organization = "AsiaCAD";
    std::string timestamp = "2026-01-01T00:00:00";   // FILE_NAME vaqti (testlar uchun barqaror)
    const BuildingModel* model = nullptr;             // bino modeli (ixtiyoriy)
    std::vector<ArchWall> walls;                      // arxitektura devorlari (ixtiyoriy)
    std::string wallMaterial = "Gazoblok D500";
};

struct IfcExportStats {
    int storeys = 0, elements = 0, openings = 0, windows = 0, doors = 0, entities = 0;
    double volume = 0;                                // eksport qilingan jismlar hajmi, m³
};

// STEP matni (IFC4). stats — ixtiyoriy
std::string ifcExportText(const IfcExportInput& in, IfcExportStats* stats = nullptr);
// Faylga yozish; Error("ifc.err.write") tashlaydi
IfcExportStats writeIfcFile(const IfcExportInput& in, const std::string& path);

// STEP satri uchun kodlash: apostrof ikkilanadi, ASCII bo'lmagan belgilar — \X2\hhhh\X0\ (UTF-16)
std::string ifcEncodeString(const std::string& utf8);
// 22 belgili IFC GlobalId (deterministik: kalitdan)
std::string ifcGuid(const std::string& key);

} // namespace asiacad::bim
