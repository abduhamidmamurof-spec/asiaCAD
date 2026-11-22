#pragma once
// AutoLISP DCL dialoglarini Qt oynasi sifatida ko'rsatish
#include <asiacad/lisp/dcl.h>

#include <QString>
#include <QWidget>
#include <functional>

namespace asiacad::gui {

class DclQtUi final : public lisp::DclUi {
public:
    DclQtUi(QWidget* parent, std::function<QString(const char*)> tr) : parent_(parent), tr_(std::move(tr)) {}
    int run(lisp::DclRuntime& rt, lisp::DclSession& s) override;
    QWidget* lastDialog = nullptr;  // sinov uchun: ochiq dialog

private:
    QWidget* parent_;
    std::function<QString(const char*)> tr_;
};

}  // namespace asiacad::gui
