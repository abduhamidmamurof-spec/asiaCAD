// AutoLISP DCL: plitalar daraxtidan QDialog quriladi; harakatlar DclRuntime::fire orqali LISP ga qaytadi.
#include "dcl_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QVBoxLayout>

#include <map>
#include <sstream>

namespace asiacad::gui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
bool truthy(const std::string& v) { return v == "true" || v == "1"; }

struct Builder {
    lisp::DclRuntime& rt;
    lisp::DclSession& s;
    QDialog& dlg;
    std::function<QString(const char*)> tr;
    std::map<std::string, std::function<void()>> refresh;  // kalit -> vidjetni yangilash
    std::map<std::string, QWidget*> widgets;
    std::string error;
    bool busy = false;

    void fire(const std::string& key, const std::string& value, int reason) {
        if (busy) { s.values[key] = value; return; }
        busy = true;
        try {
            rt.fire(s, key, value, reason);
        } catch (const std::exception& e) {
            error = e.what();
            s.done = true;
            s.status = 0;
        }
        busy = false;
        if (s.done) dlg.done(s.status);
    }
    void reg(const std::string& key, QWidget* w, std::function<void()> f) {
        if (key.empty()) return;
        widgets[key] = w;
        refresh[key] = [this, key, w, f] {
            if (f) f();
            auto it = s.enabled.find(key);
            w->setEnabled(it == s.enabled.end() || it->second);
        };
    }
    QString label(const lisp::DclTile& t, bool std) const {
        const std::string k = t.attr("key");
        if (std) {
            if (k == "accept") return tr("gui.dcl.ok");
            if (k == "cancel") return tr("gui.dcl.cancel");
            if (k == "help") return tr("gui.dcl.help");
            if (k == "info") return tr("gui.dcl.info");
        }
        return qs(t.attr("label"));
    }
    static int chars(const lisp::DclTile& t, const char* attr) { return static_cast<int>(std::atof(t.attr(attr, "0").c_str())); }

