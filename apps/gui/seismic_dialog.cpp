// AsiaCAD GUI — zilzila va grunt menyusi.
#include "seismic_dialog.h"

#include "shake_view.h"

#include <asiacad/bim/geotech.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <array>
#include <cmath>
#include <sstream>

namespace asiacad::gui {

namespace {
// ishlatilish rangi (ETABS uslubi): <50 ko'k, 50–70 yashil, 70–90 sariq, 90–100 to'q sariq, ≥100 qizil
QColor utilColor(double r) {
    if (r < 0.5) return QColor(59, 111, 216);
    if (r < 0.7) return QColor(46, 158, 106);
    if (r < 0.9) return QColor(201, 177, 42);
    if (r < 1.0) return QColor(224, 138, 30);
    return QColor(229, 72, 77);
}
QDoubleSpinBox* spin(double v, double lo, double hi, int dec, const QString& suf) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(dec);
    s->setValue(v);
    s->setSuffix(suf);
    return s;
}
}  // namespace

// Qavatlar bo'yicha perda kesish ishlatilishi — gorizontal ustunlar (pastdan yuqoriga)
class StoryBars : public QWidget {
public:
    std::vector<double> v;
    QString title;
    StoryBars() { setMinimumSize(260, 300); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base());
        p.setPen(palette().text().color());
        p.drawText(QRect(6, 2, width() - 12, 18), Qt::AlignLeft, title);
        if (v.empty()) return;
        const int n = static_cast<int>(v.size());
        const double top = 24, bot = height() - 18, x0 = 34, w = width() - x0 - 12;
        const double hb = (bot - top) / n;
        const double vmax = 1.2;
        // 100% chizig'i
        p.setPen(QPen(QColor(229, 72, 77), 1, Qt::DashLine));
        const double x100 = x0 + w * 1.0 / vmax;
        p.drawLine(QPointF(x100, top), QPointF(x100, bot));
        p.drawText(QPointF(x100 - 14, height() - 4), "100%");
        for (int i = 0; i < n; ++i) {
            const double y = bot - (i + 1) * hb;
            const double r = v[static_cast<std::size_t>(i)];
            p.fillRect(QRectF(x0, y + 1, w * std::min(r, vmax) / vmax, std::max(1.0, hb - 2)), utilColor(r));
            if (n <= 40 && (i % 3 == 0 || i == n - 1)) {
                p.setPen(palette().text().color());
                p.drawText(QRectF(0, y, x0 - 4, hb), Qt::AlignRight | Qt::AlignVCenter, QString::number(i + 1));
            }
        }
    }
};

QString SeismicDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

