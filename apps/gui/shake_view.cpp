// AsiaCAD GUI — zilzilada bino tebranishi animatsiyasi.
#include "shake_view.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace asiacad::gui {

namespace {
QColor utilCol(double r) {
    if (r < 0.5) return QColor(59, 111, 216);
    if (r < 0.7) return QColor(46, 158, 106);
    if (r < 0.9) return QColor(201, 177, 42);
    if (r < 1.0) return QColor(224, 138, 30);
    return QColor(229, 72, 77);
}
constexpr double kAz = 0.62, kEl = 0.52;   // ko'rinish burchaklari (rad)
}  // namespace

ShakeView::ShakeView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(420, 420);
    timer_ = new QTimer(this);
    timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, [this] { tick(); });
}

void ShakeView::setScene(const bim::seismic::BuildingSeis& b, const bim::seismic::SeisResult& r, char dir, int intensity,
                         const std::vector<double>& ag, double dt) {
    using namespace bim::seismic;
    const std::size_t n = b.stories.size();
    plans_.assign(n, b.foot);
    for (std::size_t i = 0; i < n && i < b.plans.size(); ++i)
        if (b.plans[i].size() >= 3) plans_[i] = b.plans[i];
    for (auto& pl : plans_) {   // soat strelkasiga teskari (CCW) tartib
        double a = 0;
        for (std::size_t k = 0; k < pl.size(); ++k) a += pl[k].x * pl[(k + 1) % pl.size()].y - pl[(k + 1) % pl.size()].x * pl[k].y;
        if (a < 0) std::reverse(pl.begin(), pl.end());
    }
    z_.clear();
    double z = 0;
    for (const auto& s : b.stories) z_.push_back(z += s.h);
    ratio_ = r.wallRatio;
    ratio_.resize(n, 0.0);
    ag_ = ag;
    dt_ = dt;
    dir_ = dir;
    intensity_ = intensity;
    pga_ = r.pga;
    h_ = timeHistory(b.dir(dir), r.modes, ag, dt);
    // vaqt tarixi spektral natijaga (tepa siljishi) keltiriladi — jadvaldagi raqamlar bilan bir xil bo'lsin
    if (h_.roofMax > 1e-12 && r.resp.roof > 0) {
        const double k = r.resp.roof / h_.roofMax;
        for (auto& row : h_.disp) for (auto& v : row) v *= k;
        for (auto& v : h_.driftEnv) v *= k;
        h_.roofMax *= k;
        h_.driftMax *= k;
    }
    driftEnv_ = h_.driftEnv;
    if (t_ > duration()) t_ = 0;
    update();
}

double ShakeView::duration() const { return ag_.empty() ? 0 : dt_ * static_cast<double>(ag_.size() - 1); }
bool ShakeView::playing() const { return timer_->isActive(); }
void ShakeView::play() {
    if (ag_.empty()) return;
    clock_.restart();
    timer_->start();
}
void ShakeView::pause() { timer_->stop(); }
void ShakeView::restart() {
    t_ = 0;
    hold_ = 0;
    clock_.restart();
    update();
    emit timeChanged(t_);
}
void ShakeView::setTime(double t) {
    t_ = std::clamp(t, 0.0, duration());
    update();
    emit timeChanged(t_);
}

double ShakeView::peakTime() const {
    double best = 0, tb = 0;
    for (std::size_t k = 0; k < h_.disp.size(); ++k)
        if (!h_.disp[k].empty() && std::abs(h_.disp[k].back()) > best) { best = std::abs(h_.disp[k].back()); tb = k * dt_; }
    return tb;
}

void ShakeView::tick() {
    const double dtw = clock_.restart() / 1000.0;
    if (hold_ > 0) {
        hold_ -= dtw;
        if (hold_ <= 0) t_ = 0;
    } else {
        t_ += dtw * speed_;
        if (t_ >= duration()) { t_ = duration(); hold_ = 1.2; }   // oxirida biroz to'xtab, qaytadan
    }
    update();
    emit timeChanged(t_);
}

