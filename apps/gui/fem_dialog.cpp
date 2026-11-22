// AsiaCAD GUI — Statika (FEM) oynasi va 3D ko'rinishi.
#include "fem_dialog.h"

#include <asiacad/bim/seismic.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>

namespace asiacad::gui {

namespace F = bim::fem;

// ------------------------------------------------------------------ 3D ko'rinish
FemView::FemView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(420, 320);
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] { phase_ += 0.12; update(); });
}

void FemView::setData(const F::Model* m, const F::StaticResult* st, const std::vector<F::ModeShape>* modes, const std::vector<F::RcCheck>* rc) {
    m_ = m;
    st_ = st;
    modes_ = modes;
    rc_ = rc;
    fit();
    setShow(show_);
}

void FemView::setShow(int mode) {
    show_ = mode;
    // avtomatik masshtab: eng katta siljish model o'lchamining ~8% iga
    double mx = 0;
    if (m_ && show_ == 1 && st_) mx = st_->maxDisp;
    if (m_ && show_ >= 2 && modes_ && show_ - 2 < static_cast<int>(modes_->size()))
        for (const auto& d : (*modes_)[static_cast<std::size_t>(show_ - 2)].phi) mx = std::max(mx, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
    autoScale_ = mx > 1e-12 ? 0.08 * radius_ / mx : 1.0;
    if (show_ >= 2) timer_->start(40); else timer_->stop();
    update();
}

void FemView::fit() {
    if (!m_ || m_->nodes.empty()) return;
    BBox b;
    for (const auto& n : m_->nodes) b.expand(n.p);
    center_ = (b.min + b.max) * 0.5;
    radius_ = std::max(1e-6, (b.max - b.min).length() / 2);
    zoom_ = 1;
    update();
}

Vec3 FemView::displaced(std::size_t i) const {
    Vec3 p = m_->nodes[i].p;
    if (show_ == 1 && st_ && i < st_->disp.size()) {
        const auto& d = st_->disp[i];
        p = p + Vec3{d[0], d[1], d[2]} * (autoScale_ * scale_);
    } else if (show_ >= 2 && modes_ && show_ - 2 < static_cast<int>(modes_->size())) {
        const auto& ph = (*modes_)[static_cast<std::size_t>(show_ - 2)].phi;
        if (i < ph.size()) p = p + Vec3{ph[i][0], ph[i][1], ph[i][2]} * (autoScale_ * scale_ * std::sin(phase_));
    }
    return p;
}

QPointF FemView::project(const Vec3& p0, double& depth) const {
    const Vec3 p = p0 - center_;
    const double cy = std::cos(yaw_), sy = std::sin(yaw_), cp = std::cos(pitch_), sp = std::sin(pitch_);
    const double x = cy * p.x - sy * p.y, y1 = sy * p.x + cy * p.y;
    const double y = cp * p.z - sp * y1;   // ekranda yuqoriga
    depth = sp * p.z + cp * y1;            // kattaroq — uzoqroq
    const double k = std::min(width(), height()) * 0.45 / radius_ * zoom_;
    return {width() / 2.0 + x * k, height() / 2.0 - y * k};
}

void FemView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 30, 46));
    p.setRenderHint(QPainter::Antialiasing);
    colored_ = 0;
    if (!m_) return;
    // qobiqlar: chuqurlik bo'yicha saralab, moment kattaligi bilan rang
    double mMax = 0;
    if (st_ && !st_->shell.empty()) for (const auto& f : st_->shell) mMax = std::max(mMax, std::abs(f.Mx) + std::abs(f.My));
    struct Q { QPolygonF poly; double depth; QColor c; };
    std::vector<Q> qs;
    for (std::size_t si = 0; si < m_->shells.size(); ++si) {
        const auto& sh = m_->shells[si];
        Q q;
        q.depth = 0;
        for (int k = 0; k < 4; ++k) {
            double d;
            q.poly << project(displaced(static_cast<std::size_t>(m_->index(sh.n[static_cast<std::size_t>(k)]))), d);
            q.depth += d / 4;
        }
        QColor c(140, 150, 170, 110);
        if (st_ && si < st_->shell.size() && mMax > 0 && show_ <= 1) {
            const double v = (std::abs(st_->shell[si].Mx) + std::abs(st_->shell[si].My)) / mMax;   // ko'k → qizil
            c = QColor::fromHsvF(std::clamp((1 - v) * 0.66, 0.0, 0.66), 0.8, 0.95, 0.75);
        }
        q.c = c;
        qs.push_back(q);
    }
    std::sort(qs.begin(), qs.end(), [](const Q& a, const Q& b) { return a.depth > b.depth; });
    p.setPen(QPen(QColor(30, 36, 50), 0.5));
    for (const auto& q : qs) { p.setBrush(q.c); p.drawPolygon(q.poly); }
    // sterjenlar: RC foydalanish koeffitsienti bo'yicha (yashil ≤ 0.8 < sariq ≤ 1 < qizil)
    for (std::size_t bi = 0; bi < m_->beams.size(); ++bi) {
        const auto& b = m_->beams[bi];
        double d1, d2;
        const QPointF a = project(displaced(static_cast<std::size_t>(m_->index(b.n1))), d1);
        const QPointF c = project(displaced(static_cast<std::size_t>(m_->index(b.n2))), d2);
        QColor col(220, 225, 235);
        if (rc_ && bi < rc_->size() && show_ == 0) {
            const double u = (*rc_)[bi].util;
            col = u > 1 ? QColor(230, 70, 60) : u > 0.8 ? QColor(240, 200, 60) : QColor(80, 200, 110);
            ++colored_;
        }
        p.setPen(QPen(col, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, c);
    }
    // tayanchlar
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(120, 170, 255));
    for (std::size_t i = 0; i < m_->nodes.size(); ++i)
        if (std::any_of(m_->nodes[i].fix.begin(), m_->nodes[i].fix.end(), [](bool f) { return f; })) {
            double d;
            const QPointF q = project(m_->nodes[i].p, d);
            p.drawRect(QRectF(q.x() - 3, q.y() - 3, 6, 6));
        }
}

