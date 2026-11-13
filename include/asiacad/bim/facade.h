#pragma once
// AsiaCAD — fasad: qavatlar konturlari bo'yicha fasad tizimi (vitraj / lentali / derazali), 3D panellar, fasad yoyilmasi
// (har tomon — ko'rinish chizmasi) va metraj (shisha, panel m², profil m).
//  * Konturlar: IFC modelidan (har qavat plitasi kesimi → rastr birlashma → tashqi kontur) yoki chizmadan (yopiq polilin + qavatlar).
//  * Tur: CURTAIN — butun balandlik shisha (plita oldida spandrel lentasi), BAND — lentali (shisha + yaxlit lenta),
//    PUNCHED — yaxlit devor paneli ichida derazalar.
#include <asiacad/bim/ifc.h>
#include <asiacad/core/document.h>

#include <array>
#include <string>
#include <vector>

namespace asiacad::bim {

enum class FacadeType { Curtain, Band, Punched };
bool facadeTypeFromKey(const std::string& k, FacadeType& out);

struct FacadeSpec {
    FacadeType type = FacadeType::Curtain;
    double module = 1.5;        // vertikal profillar qadami, m
    double spandrel = 0.9;      // plita sathidagi yaxlit lenta balandligi (CURTAIN — 0.6, BAND — ko'proq), m
    double offset = 0.15;       // plita chetidan tashqariga, m
    double mullion = 0.06;      // profil eni, m
    double winW = 1.5, winH = 1.8, sill = 0.9;   // PUNCHED: deraza o'lchami va tokcha
    double minEdge = 0.8;       // undan qisqa qirralarda panel bo'linmaydi
};

struct FacadeFloor {
    std::vector<Vec3> outline;  // XY, CCW, z e'tiborsiz
    double z0 = 0, z1 = 0;      // qavat poli .. keyingi qavat poli (absolyut)
    int level = 0;
};

enum class FacadePart { Glass, Panel, Mullion };
struct FacadeQuad {
    FacadePart part = FacadePart::Glass;
    std::array<Vec3, 4> q;      // pastki chap, pastki o'ng, yuqori o'ng, yuqori chap
    int side = 0;               // tashqi kontur qirrasi (yoyilma uchun)
    double s0 = 0, s1 = 0;      // qirra bo'ylab boshlanish/tugash (yoyilmada x)
};

struct FacadeResult {
    std::vector<FacadeQuad> quads;
    double glassArea = 0, panelArea = 0, mullionLen = 0;
    int glassCount = 0, panelCount = 0, floors = 0;
    double perimeter = 0, height = 0;
};

// Qavatlar konturlari: IFC (plitalar kesimi), cell — rastr qadami (m)
std::vector<FacadeFloor> facadeFloorsFromIfc(const IfcModel& m, double cell = 0.25);
// Chizmadan: yopiq polilin (qatlam nomida FASAD/FACADE/КОНТУР/KONTUR yoki berilgan qatlam) + n qavat × h
std::vector<FacadeFloor> facadeFloorsFromDoc(const Document& doc, const std::string& layer, int floors, double h, double z0);
FacadeResult buildFacade(const std::vector<FacadeFloor>& floors, const FacadeSpec& s);
// 3D obyektlar (MeshEntity): qatlamlar FASAD_SHISHA, FASAD_PANEL, FASAD_PROFIL
std::vector<std::unique_ptr<IEntity>> facadeEntities(const FacadeResult& r);
// Fasad yoyilmasi (ko'rinish): har panel to'rtburchak, qavat chiziqlari va belgilar; origin — chap past burchak (qatlam FASAD_YOYILMA)
std::vector<std::unique_ptr<IEntity>> facadeElevation(const FacadeResult& r, const std::vector<FacadeFloor>& floors, const Vec3& origin, double textH);

}  // namespace asiacad::bim
