#pragma once
// AsiaCAD DCL — AutoCAD Dialog Control Language: .dcl fayllarni o'qish va LISP dialog funksiyalari
// (load_dialog, new_dialog, action_tile, set_tile, get_tile, start_list ... start_dialog, done_dialog).
// Oynani chizish — DclUi (GUI Qt da beradi); UI bo'lmasa start_dialog "bekor" (0) qaytaradi.
#include <asiacad/lisp/lisp.h>

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace asiacad::lisp {

struct DclTile {
    std::string type;                          // dialog, button, edit_box, popup_list, list_box, toggle, radio_button, text,
                                               // row, column, boxed_row, boxed_column, radio_row, radio_column, boxed_radio_row ...
    std::map<std::string, std::string> attrs;  // key, label, value, width, list, is_default, is_cancel, mnemonic ...
    std::vector<DclTile> kids;
    std::string attr(const std::string& k, const std::string& def = {}) const {
        auto it = attrs.find(k);
        return it == attrs.end() ? def : it->second;
    }
};

// .dcl matnini o'qish: name -> dialog (prototiplar va base.dcl standart plitalari yoyiladi). Xato bo'lsa LispError.
std::map<std::string, DclTile> parseDcl(const std::string& src);

// Bitta ochiq dialog holati
struct DclSession {
    std::string name;
    DclTile dialog;
    std::map<std::string, std::string> values;            // kalit -> qiymat (get_tile / set_tile)
    std::map<std::string, std::string> actions;           // kalit -> LISP ifodasi (action_tile)
    std::map<std::string, std::vector<std::string>> lists;
    std::map<std::string, bool> enabled;
    std::map<std::string, std::string> clientData;
    std::string defaultAction;
    bool done = false;
    int status = 0;
    std::function<void(const std::string& key)> changed;  // UI ga: plita yangilansin (set_tile / mode_tile / ro'yxat)
    std::function<void(const std::string& key)> focus;
    const DclTile* find(const std::string& key) const;
};

class DclUi {
public:
    virtual ~DclUi() = default;
    // Dialogni modal ko'rsatadi; foydalanuvchi harakatida s.fire(key, value, reason) chaqiriladi;
    // s.done bo'lganda yopiladi. Qaytaradi — done_dialog holati.
    virtual int run(class DclRuntime& rt, DclSession& s) = 0;
};

class DclRuntime {
public:
    explicit DclRuntime(Lisp& L) : L_(L) {}
    void install();                // LISP funksiyalari
    DclUi* ui = nullptr;
    // UI dan: plita harakati (qiymat o'zgardi / tugma bosildi). reason: 1 — tanlandi, 2 — tahrirdan chiqildi, 3 — slayder, 4 — ikki marta bosish
    void fire(DclSession& s, const std::string& key, const std::string& value, int reason = 1);
    DclSession* current() { return stack_.empty() ? nullptr : &stack_.back(); }

private:
    Lisp& L_;
    std::map<int, std::map<std::string, DclTile>> files_;
    int nextId_ = 1;
    std::deque<DclSession> stack_;  // ichma-ich dialoglar
    std::string listKey_;
    int listOp_ = 3;
    int listIdx_ = 0;
    std::vector<std::string> listBuf_;
};

}  // namespace asiacad::lisp
