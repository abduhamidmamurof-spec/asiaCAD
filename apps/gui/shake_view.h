#pragma once
// AsiaCAD GUI — zilzilada bino tebranishi animatsiyasi: qavat rejalari (PLAN) dan yig'ilgan 3D bino
// vaqt tarixi (Newmark, modal yig'indi) bo'yicha silkinadi; har qavat rangi perda kesish ishlatilishi
// (ETABS uslubi: ko'k → yashil → sariq → qizil) bilan; pastda yer akselerogrammasi va vaqt ko'rsatkichi.
#include <asiacad/bim/seismic.h>

#include <QElapsedTimer>
#include <QWidget>

class QTimer;

namespace asiacad::gui {

class ShakeView : public QWidget {
    Q_OBJECT
public:
    explicit ShakeView(QWidget* parent = nullptr);
    // model + natija (evaluateSeismic) + yer akselerogrammasi (m/s²) → vaqt tarixi hisoblanadi
    void setScene(const bim::seismic::BuildingSeis& b, const bim::seismic::SeisResult& r, char dir, int intensity,
                  const std::vector<double>& ag, double dt);
    void play();
    void pause();
    bool playing() const;
    void restart();
    void setSpeed(double s) { speed_ = s; }
    void setExaggeration(double k) { exag_ = k; update(); }
    void setTime(double t);                     // s
    double time() const { return t_; }
    double duration() const;
    double peakTime() const;                    // tepa siljishi eng katta bo'lgan on
    double roofNow() const;                     // m
    double ratioNow(int story) const;           // joriy ishlatilish (0 dan)
    int stories() const { return static_cast<int>(z_.size()); }
    QStringList labels;                         // i18n: [0] vaqt, [1] tepa, [2] abartma, [3] eng zorlanan, [4] yer ivmesi, [5] oshdi

signals:
    void timeChanged(double t);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void tick();
    std::vector<std::vector<bim::seismic::P2>> plans_;
    std::vector<double> z_;                     // qavat ustki sathi, m
    std::vector<double> ratio_;                 // spektral ishlatilish (wallRatio)
    std::vector<double> driftEnv_;
    bim::seismic::History h_;
    std::vector<double> ag_;
    double dt_ = 0.01, t_ = 0, speed_ = 1, exag_ = 60, pga_ = 0;
    char dir_ = 'x';
    int intensity_ = 9;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    double hold_ = 0;
};

}  // namespace asiacad::gui