SeismicDialog::SeismicDialog(const i18n::Localizer& loc, QWidget* parent) : QDialog(parent), loc_(loc) {
    setWindowTitle(t("gui.seis.title"));
    auto* lay = new QVBoxLayout(this);
    tabs_ = new QTabWidget;
    lay->addWidget(tabs_);

    // ---- 1: bino zilzilasi
    auto* pg = new QWidget;
    auto* g = new QGridLayout(pg);
    file_ = new QLineEdit;
    auto* browse = new QPushButton(t("gui.seis.browse"));
    auto* frow = new QHBoxLayout;
    frow->addWidget(new QLabel(t("gui.seis.model")));
    frow->addWidget(file_, 1);
    frow->addWidget(browse);
    g->addLayout(frow, 0, 0, 1, 2);
    intens_ = new QSlider(Qt::Horizontal);
    intens_->setRange(6, 12);
    intens_->setValue(9);
    intens_->setTickPosition(QSlider::TicksBelow);
    intLbl_ = new QLabel;
    dir_ = new QComboBox;
    dir_->addItem(t("gui.seis.dirx"));
    dir_->addItem(t("gui.seis.diry"));
    auto* run = new QPushButton(t("gui.seis.run"));
    auto* xl = new QPushButton(t("gui.seis.excel"));
    auto* crow = new QHBoxLayout;
    crow->addWidget(new QLabel(t("gui.seis.intensity")));
    crow->addWidget(intens_, 1);
    crow->addWidget(intLbl_);
    crow->addWidget(dir_);
    crow->addWidget(run);
    crow->addWidget(xl);
    g->addLayout(crow, 1, 0, 1, 2);
    verdict_ = new QLabel;
    verdict_->setStyleSheet("font-weight:600;padding:6px;border-radius:6px");
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    g->addWidget(verdict_, 2, 0, 1, 2);
    g->addWidget(summary_, 3, 0, 1, 2);
    checks_ = new QTableWidget(0, 3);
    checks_->setHorizontalHeaderLabels({t("gui.seis.check"), t("gui.seis.util"), t("gui.seis.status")});
    checks_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    checks_->verticalHeader()->hide();
    checks_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    g->addWidget(checks_, 4, 0);
    bars_ = new StoryBars;
    bars_->title = t("gui.seis.bars");
    g->addWidget(bars_, 4, 1, 2, 1);
    sweep_ = new QTableWidget(7, 0);
    sweep_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    g->addWidget(new QLabel(t("gui.seis.sweep")), 5, 0);
    g->addWidget(sweep_, 6, 0, 1, 2);
    auto* note = new QLabel(t("gui.seis.note"));
    note->setWordWrap(true);
    note->setStyleSheet("color:gray");
    g->addWidget(note, 7, 0, 1, 2);
    g->setColumnStretch(0, 3);
    g->setColumnStretch(1, 2);
    tabs_->addTab(pg, t("gui.seis.tab_build"));

    // ---- 2: tebranish animatsiyasi
    auto* sp = new QWidget;
    auto* sl = new QVBoxLayout(sp);
    auto* srow = new QHBoxLayout;
    playBtn_ = new QPushButton(t("gui.shake.play"));
    playBtn_->setStyleSheet("font-weight:600;padding:4px 14px");
    auto* rst = new QPushButton(t("gui.shake.restart"));
    auto* peak = new QPushButton(t("gui.shake.peak"));
    auto* spd = new QComboBox;
    for (const char* v : {"0.25×", "0.5×", "1×", "2×"}) spd->addItem(v);
    spd->setCurrentIndex(2);
    auto* ex = new QSlider(Qt::Horizontal);
    ex->setRange(1, 200);
    ex->setValue(60);
    auto* exl = new QLabel("×60");
    auto* agBtn = new QPushButton(t("gui.shake.loadag"));
    agLbl_ = new QLabel(t("gui.shake.synth"));
    agLbl_->setStyleSheet("color:gray");
    srow->addWidget(playBtn_);
    srow->addWidget(rst);
    srow->addWidget(peak);
    srow->addWidget(new QLabel(t("gui.shake.speed")));
    srow->addWidget(spd);
    srow->addWidget(new QLabel(t("gui.shake.exag")));
    srow->addWidget(ex, 1);
    srow->addWidget(exl);
    srow->addWidget(agBtn);
    sl->addLayout(srow);
    sl->addWidget(agLbl_);
    shake_ = new ShakeView;
    const QStringList qa{"%1", "%2"};
    shake_->labels = {t("gui.shake.l_time", qa), t("gui.shake.l_roof", qa), t("gui.shake.l_exag", qa), t("gui.shake.l_worst", qa), t("gui.shake.l_ag", qa),
                      t("gui.shake.l_over")};
    sl->addWidget(shake_, 1);
    auto* snote = new QLabel(t("gui.shake.note"));
    snote->setWordWrap(true);
    snote->setStyleSheet("color:gray");
    sl->addWidget(snote);
    tabs_->addTab(sp, t("gui.shake.tab"));
    connect(playBtn_, &QPushButton::clicked, this, [this] {
        if (shake_->playing()) shake_->pause(); else shake_->play();
        playBtn_->setText(t(shake_->playing() ? "gui.shake.pause" : "gui.shake.play"));
    });
    connect(rst, &QPushButton::clicked, this, [this] { shake_->restart(); });
    connect(peak, &QPushButton::clicked, this, [this] {
        shake_->pause();
        playBtn_->setText(t("gui.shake.play"));
        shake_->setTime(shake_->peakTime());
    });
    connect(spd, &QComboBox::currentIndexChanged, this, [this](int i) { shake_->setSpeed(std::array{0.25, 0.5, 1.0, 2.0}[static_cast<std::size_t>(std::clamp(i, 0, 3))]); });
    connect(ex, &QSlider::valueChanged, this, [this, exl](int v) { shake_->setExaggeration(v); exl->setText(QString("×%1").arg(v)); });
    connect(agBtn, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, t("gui.shake.loadag"), QFileInfo(path_).absolutePath(), "Accelerogram (*.txt *.dat *.csv);;* (*)");
        if (f.isEmpty()) return;
        if (loadAccelerogram(f, 0)) compute();
    });
    // bino sahifasidan: animatsiyani ko'rish tugmasi
    auto* goShake = new QPushButton(t("gui.shake.open"));
    goShake->setStyleSheet("font-weight:600");
    crow->addWidget(goShake);
    connect(goShake, &QPushButton::clicked, this, [this] {
        tabs_->setCurrentIndex(1);
        if (!shake_->playing()) { shake_->restart(); shake_->play(); playBtn_->setText(t("gui.shake.pause")); }
    });
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int i) {
        if (i != 1 && shake_->playing()) { shake_->pause(); playBtn_->setText(t("gui.shake.play")); }
    });

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, t("gui.seis.model"), QFileInfo(path_).absolutePath(), "AsiaCAD seismic (*.seis)");
        if (!f.isEmpty() && loadModel(f)) compute();
    });
    connect(intens_, &QSlider::valueChanged, this, [this] { compute(); });
    connect(dir_, &QComboBox::currentIndexChanged, this, [this] { compute(); });
    connect(run, &QPushButton::clicked, this, [this] { compute(); });
    connect(xl, &QPushButton::clicked, this, [this] {
        if (path_.isEmpty()) return;
        const QString f = QFileDialog::getSaveFileName(this, t("gui.seis.excel"), QFileInfo(path_).completeBaseName() + ".xlsx", "Excel (*.xlsx)");
        if (!f.isEmpty()) emit commandReady(QString("SEISMIC \"%1\" I=%2 SWEEP XLSX=\"%3\"").arg(path_).arg(intens_->value()).arg(f));
    });

    // ---- 2: grunt va qoziq
    auto* gp = new QWidget;
    auto* gl = new QGridLayout(gp);
    auto box = [&](const char* title) { auto* b = new QGroupBox(t(title)); new QFormLayout(b); return b; };
    auto* pile = box("gui.geo.pile");
    auto* pf = static_cast<QFormLayout*>(pile->layout());
    pf->addRow(t("gui.geo.D"), pD_ = spin(1.2, 0.3, 3, 2, " m"));
    pf->addRow(t("gui.geo.L"), pL_ = spin(24, 2, 80, 2, " m"));
    pf->addRow(t("gui.geo.R"), pR_ = spin(3000, 0, 20000, 0, " kPa"));
    pf->addRow(t("gui.geo.F"), pF_ = spin(110, 0, 500, 0, " kPa"));
    pf->addRow(t("gui.geo.load"), pLoad_ = spin(7699, 0, 1e6, 0, " kN"));
    auto* bear = box("gui.geo.bearing");
    auto* bf = static_cast<QFormLayout*>(bear->layout());
    bf->addRow("B", bB_ = spin(42, 0.5, 300, 2, " m"));
    bf->addRow("L", bL_ = spin(65, 0.5, 300, 2, " m"));
    bf->addRow(t("gui.geo.Df"), bDf_ = spin(6.45, 0, 50, 2, " m"));
    bf->addRow("c", bC_ = spin(0, 0, 500, 1, " kPa"));
    bf->addRow("φ", bPhi_ = spin(38, 0, 50, 1, "°"));
    bf->addRow(t("gui.geo.p"), bP_ = spin(750, 0, 5000, 0, " kPa"));
    bf->addRow(t("gui.geo.layers"), layers_ = new QLineEdit("20:50:20,30:80:21"));
    auto* pit = box("gui.geo.pit");
    auto* tf = static_cast<QFormLayout*>(pit->layout());
    tf->addRow(t("gui.geo.c"), pitC_ = spin(4500, 1000, 6000, 0, " m/s"));
    tf->addRow(t("gui.geo.t"), pitT_ = spin(0.00764, 0.0005, 0.1, 5, " s"));
    tf->addRow(t("gui.geo.L0"), pitL0_ = spin(25, 1, 80, 2, " m"));
    auto* calc = new QPushButton(t("gui.seis.run"));
    geoRes_ = new QLabel;
    geoRes_->setWordWrap(true);
    geoRes_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    gl->addWidget(pile, 0, 0);
    gl->addWidget(bear, 0, 1);
    gl->addWidget(pit, 0, 2);
    gl->addWidget(calc, 1, 0);
    gl->addWidget(geoRes_, 2, 0, 1, 3);
    gl->setRowStretch(3, 1);
    tabs_->addTab(gp, t("gui.seis.tab_geo"));
    connect(calc, &QPushButton::clicked, this, [this] { computeGeo(); });

    resize(1100, 820);
    // namuna: Energetika vazirligi A blok
    for (const QString& d : {QCoreApplication::applicationDirPath() + "/examples", QString("examples")
#ifdef ASIACAD_EXAMPLES_DIR
                             , QString(ASIACAD_EXAMPLES_DIR)
#endif
         }) {
        const QString f = d + "/energetika_A_blok.seis";
        if (QFile::exists(f) && loadModel(f)) { compute(); break; }
    }
    computeGeo();
}