void FemView::mousePressEvent(QMouseEvent* e) { last_ = e->position(); }
void FemView::mouseMoveEvent(QMouseEvent* e) {
    const QPointF d = e->position() - last_;
    last_ = e->position();
    yaw_ += d.x() * 0.01;
    pitch_ = std::clamp(pitch_ + d.y() * 0.01, -1.5, 1.5);
    update();
}
void FemView::wheelEvent(QWheelEvent* e) {
    zoom_ *= e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    update();
}

// ------------------------------------------------------------------ oyna
QString FemDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

FemDialog::FemDialog(const i18n::Localizer& loc, QWidget* parent) : QDialog(parent), loc_(loc) {
    setWindowTitle(t("gui.fem.title"));
    auto* root = new QHBoxLayout(this);
    auto* left = new QWidget;
    left->setMaximumWidth(360);
    auto* form = new QFormLayout(left);
    src_ = new QComboBox;
    src_->addItem(t("gui.fem.src_gen"));
    src_->addItem(t("gui.fem.src_file"));
    form->addRow(t("gui.fem.source"), src_);
    auto* fileRow = new QHBoxLayout;
    path_ = new QLineEdit;
    auto* browse = new QPushButton(t("gui.fem.browse"));
    fileRow->addWidget(path_, 1);
    fileRow->addWidget(browse);
    form->addRow(t("gui.fem.file"), fileRow);
    spansX_ = new QLineEdit("6x3");
    spansY_ = new QLineEdit("6x2");
    form->addRow(t("gui.fem.spansx"), spansX_);
    form->addRow(t("gui.fem.spansy"), spansY_);
    floors_ = new QSpinBox; floors_->setRange(1, 60); floors_->setValue(5);
    form->addRow(t("gui.fem.floors"), floors_);
    h_ = new QDoubleSpinBox; h_->setRange(2, 10); h_->setDecimals(2); h_->setValue(3.3); h_->setSuffix(" m");
    form->addRow(t("gui.fem.height"), h_);
    col_ = new QLineEdit("0.4x0.4");
    beam_ = new QLineEdit("0.3x0.6");
    form->addRow(t("gui.fem.col"), col_);
    form->addRow(t("gui.fem.beam"), beam_);
    slab_ = new QDoubleSpinBox; slab_->setRange(0, 1); slab_->setDecimals(2); slab_->setSingleStep(0.02); slab_->setValue(0.2); slab_->setSuffix(" m");
    form->addRow(t("gui.fem.slab"), slab_);
    div_ = new QSpinBox; div_->setRange(1, 12); div_->setValue(3);
    form->addRow(t("gui.fem.div"), div_);
    mass_ = new QDoubleSpinBox; mass_->setRange(0, 10); mass_->setDecimals(2); mass_->setValue(1.0); mass_->setSuffix(" t/m²");
    form->addRow(t("gui.fem.mass"), mass_);
    load_ = new QDoubleSpinBox; load_->setRange(0, 200); load_->setDecimals(1); load_->setValue(10); load_->setSuffix(" kPa");
    form->addRow(t("gui.fem.load"), load_);
    modesN_ = new QSpinBox; modesN_->setRange(0, 30); modesN_->setValue(6);
    form->addRow(t("gui.fem.modes"), modesN_);
    rp_ = new QComboBox;
    for (int y : {475, 2475}) rp_->addItem(QString::number(y), y);
    form->addRow(t("gui.fem.rp"), rp_);
    design_ = new QCheckBox(t("gui.fem.design"));
    design_->setChecked(true);
    form->addRow(design_);
    conc_ = new QComboBox;
    for (const char* c : {"B20", "B25", "B30", "B35", "B40"}) conc_->addItem(c);
    conc_->setCurrentText("B25");
    form->addRow(t("gui.fem.conc"), conc_);
    auto* run = new QPushButton(t("gui.fem.run"));
    run->setDefault(true);
    auto* save = new QPushButton(t("gui.fem.save"));
    form->addRow(run);
    form->addRow(save);
    root->addWidget(left);

    tabs_ = new QTabWidget;
    auto* v3 = new QWidget;
    auto* v3l = new QVBoxLayout(v3);
    auto* bar = new QHBoxLayout;
    show_ = new QComboBox;
    scale_ = new QSlider(Qt::Horizontal);
    scale_->setRange(1, 50);
    scale_->setValue(10);
    bar->addWidget(new QLabel(t("gui.fem.show")));
    bar->addWidget(show_, 1);
    bar->addWidget(new QLabel(t("gui.fem.scale")));
    bar->addWidget(scale_, 1);
    v3l->addLayout(bar);
    view_ = new FemView;
    v3l->addWidget(view_, 1);
    tabs_->addTab(v3, t("gui.fem.tab3d"));
    text_ = new QTextBrowser;
    tabs_->addTab(text_, t("gui.fem.tabres"));
    root->addWidget(tabs_, 1);
    resize(1200, 760);

    auto syncSrc = [this] {
        const bool gen = src_->currentIndex() == 0;
        for (QWidget* w : std::initializer_list<QWidget*>{spansX_, spansY_, floors_, h_, col_, beam_, slab_, div_, mass_, load_}) w->setEnabled(gen);
        path_->setEnabled(!gen);
    };
    connect(src_, &QComboBox::currentIndexChanged, this, syncSrc);
    syncSrc();
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, t("gui.fem.file"), QString(), QStringLiteral("FEM (*.fem);;* (*)"));
        if (!f.isEmpty()) loadFile(f);
    });
    connect(run, &QPushButton::clicked, this, [this] { compute(); });
    connect(save, &QPushButton::clicked, this, [this] {
        QString err;
        if (!model_ && !buildModel(err)) { text_->setPlainText(err); return; }
        const QString f = QFileDialog::getSaveFileName(this, t("gui.fem.save"), QString(), QStringLiteral("FEM (*.fem)"));
        if (f.isEmpty()) return;
        std::ofstream o(f.toStdString(), std::ios::binary);
        o << F::writeModel(*model_);
    });
    connect(show_, &QComboBox::currentIndexChanged, this, [this](int i) { view_->setShow(std::max(0, i)); });
    connect(scale_, &QSlider::valueChanged, this, [this](int v) { view_->setScale(v / 10.0); });
}

