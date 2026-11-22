// AsiaCAD GUI — Video: kamera yo'li, real-vaqt ko'rish, yozib olish, eksport.
#include "video_dialog.h"

#include "model3d_view.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace asiacad::gui {

QString VideoDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

VideoDialog::VideoDialog(const i18n::Localizer& loc, Model3DView* view, std::function<std::vector<Vec3>()> pathPoints,
                         std::function<QString()> selectionIds, QWidget* parent)
    : QDialog(parent), loc_(loc), view_(view), points_(std::move(pathPoints)), ids_(std::move(selectionIds)) {
    setWindowTitle(t("gui.video.title"));
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    src_ = new QComboBox;
    src_->addItem(t("gui.video.walk"));
    src_->addItem(t("gui.video.orbit"));
    form->addRow(t("gui.video.source"), src_);
    eye_ = new QDoubleSpinBox; eye_->setRange(0.5, 300); eye_->setDecimals(1); eye_->setValue(1.7); eye_->setSuffix(" m");
    speed_ = new QDoubleSpinBox; speed_->setRange(0.2, 60); speed_->setDecimals(1); speed_->setValue(1.4); speed_->setSuffix(" m/s");
    ahead_ = new QDoubleSpinBox; ahead_->setRange(1, 200); ahead_->setDecimals(0); ahead_->setValue(8); ahead_->setSuffix(" m");
    orbitSec_ = new QDoubleSpinBox; orbitSec_->setRange(4, 600); orbitSec_->setDecimals(0); orbitSec_->setValue(20); orbitSec_->setSuffix(" s");
    fps_ = new QSpinBox; fps_->setRange(10, 60); fps_->setValue(30);
    size_ = new QComboBox;
    size_->addItem("1280×720", QSize(1280, 720));
    size_->addItem("1920×1080", QSize(1920, 1080));
    size_->addItem("3840×2160", QSize(3840, 2160));
    size_->setCurrentIndex(1);
    form->addRow(t("gui.video.eye"), eye_);
    form->addRow(t("gui.video.speed"), speed_);
    form->addRow(t("gui.video.ahead"), ahead_);
    form->addRow(t("gui.video.seconds"), orbitSec_);
    form->addRow(t("gui.video.fps"), fps_);
    form->addRow(t("gui.video.size"), size_);
    lay->addLayout(form);
    auto* row = new QHBoxLayout;
    auto* play = new QPushButton(t("gui.video.play"));
    auto* stop = new QPushButton(t("gui.video.stop"));
    auto* rec = new QPushButton(t("gui.video.record"));
    auto* exp = new QPushButton(t("gui.video.export"));
    for (auto* b : {play, stop, rec, exp}) row->addWidget(b);
    lay->addLayout(row);
    status_ = new QLabel;
    status_->setWordWrap(true);
    lay->addWidget(status_);
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] {
        playT_ += timer_->interval() / 1000.0;
        if (playT_ > path_.duration()) { timer_->stop(); return; }
        showFrame(playT_);
    });
    connect(play, &QPushButton::clicked, this, [this] {
        if (!buildPath()) return;
        playT_ = 0;
        showFrame(0);
        timer_->start(1000 / std::max(10, fps_->value()));
    });
    connect(stop, &QPushButton::clicked, this, [this] { timer_->stop(); });
    connect(rec, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, t("gui.video.record"), QString(), QStringLiteral("MP4 (*.mp4);;PNG (*.png)"));
        if (!f.isEmpty()) record(f);
    });
    connect(exp, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, t("gui.video.export"), QString(), QStringLiteral("Unreal (*.py);;3ds Max (*.ms)"));
        if (f.isEmpty()) return;
        const QFileInfo fi(f);
        const QString gltf = fi.absolutePath() + "/" + fi.completeBaseName() + ".gltf";
        QString line = QStringLiteral("EXPORT3D \"%1\" %2=\"%3\" FPS=%4").arg(gltf, fi.suffix().toLower() == "ms" ? "MAX" : "UNREAL", f).arg(fps_->value());
        if (src_->currentIndex() == 0 && !ids_().isEmpty())
            line += QStringLiteral(" CAMPATH=%1 EYE=%2 SPEED=%3 AHEAD=%4").arg(ids_()).arg(eye_->value()).arg(speed_->value()).arg(ahead_->value());
        else line += QStringLiteral(" ORBIT=%1").arg(orbitSec_->value());
        emit commandReady(line);
    });
    auto sync = [this] {
        const bool walk = src_->currentIndex() == 0;
        speed_->setEnabled(walk);
        ahead_->setEnabled(walk);
        eye_->setEnabled(walk);
        orbitSec_->setEnabled(!walk);
    };
    connect(src_, &QComboBox::currentIndexChanged, this, sync);
    sync();
}