bool SeismicDialog::loadModel(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    try {
        model_ = bim::seismic::readBuildingSeis(f.readAll().toStdString());
    } catch (const std::exception& e) {
        summary_->setText(t("seis.err.parse", {QString::fromUtf8(e.what())}));
        model_.reset();
        return false;
    }
    path_ = QFileInfo(path).absoluteFilePath();
    file_->setText(QString::fromStdString(model_->name) + "  —  " + path_);
    return true;
}

void SeismicDialog::setIntensity(int I) { intens_->setValue(I); }
void SeismicDialog::setDirection(char d) { dir_->setCurrentIndex(d == 'y' ? 1 : 0); }

int SeismicDialog::failingCount() const {
    int n = 0;
    for (int r = 0; r < checks_->rowCount(); ++r)
        if (checks_->item(r, 1) && checks_->item(r, 1)->data(Qt::UserRole).toDouble() >= 1) ++n;
    return n;
}

void SeismicDialog::compute() {
    using namespace bim::seismic;
    const int I = intens_->value();
    if (!model_) { intLbl_->setText(QString::number(I)); return; }
    const double pga = pgaForIntensity(model_->pga9, I);
    intLbl_->setText(t("gui.seis.intlbl", {QString::number(I), QString::number(pga, 'f', 2)}));
    const char d = dir_->currentIndex() == 1 ? 'y' : 'x';
    const auto r = evaluateSeismic(*model_, pga, d);
    QString T;
    for (std::size_t k = 0; k < std::min<std::size_t>(3, r.modes.size()); ++k) T += (k ? " / " : "") + QString::number(r.modes[k].T, 'f', 2);
    summary_->setText(t("seis.msg.result", {d == 'y' ? "Y" : "X", QString::number(pga, 'f', 3), T, QString::number(r.resp.baseShear / 1000, 'f', 0),
                                            QString::number(r.resp.roof * 100, 'f', 1), QString::number(r.resp.maxDrift > 0 ? int(1 / r.resp.maxDrift) : 0)}));
    const bool ok = r.ok();
    verdict_->setText(t(ok ? "gui.seis.ok" : "gui.seis.fail", {QString::number(I)}));
    verdict_->setStyleSheet(QString("font-weight:600;padding:6px;border-radius:6px;background:%1;color:white").arg(ok ? "#2e9e6a" : "#e5484d"));
    checks_->setRowCount(static_cast<int>(r.checks.size()));
    for (int i = 0; i < static_cast<int>(r.checks.size()); ++i) {
        const auto& c = r.checks[static_cast<std::size_t>(i)];
        QString name = t("seis.chk." + c.key);
        if (c.story) name += "  (" + t("gui.seis.story", {QString::number(c.story)}) + ")";
        checks_->setItem(i, 0, new QTableWidgetItem(name));
        auto* u = new QTableWidgetItem(QString("%1 %").arg(std::round(c.ratio * 100)));
        u->setData(Qt::UserRole, c.ratio);
        u->setBackground(utilColor(c.ratio));
        u->setForeground(Qt::white);
        checks_->setItem(i, 1, u);
        checks_->setItem(i, 2, new QTableWidgetItem(t(c.ratio < 1 ? "seis.xl.ok" : "seis.xl.bad")));
    }
    checks_->resizeColumnsToContents();
    bars_->v = r.wallRatio;
    bars_->update();
    updateShake(r, d, I);
    // 6–12 ball jadvali
    QStringList keys;
    for (const auto& c : r.checks) keys << QString::fromStdString(c.key);
    sweep_->setColumnCount(static_cast<int>(keys.size()) + 1);
    QStringList hdr{t("seis.xl.int")};
    for (const auto& k : keys) hdr << t(("seis.chk." + k.toStdString()).c_str());
    sweep_->setHorizontalHeaderLabels(hdr);
    sweep_->verticalHeader()->hide();
    for (int i = 6; i <= 12; ++i) {
        const double p = pgaForIntensity(model_->pga9, i);
        const auto ri = evaluateSeismic(*model_, p, d);
        auto* h = new QTableWidgetItem(QString("%1  (%2 g)").arg(i).arg(p, 0, 'f', 2));
        if (i == I) { QFont f = h->font(); f.setBold(true); h->setFont(f); }
        sweep_->setItem(i - 6, 0, h);
        for (int k = 0; k < keys.size(); ++k) {
            double rr = 0;
            for (const auto& c : ri.checks) if (QString::fromStdString(c.key) == keys[k]) rr = c.ratio;
            auto* it = new QTableWidgetItem(QString("%1 %").arg(std::round(rr * 100)));
            it->setBackground(utilColor(rr));
            it->setForeground(Qt::white);
            sweep_->setItem(i - 6, k + 1, it);
        }
    }
    sweep_->resizeColumnsToContents();
    sweep_->horizontalHeader()->setStretchLastSection(true);
}

