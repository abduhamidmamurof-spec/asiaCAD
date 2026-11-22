#pragma once
// AsiaCAD GUI — "Statika (FEM)": ramka (+ qobiq plitalar) generatori yoki .fem fayl, statik / modal / spektral hisob,
// temirbeton oldindan o'lchamlash (qizil-sariq-yashil) va 3D ko'rinish (deformatsiya, tebranish shakllari, plita momentlari).
// Hisob — bim/fem.h; chizma o'zgarmaydi.
#include <asiacad/bim/fem.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>
#include <QWidget>

#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTextBrowser;
class QTimer;

namespace asiacad::gui {

class FemView : public QWidget {
    Q_OBJECT
public:
    explicit FemView(QWidget* parent = nullptr);
    void setData(const bim::fem::Model* m, const bim::fem::StaticResult* st, const std::vector<bim::fem::ModeShape>* modes,
                 const std::vector<bim::fem::RcCheck>* rc);
    void setShow(int mode);      // 0 — model (RC ranglari), 1 — statik deformatsiya, 2+k — k-shakl (animatsiya)
    void setScale(double s) { scale_ = s; update(); }
    int show() const { return show_; }
    void fit();
    int coloredBeams() const { return colored_; }   // oxirgi chizishda rang berilgan sterjenlar (sinov uchun)

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    QPointF project(const Vec3& p, double& depth) const;
    Vec3 displaced(std::size_t node) const;
    const bim::fem::Model* m_ = nullptr;
    const bim::fem::StaticResult* st_ = nullptr;
    const std::vector<bim::fem::ModeShape>* modes_ = nullptr;
    const std::vector<bim::fem::RcCheck>* rc_ = nullptr;
    int show_ = 0;
    double scale_ = 1, yaw_ = -0.6, pitch_ = 0.55, zoom_ = 1, phase_ = 0, autoScale_ = 1;
    Vec3 center_;
    double radius_ = 1;
    QPointF last_;
    QTimer* timer_ = nullptr;
    mutable int colored_ = 0;
};

class FemDialog : public QDialog {
    Q_OBJECT
public:
    explicit FemDialog(const i18n::Localizer& loc, QWidget* parent = nullptr);
    bool loadFile(const QString& path);   // .fem
    void setGenerator(const QString& spansX, const QString& spansY, int floors, double slab);
    void setDesign(bool on);
    bool compute();                       // xato bo'lsa false (report() da sabab)
    QString report() const;
    FemView* view() const { return view_; }
    QTabWidget* tabs() const { return tabs_; }
    const bim::fem::Model* model() const { return model_ ? &*model_ : nullptr; }

private:
    QString t(const char* key, const QStringList& args = {}) const;
    bool buildModel(QString& err);
    const i18n::Localizer& loc_;
    std::optional<bim::fem::Model> model_;
    bim::fem::StaticResult static_;
    std::vector<bim::fem::ModeShape> modes_;
    std::vector<bim::fem::RcCheck> rc_;
    QString file_;
    QTabWidget* tabs_ = nullptr;
    QComboBox *src_ = nullptr, *show_ = nullptr, *rp_ = nullptr, *conc_ = nullptr;
    QLineEdit *spansX_ = nullptr, *spansY_ = nullptr, *col_ = nullptr, *beam_ = nullptr, *path_ = nullptr;
    QSpinBox *floors_ = nullptr, *div_ = nullptr, *modesN_ = nullptr;
    QDoubleSpinBox *h_ = nullptr, *slab_ = nullptr, *mass_ = nullptr, *load_ = nullptr;
    QCheckBox* design_ = nullptr;
    QSlider* scale_ = nullptr;
    QTextBrowser* text_ = nullptr;
    FemView* view_ = nullptr;
    QString report_;
};

}  // namespace asiacad::gui