void VideoDialog::setSource(int i) { src_->setCurrentIndex(i); }

bool VideoDialog::buildPath() {
    if (src_->currentIndex() == 0) {
        const auto pts = points_();
        if (pts.size() < 2) { status_->setText(t("gui.video.nopath")); return false; }
        path_ = bim::walkPath(pts, eye_->value(), speed_->value(), ahead_->value(), 0.25);
    } else {
        P3 mn, mx;
        if (!view_ || !view_->sceneBounds(mn, mx)) { status_->setText(t("gui.video.empty")); return false; }
        const double span = std::max({mx.x - mn.x, mx.y - mn.y, 10.0});
        path_ = bim::orbitPath({(mn.x + mx.x) / 2, (mn.y + mx.y) / 2, mn.z}, span * 0.8, (mx.z - mn.z) + span * 0.35, orbitSec_->value(),
                               mn.z + (mx.z - mn.z) * 0.3, 0.25);
    }
    path_.fps = fps_->value();
    status_->setText(status());
    return !path_.keys.empty();
}

QString VideoDialog::status() const {
    return t("gui.video.status", {QString::number(path_.duration(), 'f', 1), QString::number(static_cast<int>(std::ceil(path_.duration() * path_.fps)))});
}

void VideoDialog::showFrame(double tt) {
    if (!view_ || path_.keys.empty()) return;
    const auto k = bim::cameraAt(path_, tt);
    view_->setCamera({k.eye.x, k.eye.y, k.eye.z}, {k.target.x, k.target.y, k.target.z});
}

int VideoDialog::record(const QString& out, int maxFrames) {
    if (!buildPath() || !view_) return -1;
    timer_->stop();
    const QSize sz = size_->currentData().toSize();
    const int n = static_cast<int>(std::ceil(path_.duration() * path_.fps)) + 1;
    const int total = maxFrames > 0 ? std::min(n, maxFrames) : n;
    const QString ff = QStandardPaths::findExecutable("ffmpeg");
    const bool mp4 = out.toLower().endsWith(".mp4") && !ff.isEmpty();
    QTemporaryDir tmp;
    const QFileInfo fi(out);
    const QString dir = mp4 ? tmp.path() : fi.absolutePath() + "/" + fi.completeBaseName() + "_kadrlar";
    QDir().mkpath(dir);
    for (int i = 0; i < total; ++i) {
        showFrame(i / path_.fps);
        QApplication::processEvents();
        QImage im = view_->snapshot();
        if (im.isNull()) return -1;
        im = im.scaled(sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QRect crop((im.width() - sz.width()) / 2, (im.height() - sz.height()) / 2, sz.width(), sz.height());
        im.copy(crop).save(QStringLiteral("%1/kadr_%2.png").arg(dir).arg(i, 5, 10, QChar('0')));
        if (i % 10 == 0) { status_->setText(t("gui.video.recording", {QString::number(i), QString::number(total)})); QApplication::processEvents(); }
    }
    if (mp4) {
        QProcess p;
        p.start(ff, {"-y", "-framerate", QString::number(path_.fps), "-i", dir + "/kadr_%05d.png", "-c:v", "libx264", "-preset", "veryfast", "-crf", "20",
                     "-pix_fmt", "yuv420p", out});
        p.waitForFinished(-1);
        if (p.exitCode() != 0) { status_->setText(t("gui.video.ffmpeg_fail")); return -1; }
        status_->setText(t("gui.video.done_mp4", {out}));
    } else {
        status_->setText(t("gui.video.done_png", {dir}));
    }
    return total;
}

}  // namespace asiacad::gui