// joriy vaqt bo'yicha qavat siljishi (m), chiziqli interpolyatsiya
static double dispAt(const bim::seismic::History& h, double t, double dt, int i) {
    if (h.disp.empty() || i < 0) return 0;
    const double f = t / dt;
    const std::size_t k = std::min<std::size_t>(static_cast<std::size_t>(f), h.disp.size() - 1);
    const std::size_t k2 = std::min(k + 1, h.disp.size() - 1);
    const double a = f - static_cast<double>(k);
    const auto& d0 = h.disp[k];
    const auto& d1 = h.disp[k2];
    if (static_cast<std::size_t>(i) >= d0.size()) return 0;
    return d0[static_cast<std::size_t>(i)] * (1 - a) + d1[static_cast<std::size_t>(i)] * a;
}

double ShakeView::roofNow() const { return z_.empty() ? 0 : dispAt(h_, t_, dt_, static_cast<int>(z_.size()) - 1); }

double ShakeView::ratioNow(int i) const {
    if (i < 0 || i >= static_cast<int>(z_.size())) return 0;
    const double hh = z_[static_cast<std::size_t>(i)] - (i ? z_[static_cast<std::size_t>(i - 1)] : 0);
    const double dr = std::abs(dispAt(h_, t_, dt_, i) - dispAt(h_, t_, dt_, i - 1)) / std::max(0.1, hh);
    const double env = i < static_cast<int>(driftEnv_.size()) ? driftEnv_[static_cast<std::size_t>(i)] : 0;
    return env > 1e-12 ? ratio_[static_cast<std::size_t>(i)] * dr / env : 0;
}

