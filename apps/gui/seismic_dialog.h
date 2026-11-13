#pragma once
// AsiaCAD GUI — "Zilzila va grunt": bino seysmik modeli (.seis) bo'yicha 6–12 ball sinovi (tekshiruvlar, qavatlar
// bo'yicha ishlatilish, ball jadvali) va geotexnik kalkulyatorlar (qoziq, zamin, cho'kish, PIT).
// Formulalar — bim/seismic.h, bim/geotech.h; Excel — SEISMIC buyrug'i orqali.
#include <asiacad/bim/seismic.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTableWidget;
class QTabWidget;

namespace asiacad::gui {

class StoryBars;
class ShakeView;

class SeismicDialog : public QDialog {
    Q_OBJECT
public:
    explicit SeismicDialog(const i18n::Localizer& loc, QWidget* parent = nullptr);
    bool loadModel(const QString& path);       // .seis
    void setIntensity(int I);                  // 6..12
    void setDirection(char d);                 // 'x' | 'y'
    void compute();
    int failingCount() const;                  // joriy balldagi oshgan tekshiruvlar
    QTableWidget* checksTable() const { return checks_; }
    QTableWidget* sweepTable() const { return sweep_; }
    QTabWidget* tabs() const { return tabs_; }
    void computeGeo();                         // geotexnik sahifasi
    QString geoText() const;
    ShakeView* shakeView() const { return shake_; }
    bool loadAccelerogram(const QString& path, int column);   // matn: "t a1 t a2 …" (sm/s²)

signals:
    void commandReady(const QString& line);

private:
    QString t(const char* key, const QStringList& args = {}) const;
    QString t(const std::string& key) const { return t(key.c_str()); }
    const i18n::Localizer& loc_;
    std::optional<bim::seismic::BuildingSeis> model_;
    QString path_;
    QTabWidget* tabs_ = nullptr;
    QLineEdit* file_ = nullptr;
    QSlider* intens_ = nullptr;
    QLabel *intLbl_ = nullptr, *summary_ = nullptr, *verdict_ = nullptr, *geoRes_ = nullptr;
    QComboBox* dir_ = nullptr;
    QTableWidget *checks_ = nullptr, *sweep_ = nullptr;
    StoryBars* bars_ = nullptr;
    // tebranish animatsiyasi
    ShakeView* shake_ = nullptr;
    QPushButton* playBtn_ = nullptr;
    QLabel* agLbl_ = nullptr;
    std::vector<double> agFile_;
    double agDt_ = 0.01;
    QString agName_;
    void updateShake(const bim::seismic::SeisResult& r, char d, int I);
    // geotexnika
    QDoubleSpinBox *pD_ = nullptr, *pL_ = nullptr, *pR_ = nullptr, *pF_ = nullptr, *pLoad_ = nullptr;
    QDoubleSpinBox *bB_ = nullptr, *bL_ = nullptr, *bDf_ = nullptr, *bC_ = nullptr, *bPhi_ = nullptr, *bP_ = nullptr;
    QLineEdit* layers_ = nullptr;
    QDoubleSpinBox *pitC_ = nullptr, *pitT_ = nullptr, *pitL0_ = nullptr;
};

}  // namespace asiacad::gui