bool FemDialog::loadFile(const QString& path) {
    std::ifstream f(path.toStdString(), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        model_ = F::parseModel(ss.str());
    } catch (const std::exception& e) {
        model_.reset();
        report_ = t("gui.fem.err", {QString::fromUtf8(e.what())});
        text_->setPlainText(report_);
        return false;
    }
    file_ = path;
    path_->setText(path);
    src_->setCurrentIndex(1);
    return true;
}

void FemDialog::setGenerator(const QString& sx, const QString& sy, int floors, double slab) {
    src_->setCurrentIndex(0);
    spansX_->setText(sx);
    spansY_->setText(sy);
    floors_->setValue(floors);
    slab_->setValue(slab);
}

void FemDialog::setDesign(bool on) { design_->setChecked(on); }

bool FemDialog::buildModel(QString& err) {
    if (src_->currentIndex() == 1) {
        if (file_.isEmpty() || !model_) { err = t("gui.fem.nofile"); return false; }
        return true;
    }
    auto spans = [](const QString& s, bool& ok) {
        std::vector<double> v;
        ok = true;
        for (const QString& part : s.split(QRegularExpression("[;+ ]"), Qt::SkipEmptyParts)) {
            const QString u = part.toUpper();
            const int x = u.indexOf('X');
            bool a = true, b = true;
            if (x < 0) v.push_back(part.toDouble(&a));
            else for (int i = 0, n = u.mid(x + 1).toInt(&b); b && i < n; ++i) v.push_back(u.left(x).toDouble(&a));
            if (!a || !b) ok = false;
        }
        for (double d : v) if (d <= 0) ok = false;
        return v;
    };
    auto pair = [](const QString& s, double& a, double& b) {
        const QStringList p = s.toUpper().split('X');
        bool o1 = false, o2 = false;
        if (p.size() == 2) { a = p[0].toDouble(&o1); b = p[1].toDouble(&o2); }
        return o1 && o2 && a > 0 && b > 0;
    };
    bool ok1, ok2;
    const auto dx = spans(spansX_->text(), ok1), dy = spans(spansY_->text(), ok2);
    double cb, ch, bb, bh;
    if (!ok1 || !ok2 || dx.empty() || dy.empty() || !pair(col_->text(), cb, ch) || !pair(beam_->text(), bb, bh)) { err = t("gui.fem.badinput"); return false; }
    if (slab_->value() > 0)
        model_ = F::frameWithSlabs(dx, dy, floors_->value(), h_->value(), cb, ch, bb, bh, 3.0e7, mass_->value(), load_->value(), slab_->value(), div_->value());
    else
        model_ = F::frameGenerator(dx, dy, floors_->value(), h_->value(), cb, ch, bb, bh, 3.0e7, mass_->value(), load_->value(), design_->isChecked());
    return true;
}