void ShakeView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0, QColor(14, 24, 48));
    bg.setColorAt(1, QColor(8, 14, 30));
    p.fillRect(rect(), bg);
    const QColor txt(220, 228, 245);
    if (z_.empty() || plans_.empty()) {
        p.setPen(txt);
        p.drawText(rect(), Qt::AlignCenter, "—");
        return;
    }
    const int n = static_cast<int>(z_.size());
    // juda katta silkinishda tasvir ekrandan chiqmasin: tepa siljishi ≤ bino balandligining 22% i
    const double exag = h_.roofMax > 1e-9 ? std::min(exag_, 0.22 * z_.back() / h_.roofMax) : exag_;
    const double ca = std::cos(kAz), sa = std::sin(kAz), ce = std::cos(kEl), se = std::sin(kEl);
    // reja markazi
    double cx = 0, cy = 0;
    int cnt = 0;
    for (const auto& pl : plans_)
        for (const auto& q : pl) { cx += q.x; cy += q.y; ++cnt; }
    cx /= std::max(1, cnt);
    cy /= std::max(1, cnt);
    auto proj = [&](double x, double y, double z) {
        const double X = (x - cx) * ca - (y - cy) * sa, Y = (x - cx) * sa + (y - cy) * ca;
        return QPointF(X, -z * ce + Y * se);
    };
    auto depth = [&](double x, double y) { return (x - cx) * sa + (y - cy) * ca; };
    // masshtab: butun bino (± tebranish zaxirasi) sig'sin
    const double H = z_.back();
    double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
    for (int i = 0; i < n; ++i)
        for (const auto& q : plans_[static_cast<std::size_t>(i)])
            for (double zz : {i ? z_[static_cast<std::size_t>(i - 1)] : 0.0, z_[static_cast<std::size_t>(i)]}) {
                const QPointF s = proj(q.x, q.y, zz);
                minx = std::min(minx, s.x()); maxx = std::max(maxx, s.x());
                miny = std::min(miny, s.y()); maxy = std::max(maxy, s.y());
            }
    const double margin = H * 0.18;
    const double plotTop = 54, plotBot = height() - 92;
    const double sc = std::min((width() - 40) / (maxx - minx + 2 * margin), (plotBot - plotTop) / (maxy - miny + margin * 0.3));
    const QPointF off(width() / 2.0 - (minx + maxx) / 2 * sc, plotBot - maxy * sc - 6);
    auto S = [&](const QPointF& q) { return off + q * sc; };

    // yer (zamin plitasi) — yer ivmesi bo'yicha titraydi
    const std::size_t kk = std::min<std::size_t>(static_cast<std::size_t>(t_ / dt_), ag_.empty() ? 0 : ag_.size() - 1);
    const double agNow = ag_.empty() ? 0 : ag_[kk];
    const double jig = agNow / 9.81 * 0.6 * std::min(1.0, exag / 60.0);   // ko'rinish uchun (m)
    const double gx = dir_ == 'x' ? jig : 0, gy = dir_ == 'y' ? jig : 0;
    {
        const double R = std::max(maxx - minx, maxy - miny) * 0.75 / std::max(ce, 0.3);
        QPolygonF g;
        for (auto [ux, uy] : {std::pair{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}}) g << S(proj(cx + ux * R + gx, cy + uy * R + gy, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(38, 52, 82));
        p.drawPolygon(g);
        p.setPen(QPen(QColor(60, 78, 116), 1));
        for (int k = -8; k <= 8; ++k) {
            const double f = R * k / 8.0;
            p.drawLine(S(proj(cx - R + gx, cy + f + gy, 0)), S(proj(cx + R + gx, cy + f + gy, 0)));
            p.drawLine(S(proj(cx + f + gx, cy - R + gy, 0)), S(proj(cx + f + gx, cy + R + gy, 0)));
        }
    }
    // qavatlar: pastdan yuqoriga (yuqoridagilar pastdagilarni yopadi)
    const QPointF light(-0.5, -0.8);
    int worst = -1;
    double worstR = 0;
    for (int i = 0; i < n; ++i) {
        const auto& pl = plans_[static_cast<std::size_t>(i)];
        if (pl.size() < 3) continue;
        const double z0 = i ? z_[static_cast<std::size_t>(i - 1)] : 0, z1 = z_[static_cast<std::size_t>(i)];
        const double d0 = (i ? dispAt(h_, t_, dt_, i - 1) : 0) * exag, d1 = dispAt(h_, t_, dt_, i) * exag;
        const double bx = gx + (dir_ == 'x' ? d0 : 0), by = gy + (dir_ == 'y' ? d0 : 0);
        const double tx = gx + (dir_ == 'x' ? d1 : 0), ty = gy + (dir_ == 'y' ? d1 : 0);
        const double rr = ratioNow(i);
        if (rr > worstR) { worstR = rr; worst = i; }
        const QColor base = utilCol(rr);
        // yon yuzlar — chuqurlik bo'yicha uzoqdan yaqinga
        struct Face { QPolygonF q; double d; double shade; };
        std::vector<Face> faces;
        const std::size_t m = pl.size();
        for (std::size_t k = 0; k < m; ++k) {
            const auto& a = pl[k];
            const auto& b = pl[(k + 1) % m];
            QPolygonF q;
            q << S(proj(a.x + bx, a.y + by, z0)) << S(proj(b.x + bx, b.y + by, z0)) << S(proj(b.x + tx, b.y + ty, z1)) << S(proj(a.x + tx, a.y + ty, z1));
            // ko'rinadigan tomonmi (ekrandagi yo'nalish)
            const QPointF e1 = q[1] - q[0], e2 = q[3] - q[0];
            const double cr = e1.x() * e2.y() - e1.y() * e2.x();
            if (cr <= 0) continue;
            const double ex = b.x - a.x, ey = b.y - a.y, L = std::hypot(ex, ey);
            const double nx = ey / std::max(L, 1e-9), ny = -ex / std::max(L, 1e-9);
            const double sx = nx * ca - ny * sa;   // ekran bo'yicha normal
            faces.push_back({q, depth((a.x + b.x) / 2, (a.y + b.y) / 2), 0.62 + 0.3 * std::clamp(-sx * light.x(), -1.0, 1.0)});
        }
        std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.d < b.d; });
        for (const auto& f : faces) {
            QColor c = base;
            c.setRgbF(std::min(1.0, c.redF() * f.shade), std::min(1.0, c.greenF() * f.shade), std::min(1.0, c.blueF() * f.shade));
            p.setPen(QPen(c.darker(150), 0.6));
            p.setBrush(c);
            p.drawPolygon(f.q);
        }
        // tom (qavat plitasi)
        QPolygonF top;
        for (const auto& a : pl) top << S(proj(a.x + tx, a.y + ty, z1));
        p.setPen(QPen(QColor(20, 26, 40), 0.8));
        p.setBrush(base.lighter(135));
        p.drawPolygon(top);
        if (rr >= 1.0) {   // oshgan qavat — ogohlantirish belgisi
            const QPointF c0 = S(proj(cx + tx, cy + ty, z1));
            p.setPen(QPen(QColor(255, 235, 235), 1.2));
            p.setBrush(QColor(229, 72, 77));
            p.drawEllipse(c0, 7, 7);
            p.drawText(QRectF(c0.x() - 7, c0.y() - 8, 14, 16), Qt::AlignCenter, "!");
        }
    }

    // yuqori ma'lumot paneli
    auto lab = [&](int k) { return k < labels.size() ? labels[k] : QString(); };
    p.setPen(txt);
    QFont f = font();
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * 1.05);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRectF(12, 8, width() - 24, 20), Qt::AlignLeft,
               QString("%1 · %2 g · %3").arg(intensity_).arg(pga_, 0, 'f', 2).arg(dir_ == 'y' ? "Y" : "X") + "   " + lab(0).arg(t_, 0, 'f', 1));
    f.setBold(false);
    p.setFont(f);
    p.drawText(QRectF(12, 28, width() - 24, 20), Qt::AlignLeft,
               lab(1).arg(roofNow() * 100, 0, 'f', 1).arg(h_.roofMax * 100, 0, 'f', 1) + "    " + lab(2).arg(exag, 0, 'f', 0));
    if (worst >= 0) {
        const QColor wc = utilCol(worstR);
        p.setPen(wc.lighter(130));
        p.drawText(QRectF(12, 28, width() - 24, 20), Qt::AlignRight, lab(3).arg(worst + 1).arg(std::round(worstR * 100)) + (worstR >= 1 ? "  " + lab(5) : QString()));
    }
    // rang shkalasi
    {
        const double lx = width() - 26, ly0 = 64, lh = 150;
        const double vals[] = {1.1, 0.95, 0.8, 0.6, 0.3};
        const char* names[] = {"≥100%", "90%", "70%", "50%", "<50%"};
        for (int k = 0; k < 5; ++k) {
            p.fillRect(QRectF(lx, ly0 + k * lh / 5, 14, lh / 5 - 2), utilCol(vals[k]));
            p.setPen(txt);
            p.drawText(QRectF(lx - 56, ly0 + k * lh / 5, 52, lh / 5), Qt::AlignRight | Qt::AlignVCenter, names[k]);
        }
    }
    // pastki: yer ivmesi grafigi
    if (!ag_.empty()) {
        const QRectF gr(12, height() - 80, width() - 24, 68);
        p.fillRect(gr, QColor(16, 26, 50));
        p.setPen(QPen(QColor(40, 58, 96), 1));
        p.drawLine(QPointF(gr.left(), gr.center().y()), QPointF(gr.right(), gr.center().y()));
        double amax = 1e-9;
        for (double v : ag_) amax = std::max(amax, std::abs(v));
        QPainterPath path;
        const std::size_t N = ag_.size(), stp = std::max<std::size_t>(1, N / static_cast<std::size_t>(std::max(50.0, gr.width())));
        for (std::size_t k = 0; k < N; k += stp) {
            const QPointF q(gr.left() + gr.width() * k / (N - 1.0), gr.center().y() - ag_[k] / amax * (gr.height() / 2 - 4));
            k ? path.lineTo(q) : path.moveTo(q);
        }
        p.setPen(QPen(intensity_ >= 10 ? QColor(229, 72, 77) : QColor(240, 160, 48), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        const double xc = gr.left() + gr.width() * t_ / std::max(1e-9, duration());
        p.setPen(QPen(Qt::white, 1.5));
        p.drawLine(QPointF(xc, gr.top()), QPointF(xc, gr.bottom()));
        p.setPen(QColor(150, 170, 205));
        p.drawText(gr.adjusted(6, 2, -6, 0), Qt::AlignLeft | Qt::AlignTop, lab(4).arg(agNow / 9.81, 0, 'f', 2).arg(amax / 9.81, 0, 'f', 2));
    }
}

}  // namespace asiacad::gui