    void build(const lisp::DclTile& t, QBoxLayout* lay, bool std, QButtonGroup* radios, const std::string& radioKey) {
        const std::string& ty = t.type;
        const std::string key = t.attr("key");
        const bool isStd = std || t.attrs.count("$std");
        if (ty == "row" || ty == "column" || ty == "boxed_row" || ty == "boxed_column" || ty == "radio_row" || ty == "radio_column" ||
            ty == "boxed_radio_row" || ty == "boxed_radio_column" || ty == "concatenation" || ty == "paragraph") {
            const bool horiz = ty.find("row") != std::string::npos || ty == "concatenation";
            QBoxLayout* inner = horiz ? static_cast<QBoxLayout*>(new QHBoxLayout) : new QVBoxLayout;
            QWidget* holder;
            if (ty.rfind("boxed", 0) == 0) {
                auto* g = new QGroupBox(qs(t.attr("label")));
                g->setLayout(inner);
                holder = g;
            } else {
                holder = new QWidget;
                inner->setContentsMargins(0, 0, 0, 0);
                holder->setLayout(inner);
            }
            QButtonGroup* grp = radios;
            std::string gkey = radioKey;
            if (ty.find("radio") != std::string::npos) {
                grp = new QButtonGroup(holder);
                grp->setExclusive(true);
                gkey = key;
                if (!key.empty()) {
                    reg(key, holder, [this, key, grp] {
                        const std::string v = s.values[key];
                        for (auto* b : grp->buttons())
                            if (b->property("dclKey").toString().toStdString() == v) b->setChecked(true);
                    });
                }
            }
            for (auto& k : t.kids) build(k, inner, isStd, grp, gkey);
            if (isStd) inner->insertStretch(0);
            lay->addWidget(holder);
            return;
        }
        if (ty == "button" || ty == "image_button") {
            auto* b = new QPushButton(label(t, isStd));
            if (truthy(t.attr("is_default"))) b->setDefault(true);
            if (ty == "image_button") b->setMinimumSize(chars(t, "width") * 7 + 20, chars(t, "height") * 14 + 10);
            QObject::connect(b, &QPushButton::clicked, &dlg, [this, key] { fire(key, "", 1); });
            if (truthy(t.attr("is_cancel"))) dlg.setProperty("dclCancel", qs(key));
            reg(key, b, {});
            lay->addWidget(b);
            return;
        }
        if (ty == "edit_box") {
            auto* row = new QHBoxLayout;
            if (!t.attr("label").empty()) row->addWidget(new QLabel(qs(t.attr("label"))));
            auto* e = new QLineEdit;
            if (int w = chars(t, "edit_width")) e->setMinimumWidth(w * 8 + 12);
            if (truthy(t.attr("password_char")) || !t.attr("password_char").empty()) e->setEchoMode(QLineEdit::Password);
            if (int ml = chars(t, "edit_limit")) e->setMaxLength(ml);
            row->addWidget(e, 1);
            lay->addLayout(row);
            reg(key, e, [this, key, e] { const QString v = qs(s.values[key]); if (e->text() != v) e->setText(v); });
            QObject::connect(e, &QLineEdit::textEdited, &dlg, [this, key](const QString& v) { s.values[key] = v.toStdString(); });
            QObject::connect(e, &QLineEdit::editingFinished, &dlg, [this, key, e] { if (!s.done) fire(key, e->text().toStdString(), 2); });
            return;
        }
        if (ty == "popup_list") {
            auto* row = new QHBoxLayout;
            if (!t.attr("label").empty()) row->addWidget(new QLabel(qs(t.attr("label"))));
            auto* c = new QComboBox;
            if (int w = chars(t, "edit_width")) c->setMinimumWidth(w * 8 + 30);
            row->addWidget(c, 1);
            lay->addLayout(row);
            reg(key, c, [this, key, c] {
                QStringList items;
                for (auto& x : s.lists[key]) items << qs(x);
                const QSignalBlocker blk(c);
                QStringList cur;
                for (int i = 0; i < c->count(); ++i) cur << c->itemText(i);
                if (cur != items) { c->clear(); c->addItems(items); }
                c->setCurrentIndex(std::atoi(s.values[key].c_str()));
            });
            QObject::connect(c, &QComboBox::currentIndexChanged, &dlg, [this, key](int i) { fire(key, std::to_string(i), 1); });
            return;
        }
        if (ty == "list_box") {
            if (!t.attr("label").empty()) lay->addWidget(new QLabel(qs(t.attr("label"))));
            auto* l = new QListWidget;
            const bool multi = truthy(t.attr("multiple_select"));
            l->setSelectionMode(multi ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
            if (int h = chars(t, "height")) l->setMinimumHeight(h * 16);
            if (int w = chars(t, "width")) l->setMinimumWidth(w * 8);
            lay->addWidget(l, 1);
            reg(key, l, [this, key, l] {
                const QSignalBlocker blk(l);
                QStringList items;
                for (auto& x : s.lists[key]) items << qs(x);
                QStringList cur;
                for (int i = 0; i < l->count(); ++i) cur << l->item(i)->text();
                if (cur != items) { l->clear(); l->addItems(items); }
                l->clearSelection();
                std::stringstream ss(s.values[key]);
                int i;
                while (ss >> i) if (i >= 0 && i < l->count()) l->item(i)->setSelected(true);
            });
            auto val = [l] {
                std::string v;
                for (int i = 0; i < l->count(); ++i) if (l->item(i)->isSelected()) v += (v.empty() ? "" : " ") + std::to_string(i);
                return v;
            };
            QObject::connect(l, &QListWidget::itemSelectionChanged, &dlg, [this, key, val] { fire(key, val(), 1); });
            QObject::connect(l, &QListWidget::itemDoubleClicked, &dlg, [this, key, val] { fire(key, val(), 4); });
            return;
        }
        if (ty == "toggle") {
            auto* c = new QCheckBox(qs(t.attr("label")));
            lay->addWidget(c);
            reg(key, c, [this, key, c] { const QSignalBlocker blk(c); c->setChecked(s.values[key] == "1"); });
            QObject::connect(c, &QCheckBox::toggled, &dlg, [this, key](bool on) { fire(key, on ? "1" : "0", 1); });
            return;
        }
        if (ty == "radio_button") {
            auto* r = new QRadioButton(qs(t.attr("label")));
            r->setProperty("dclKey", qs(key));
            if (radios) radios->addButton(r);
            lay->addWidget(r);
            reg(key, r, [this, key, r] { const QSignalBlocker blk(r); if (s.values[key] == "1") r->setChecked(true); });
            QObject::connect(r, &QRadioButton::toggled, &dlg, [this, key, radioKey](bool on) {
                s.values[key] = on ? "1" : "0";
                if (!on) return;
                if (!radioKey.empty()) s.values[radioKey] = key;
                fire(key, "1", 1);
                if (!radioKey.empty() && !s.done && s.actions.count(radioKey)) fire(radioKey, key, 1);
            });
            return;
        }
        if (ty == "text" || ty == "text_part" || ty == "errtile") {
            auto* l = new QLabel(qs(t.attr("label")));
            l->setWordWrap(true);
            if (ty == "errtile") l->setStyleSheet("color:#d9534f;");
            lay->addWidget(l);
            reg(key, l, [this, key, l, t] {
                const std::string v = s.values[key];
                l->setText(qs(v.empty() ? t.attr("label") : v));
            });
            return;
        }
        if (ty == "slider") {
            auto* sl = new QSlider(t.attr("layout") == "vertical" ? Qt::Vertical : Qt::Horizontal);
            sl->setRange(std::atoi(t.attr("min_value", "0").c_str()), std::atoi(t.attr("max_value", "10000").c_str()));
            lay->addWidget(sl);
            reg(key, sl, [this, key, sl] { const QSignalBlocker blk(sl); sl->setValue(std::atoi(s.values[key].c_str())); });
            QObject::connect(sl, &QSlider::valueChanged, &dlg, [this, key](int v) { fire(key, std::to_string(v), 3); });
            return;
        }
        if (ty == "image") {
            auto* l = new QLabel;
            l->setMinimumSize(chars(t, "width") * 7 + 10, chars(t, "height") * 14 + 10);
            l->setStyleSheet("background:#202020;border:1px solid #555;");
            lay->addWidget(l);
            return;
        }
        if (ty == "spacer") { lay->addSpacing(8); return; }
        // noma'lum plita: ichidagilar ustun sifatida
        for (auto& k : t.kids) build(k, lay, isStd, radios, radioKey);
    }
};
}  // namespace