bool FemDialog::compute() {
    QString err;
    if (!buildModel(err)) { report_ = err; text_->setPlainText(err); return false; }
    const F::Model& m = *model_;
    QStringList lines;
    auto tr = [this](const char* k, const std::vector<std::string>& a) { return QString::fromStdString(loc_.tr(k, a)); };
    static_ = {};
    modes_.clear();
    rc_.clear();
    try {
        double totMass = 0;
        for (const auto& [n, v] : m.mass) totMass += v;
        for (const auto& sh : m.shells)
            if (auto it = m.smass.find(sh.id); it != m.smass.end()) {
                Vec3 p[4];
                for (int i = 0; i < 4; ++i) p[i] = m.nodes[static_cast<std::size_t>(m.index(sh.n[static_cast<std::size_t>(i)]))].p;
                totMass += it->second * 0.5 * ((p[2] - p[0]).cross(p[3] - p[1])).length();
            }
        lines << tr("fem.msg.model", {std::to_string(m.nodes.size()), std::to_string(m.beams.size()), std::format("{:.1f}", totMass)});
        static_ = F::solveStatic(m);
        double rz = 0, maxM = 0;
        for (const auto& [n, R] : static_.reactions) rz += R[2];
        for (const auto& bf : static_.forces) for (int k : {4, 5, 10, 11}) maxM = std::max(maxM, std::abs(bf.f[static_cast<std::size_t>(k)]));
        lines << tr("fem.msg.static", {std::format("{:.2f}", static_.maxDisp * 1000), std::format("{:.0f}", rz), std::format("{:.0f}", maxM)});
        if (!static_.shell.empty()) {
            double mx = 0, my = 0, nmax = 0;
            for (const auto& f : static_.shell) { mx = std::max(mx, std::abs(f.Mx)); my = std::max(my, std::abs(f.My)); nmax = std::max({nmax, std::abs(f.Nx), std::abs(f.Ny)}); }
            lines << tr("fem.msg.shells", {std::to_string(m.shells.size()), std::format("{:.1f}", mx), std::format("{:.1f}", my), std::format("{:.1f}", nmax)});
        }
        std::vector<F::BeamForces> env;
        if (totMass > 0 && modesN_->value() > 0) {
            modes_ = F::solveModes(m, modesN_->value());
            double sx = 0, sy = 0;
            for (std::size_t k = 0; k < modes_.size(); ++k) {
                const auto& q = modes_[k];
                sx += q.meffRatio[0];
                sy += q.meffRatio[1];
                lines << tr("fem.msg.mode", {std::to_string(k + 1), std::format("{:.3f}", q.T), std::format("{:.1f}", q.meffRatio[0] * 100),
                                             std::format("{:.1f}", q.meffRatio[1] * 100), std::format("{:.1f}", q.meffRatio[2] * 100)});
            }
            lines << tr(std::min(sx, sy) >= 0.9 ? "fem.msg.mass_ok" : "fem.msg.mass_low", {std::format("{:.0f}", sx * 100), std::format("{:.0f}", sy * 100)});
            const int rp = rp_->currentData().toInt();
            const auto spec = bim::seismic::siteSpectrumLot16(rp);
            for (int di = 0; di < 2; ++di) {
                const auto sp = F::responseSpectrum(m, modes_, [&](double T) { return spec.sa(T) * 9.81; }, di);
                if (env.empty()) env.assign(m.beams.size(), F::BeamForces{});
                for (std::size_t bi = 0; bi < sp.forces.size(); ++bi)
                    for (std::size_t c = 0; c < 12; ++c) env[bi].f[c] = std::max(env[bi].f[c], sp.forces[bi].f[c]);
                lines << tr("fem.msg.spectrum", {di ? "Y" : "X", std::to_string(rp), std::format("{:.0f}", sp.baseShear), std::format("{:.1f}", sp.roof * 1000),
                                                 std::format("{:.1f}", sp.baseShear / (totMass * 9.81) * 100)});
            }
        }
        if (design_->isChecked() && !m.beams.empty()) {
            F::RcParams rp;
            F::rcConcreteClass(conc_->currentText().toStdString(), rp);
            rc_ = F::designRc(m, static_, env.empty() ? nullptr : &env, rp);
            int g = 0, y = 0, r = 0;
            for (const auto& c : rc_) (c.util > 1 ? r : c.util > 0.8 ? y : g)++;
            lines << tr("fem.msg.design", {std::to_string(g), std::to_string(y), std::to_string(r), loc_.tr(env.empty() ? "fem.msg.design_static" : "fem.msg.design_seis")});
        }
    } catch (const std::exception& e) {
        report_ = t("gui.fem.err", {QString::fromUtf8(e.what())});
        text_->setPlainText(report_);
        return false;
    }
    report_ = lines.join('\n');
    text_->setPlainText(report_);
    const int keep = show_->currentIndex();
    show_->blockSignals(true);
    show_->clear();
    show_->addItem(t("gui.fem.show_model"));
    show_->addItem(t("gui.fem.show_static"));
    for (std::size_t k = 0; k < modes_.size(); ++k) show_->addItem(t("gui.fem.show_mode", {QString::number(k + 1), QString::number(modes_[k].T, 'f', 3)}));
    show_->setCurrentIndex(std::clamp(keep, 0, show_->count() - 1));
    show_->blockSignals(false);
    view_->setData(&*model_, &static_, &modes_, rc_.empty() ? nullptr : &rc_);
    view_->setShow(show_->currentIndex());
    return true;
}

QString FemDialog::report() const { return report_; }

}  // namespace asiacad::gui
