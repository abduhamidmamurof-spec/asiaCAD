#pragma once
// AsiaCAD — vektor PDF chop etish (PLOT): model oynasi yoki listlar (GOST ramka, shtamp, ko'rinishlar).
// Matn TrueType shrift bilan (faqat ishlatilgan belgilar joylanadi — kirill, o'zbek, turk harflari to'g'ri chiqadi).
// Shrift topilmasa — Helvetica (faqat lotin).
#include <asiacad/core/document.h>

#include <string>
#include <vector>

namespace asiacad::io {

struct PlotOptions {
    std::string paper = "A3";       // A0..A4
    bool landscape = true;
    double margin = 10;             // mm (model rejimi)
    bool hasWindow = false;         // false — chizma chegaralari
    Vec3 w0, w1;                    // model oynasi
    double scale = 0;               // 1:scale; 0 — sig'dirish
    double unitMm = 1;              // bitta chizma birligi necha mm (m da chizilgan bo'lsa 1000)
    bool mono = false;              // oq-qora
    double lineWidth = 0.25;        // mm
    std::string font;               // .ttf yo'li; bo'sh — tizimdan qidiriladi
    std::string title = "AsiaCAD";
};

struct PlotResult {
    bool ok = false;
    std::string error;
    int pages = 0;
    double scale = 0;               // ishlatilgan masshtab (1:scale)
    std::string fontUsed;           // bo'sh — Helvetica
};

// Model oynasini bitta sahifaga
PlotResult plotModelPdf(const Document& doc, const std::string& path, const PlotOptions& o);
// Listlar (indekslar; bo'sh — hammasi). tr — shtamp yozuvlari uchun tarjima
PlotResult plotSheetsPdf(const Document& doc, const std::string& path, const std::vector<int>& which, const PlotOptions& o,
                         const std::function<std::string(const std::string&)>& tr);

std::string findSystemFont();  // Arial / DejaVu Sans / Liberation Sans ...

}  // namespace asiacad::io