int DclQtUi::run(lisp::DclRuntime& rt, lisp::DclSession& s) {
    QDialog dlg(parent_);
    dlg.setWindowTitle(qs(s.dialog.attr("label", "AutoLISP")));
    auto* lay = new QVBoxLayout(&dlg);
    Builder b{rt, s, dlg, tr_, {}, {}, {}, false};
    for (auto& k : s.dialog.kids) b.build(k, lay, false, nullptr, {});
    for (auto& [k, f] : b.refresh) f();
    s.changed = [&b](const std::string& key) { if (auto it = b.refresh.find(key); it != b.refresh.end()) it->second(); };
    s.focus = [&b](const std::string& key) { if (auto it = b.widgets.find(key); it != b.widgets.end()) it->second->setFocus(); };
    s.done = false;
    QObject::connect(&dlg, &QDialog::rejected, &dlg, [&] {
        if (s.done) return;
        const QString ck = dlg.property("dclCancel").toString();
        b.fire(ck.isEmpty() ? std::string("cancel") : ck.toStdString(), "", 1);
        if (!s.done) { s.done = true; s.status = 0; }
    });
    lastDialog = &dlg;
    dlg.exec();
    lastDialog = nullptr;
    s.changed = nullptr;
    s.focus = nullptr;
    if (!b.error.empty()) throw lisp::LispError(b.error);
    return s.status;
}

}  // namespace asiacad::gui
