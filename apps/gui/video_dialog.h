#pragma once
// AsiaCAD GUI — "Video": yengil real-vaqt kamera yo'li 3D ko'rinishda (chizmadagi chiziq bo'ylab yurish yoki dron aylanishi),
// kadrlarni yozib olish (ffmpeg bo'lsa — MP4, aks holda PNG ketma-ketlik) va render dasturlariga eksport (EXPORT3D CAMPATH= / ORBIT=).
#include <asiacad/bim/scene_export.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

namespace asiacad::gui {

class Model3DView;

class VideoDialog : public QDialog {
    Q_OBJECT
public:
    // pathPoints — tanlangan chiziq nuqtalari (bo'sh — tanlanmagan); selectionIds — "12,15" (eksport uchun)
    VideoDialog(const i18n::Localizer& loc, Model3DView* view, std::function<std::vector<Vec3>()> pathPoints,
                std::function<QString()> selectionIds, QWidget* parent = nullptr);
    bool buildPath();                          // joriy sozlamalar bo'yicha yo'l (false — chiziq tanlanmagan / sahna bo'sh)
    const bim::CameraPath& path() const { return path_; }
    void setSource(int i);                     // 0 — chiziq bo'ylab yurish, 1 — dron aylanishi
    void showFrame(double t);                  // kamerani t soniyaga qo'yish
    int record(const QString& out, int maxFrames = -1);   // yozilgan kadrlar soni (-1 — xato); .mp4 (ffmpeg) yoki papka (PNG)
    QString status() const;

signals:
    void commandReady(const QString& line);    // EXPORT3D ...

private:
    QString t(const char* key, const QStringList& args = {}) const;
    const i18n::Localizer& loc_;
    Model3DView* view_;
    std::function<std::vector<Vec3>()> points_;
    std::function<QString()> ids_;
    bim::CameraPath path_;
    QComboBox *src_ = nullptr, *size_ = nullptr;
    QDoubleSpinBox *eye_ = nullptr, *speed_ = nullptr, *ahead_ = nullptr, *orbitSec_ = nullptr;
    QSpinBox* fps_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* timer_ = nullptr;
    double playT_ = 0;
};

}  // namespace asiacad::gui