bool SeismicDialog::loadAccelerogram(const QString& path, int column) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    double dt = 0;
    auto a = bim::seismic::readAccelerogram(f.readAll().toStdString(), column, &dt);
    if (a.size() < 10 || dt <= 0) return false;
    agFile_ = std::move(a);
    agDt_ = dt;
    agName_ = QFileInfo(path).fileName();
    return true;
}

void SeismicDialog::updateShake(const bim::seismic::SeisResult& r, char d, int I) {
    if (!shake_ || !model_) return;
    std::vector<double> ag;
    double dt = 0.01;
    if (!agFile_.empty()) {   // fayldagi yozuv joriy PGA ga keltiriladi
        double mx = 1e-12;
        for (double v : agFile_) mx = std::max(mx, std::abs(v));
        ag = agFile_;
        for (auto& v : ag) v *= r.pga * 9.81 / mx;
        dt = agDt_;
        agLbl_->setText(t("gui.shake.file", {agName_}));
    } else {
        ag = bim::seismic::syntheticAccelerogram(r.pga, dt, 20.0);
        agLbl_->setText(t("gui.shake.synth"));
    }
    shake_->setScene(*model_, r, d, I, ag, dt);
}

void SeismicDialog::computeGeo() {
    using namespace bim::geotech;
    QStringList out;
    PileInput pi;
    pi.D = pD_->value(); pi.L = pL_->value(); pi.R = pR_->value(); pi.fAvg = pF_->value();
    const auto pc = pileCapacity(pi);
    out << t("geo.msg.pile", {QString::number(pc.Qs, 'f', 0), QString::number(pc.Qb, 'f', 0), QString::number(pc.Qd, 'f', 0), QString::number(pc.Fd, 'f', 0)});
    const double P = pLoad_->value();
    if (P > 0) out << t(P <= pc.Qd ? "geo.msg.pile_ok" : "geo.msg.pile_bad", {QString::number(P, 'f', 0), QString::number(pc.Qd / P, 'f', 2)});
    BearingInput bi;
    bi.B = bB_->value(); bi.L = bL_->value(); bi.Df = bDf_->value(); bi.c = bC_->value(); bi.phiDeg = bPhi_->value();
    const auto br = bearingCapacity(bi);
    out << t("geo.msg.bearing", {QString::number(br.Nc, 'f', 2), QString::number(br.Nq, 'f', 2), QString::number(br.Ng, 'f', 2), QString::number(br.qult, 'f', 0),
                                 QString::number(br.qall, 'f', 0)});
    SettlementInput si;
    si.B = bi.B; si.L = bi.L; si.Df = bi.Df; si.p = bP_->value();
    for (const auto& tok : layers_->text().split(',', Qt::SkipEmptyParts)) {
        const auto f = tok.split(':');
        if (f.size() < 2) continue;
        SoilLayer l;
        l.h = f[0].toDouble(); l.E = f[1].toDouble();
        if (f.size() > 2) l.gamma = f[2].toDouble();
        si.layers.push_back(l);
    }
    if (!si.layers.empty()) {
        const auto sr = settlement(si);
        out << t("geo.msg.settle", {QString::number(sr.s * 1000, 'f', 1), QString::number(sr.Hc, 'f', 1), QString::number(sr.p0, 'f', 0)});
    }
    const auto pr = pitCheck(pitC_->value(), pitT_->value(), pitL0_->value());
    out << t(pr.flag ? "geo.msg.pit_bad" : "geo.msg.pit_ok", {QString::number(pr.L, 'f', 2), QString::number(-pr.shortfall, 'f', 2),
                                                             t(("geo.pit.q" + std::to_string(pr.quality)).c_str())});
    geoRes_->setText(out.join("\n"));
}

QString SeismicDialog::geoText() const { return geoRes_->text(); }

}  // namespace asiacad::gui
