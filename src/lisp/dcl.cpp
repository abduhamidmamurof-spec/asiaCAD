// AsiaCAD DCL: .dcl o'quvchi va dialog funksiyalari.
#include <asiacad/lisp/dcl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>

namespace asiacad::lisp {

namespace {
[[noreturn]] void fail(const std::string& m) { throw LispError("DCL: " + m); }

struct Tok {
    enum K { Id, Str, Num, Punct, End } k;
    std::string s;
};

std::vector<Tok> lex(const std::string& src) {
    std::vector<Tok> t;
    std::size_t i = 0;
    const std::size_t n = src.size();
    while (i < n) {
        const char c = src[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '/' && i + 1 < n && src[i + 1] == '/') { while (i < n && src[i] != '\n') ++i; continue; }
        if (c == '/' && i + 1 < n && src[i + 1] == '*') { auto e = src.find("*/", i + 2); i = e == std::string::npos ? n : e + 2; continue; }
        if (c == '"') {
            std::string s;
            ++i;
            while (i < n && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < n) {
                    const char e = src[++i];
                    s += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                } else s += src[i];
                ++i;
            }
            ++i;
            t.push_back({Tok::Str, s});
            continue;
        }
        if (c == ':' || c == '{' || c == '}' || c == ';' || c == '=') { t.push_back({Tok::Punct, std::string(1, c)}); ++i; continue; }
        if (std::isdigit(static_cast<unsigned char>(c)) || ((c == '-' || c == '.') && i + 1 < n && std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
            std::size_t b = i++;
            while (i < n && (std::isdigit(static_cast<unsigned char>(src[i])) || src[i] == '.')) ++i;
            t.push_back({Tok::Num, src.substr(b, i - b)});
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '@' || c == '$') {
            std::size_t b = i++;
            while (i < n && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_' || src[i] == '-' || src[i] == '.')) ++i;
            t.push_back({Tok::Id, src.substr(b, i - b)});
            continue;
        }
        ++i;  // noma'lum belgi — o'tkaziladi
    }
    t.push_back({Tok::End, ""});
    return t;
}

std::string lowerS(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

struct Parser {
    std::vector<Tok> t;
    std::size_t p = 0;
    std::map<std::string, DclTile> protos;  // nomlangan prototiplar (dialog ham)
    const Tok& cur() const { return t[p]; }
    bool isP(const char* s) const { return cur().k == Tok::Punct && cur().s == s; }
    void expect(const char* s) {
        if (!isP(s)) fail(std::string("'") + s + "' expected near '" + cur().s + "'");
        ++p;
    }
    // ':' type '{' body '}' — type allaqachon o'qilgan
    DclTile body(const std::string& type) {
        DclTile tl;
        tl.type = lowerS(type);
        expect("{");
        while (!isP("}") && cur().k != Tok::End) {
            if (isP(":")) {
                ++p;
                const std::string ty = cur().s;
                ++p;
                tl.kids.push_back(body(ty));
            } else if (cur().k == Tok::Id) {
                const std::string id = cur().s;
                ++p;
                if (isP("=")) {
                    ++p;
                    tl.attrs[lowerS(id)] = cur().s;
                    ++p;
                    if (isP(";")) ++p;
                } else if (isP(";")) {
                    ++p;
                    DclTile r;
                    r.type = lowerS(id);
                    tl.kids.push_back(r);  // havola: ok_cancel; spacer;
                } else if (isP(":")) {     // nomlangan ichki plita: name : type { }
                    ++p;
                    const std::string ty = cur().s;
                    ++p;
                    tl.kids.push_back(body(ty));
                } else fail("unexpected '" + cur().s + "'");
            } else if (isP(";")) ++p;
            else fail("unexpected '" + cur().s + "'");
        }
        expect("}");
        if (isP(";")) ++p;
        return tl;
    }
    void file() {
        while (cur().k != Tok::End) {
            if (cur().k == Tok::Id && cur().s == "@include") { p += 2; continue; }
            if (cur().k != Tok::Id) { ++p; continue; }
            const std::string name = cur().s;
            ++p;
            if (!isP(":")) { if (isP(";")) ++p; continue; }
            ++p;
            const std::string ty = cur().s;
            ++p;
            DclTile d = body(ty);
            d.attrs["$name"] = name;
            protos[lowerS(name)] = std::move(d);
        }
    }
};

// base.dcl standart plitalari
DclTile button(const std::string& key, const std::string& label, bool def, bool cancel) {
    DclTile b;
    b.type = "button";
    b.attrs["key"] = key;
    b.attrs["label"] = label;
    if (def) b.attrs["is_default"] = "true";
    if (cancel) b.attrs["is_cancel"] = "true";
    return b;
}
bool builtinRef(const std::string& name, DclTile& out) {
    auto row = [&](std::vector<DclTile> k) { out = DclTile{}; out.type = "row"; out.attrs["$std"] = name; out.kids = std::move(k); return true; };
    if (name == "ok_only") return row({button("accept", "OK", true, true)});
    if (name == "ok_cancel") return row({button("accept", "OK", true, false), button("cancel", "Cancel", false, true)});
    if (name == "ok_cancel_help") return row({button("accept", "OK", true, false), button("cancel", "Cancel", false, true), button("help", "Help...", false, false)});
    if (name == "ok_cancel_help_info") return row({button("accept", "OK", true, false), button("cancel", "Cancel", false, true), button("help", "Help...", false, false), button("info", "Info...", false, false)});
    if (name == "ok_cancel_help_errtile") {
        row({button("accept", "OK", true, false), button("cancel", "Cancel", false, true), button("help", "Help...", false, false)});
        DclTile e; e.type = "errtile"; e.attrs["key"] = "error";
        DclTile col; col.type = "column"; col.kids = {out, e};
        out = col;
        return true;
    }
    if (name == "errtile") { out = DclTile{}; out.type = "errtile"; out.attrs["key"] = "error"; return true; }
    if (name == "spacer" || name == "spacer_0" || name == "spacer_1") { out = DclTile{}; out.type = "spacer"; return true; }
    return false;
}
// Prototip / standart plitalarni yoyish
void expand(DclTile& t, const std::map<std::string, DclTile>& protos, int depth = 0) {
    if (depth > 50) fail("prototype recursion");
    if (t.type != "dialog") {
        DclTile b;
        if (auto it = protos.find(t.type); it != protos.end() && it->second.type != t.type) {
            DclTile base = it->second;
            for (auto& [k, v] : t.attrs) base.attrs[k] = v;
            for (auto& k : t.kids) base.kids.push_back(k);
            base.attrs.erase("$name");
            t = base;
            expand(t, protos, depth + 1);
            return;
        }
        if (t.kids.empty() && builtinRef(t.type, b)) {
            for (auto& [k, v] : t.attrs) b.attrs[k] = v;
            t = b;
        }
    }
    for (auto& k : t.kids) expand(k, protos, depth + 1);
}
}  // namespace

std::map<std::string, DclTile> parseDcl(const std::string& src) {
    Parser ps;
    ps.t = lex(src);
    ps.file();
    std::map<std::string, DclTile> out;
    for (auto& [n, d] : ps.protos) {
        if (d.type != "dialog") continue;
        DclTile x = d;
        expand(x, ps.protos);
        out[n] = std::move(x);
    }
    return out;
}

const DclTile* DclSession::find(const std::string& key) const {
    std::function<const DclTile*(const DclTile&)> rec = [&](const DclTile& t) -> const DclTile* {
        if (t.attr("key") == key) return &t;
        for (auto& k : t.kids) if (auto* r = rec(k)) return r;
        return nullptr;
    };
    return rec(dialog);
}

void DclRuntime::fire(DclSession& s, const std::string& key, const std::string& value, int reason) {
    s.values[key] = value;
    std::string expr;
    if (auto it = s.actions.find(key); it != s.actions.end()) expr = it->second;
    else if (key == "accept" || key == "cancel") {
        s.done = true;
        s.status = key == "accept" ? 1 : 0;
        return;
    } else expr = s.defaultAction;
    if (expr.empty()) return;
    L_.setGlobal("$KEY", Lisp::mkStr(key));
    L_.setGlobal("$VALUE", Lisp::mkStr(value));
    L_.setGlobal("$REASON", Lisp::mkInt(reason));
    L_.setGlobal("$DATA", s.clientData.count(key) ? Lisp::mkStr(s.clientData[key]) : nullptr);
    L_.setGlobal("$X", Lisp::mkInt(0));
    L_.setGlobal("$Y", Lisp::mkInt(0));
    L_.evalString(expr);
}

void DclRuntime::install() {
    using A = std::vector<V>;
    L_.defineBuiltin("LOAD_DIALOG", [this](Lisp& L, A& a) -> V {
        if (a.size() != 1) fail("load_dialog: 1 argument");
        std::string f = Lisp::str(a[0], "load_dialog");
        std::string path;
        if (L.host()) {
            path = L.host()->findFile(f);
            if (path.empty() && f.find('.') == std::string::npos) path = L.host()->findFile(f + ".dcl");
        } else path = f;
        std::ifstream in(path, std::ios::binary);
        if (path.empty() || !in) return Lisp::mkInt(-1);
        std::stringstream ss;
        ss << in.rdbuf();
        const int id = nextId_++;
        files_[id] = parseDcl(ss.str());
        return Lisp::mkInt(id);
    });
    L_.defineBuiltin("UNLOAD_DIALOG", [this](Lisp&, A& a) -> V { if (!a.empty() && Lisp::isNum(a[0])) files_.erase(static_cast<int>(Lisp::num(a[0], ""))); return nullptr; });
    L_.defineBuiltin("NEW_DIALOG", [this](Lisp& L, A& a) -> V {
        if (a.size() < 2) fail("new_dialog: too few arguments");
        const std::string name = lowerS(Lisp::str(a[0], "new_dialog"));
        auto f = files_.find(static_cast<int>(Lisp::num(a[1], "new_dialog")));
        if (f == files_.end()) return nullptr;
        auto d = f->second.find(name);
        if (d == f->second.end()) return nullptr;
        DclSession s;
        s.name = name;
        s.dialog = d->second;
        if (a.size() > 2 && a[2] && a[2]->kind == Kind::Str) s.defaultAction = a[2]->s;
        std::function<void(const DclTile&)> init = [&](const DclTile& t) {
            const std::string k = t.attr("key");
            if (!k.empty()) {
                s.values[k] = t.attr("value");
                if (t.attrs.count("list")) {
                    std::vector<std::string> items;
                    std::stringstream ss(t.attr("list"));
                    std::string ln;
                    while (std::getline(ss, ln, '\n')) items.push_back(ln);
                    s.lists[k] = items;
                }
                s.enabled[k] = t.attr("is_enabled", "true") != "false";
            }
            for (auto& c : t.kids) init(c);
        };
        init(s.dialog);
        stack_.push_back(std::move(s));
        return L.T();
    });
    L_.defineBuiltin("START_DIALOG", [this](Lisp& L, A&) -> V {
        if (stack_.empty()) fail("start_dialog: no dialog");
        DclSession& s = stack_.back();
        int st = 0;
        if (ui) st = ui->run(*this, s);
        else {
            const std::string w = "DCL: " + s.name;
            if (std::find(L.warnings.begin(), L.warnings.end(), w) == L.warnings.end()) L.warnings.push_back(w);
        }
        stack_.pop_back();
        return Lisp::mkInt(st);
    });
    L_.defineBuiltin("DONE_DIALOG", [this](Lisp&, A& a) -> V {
        if (stack_.empty()) return nullptr;
        auto& s = stack_.back();
        s.done = true;
        s.status = a.empty() || !a[0] ? 1 : static_cast<int>(Lisp::num(a[0], ""));
        return Lisp::list({Lisp::mkInt(0), Lisp::mkInt(0)});
    });
    L_.defineBuiltin("TERM_DIALOG", [this](Lisp&, A&) -> V {
        for (auto& s : stack_) { s.done = true; s.status = 0; }
        return nullptr;
    });
    L_.defineBuiltin("ACTION_TILE", [this](Lisp& L, A& a) -> V {
        if (stack_.empty() || a.size() < 2) return nullptr;
        stack_.back().actions[Lisp::str(a[0], "action_tile")] = Lisp::str(a[1], "action_tile");
        return L.T();
    });
    L_.defineBuiltin("SET_TILE", [this](Lisp&, A& a) -> V {
        if (stack_.empty() || a.size() < 2) return nullptr;
        auto& s = stack_.back();
        const std::string k = Lisp::str(a[0], "set_tile"), v = Lisp::str(a[1], "set_tile");
        s.values[k] = v;
        if (s.changed) s.changed(k);
        return a[1];
    });
    L_.defineBuiltin("GET_TILE", [this](Lisp&, A& a) -> V {
        if (stack_.empty() || a.empty()) return nullptr;
        auto& s = stack_.back();
        auto it = s.values.find(Lisp::str(a[0], "get_tile"));
        return it == s.values.end() ? nullptr : Lisp::mkStr(it->second);
    });
    L_.defineBuiltin("GET_ATTR", [this](Lisp&, A& a) -> V {
        if (stack_.empty() || a.size() < 2) return nullptr;
        const DclTile* t = stack_.back().find(Lisp::str(a[0], "get_attr"));
        if (!t) return nullptr;
        const std::string at = lowerS(Lisp::str(a[1], "get_attr"));
        return t->attrs.count(at) ? Lisp::mkStr(t->attr(at)) : nullptr;
    });
    L_.defineBuiltin("MODE_TILE", [this](Lisp&, A& a) -> V {
        if (stack_.empty() || a.size() < 2) return nullptr;
        auto& s = stack_.back();
        const std::string k = Lisp::str(a[0], "mode_tile");
        const int m = static_cast<int>(Lisp::num(a[1], "mode_tile"));
        if (m == 0) s.enabled[k] = true;
        else if (m == 1) s.enabled[k] = false;
        else if (m == 4) s.enabled[k] = !s.enabled[k];
        else if ((m == 2 || m == 3) && s.focus) s.focus(k);
        if (s.changed) s.changed(k);
        return nullptr;
    });
    L_.defineBuiltin("CLIENT_DATA_TILE", [this](Lisp&, A& a) -> V {
        if (stack_.empty() || a.size() < 2) return nullptr;
        stack_.back().clientData[Lisp::str(a[0], "")] = Lisp::str(a[1], "");
        return nullptr;
    });
    L_.defineBuiltin("START_LIST", [this](Lisp&, A& a) -> V {
        if (a.empty()) return nullptr;
        listKey_ = Lisp::str(a[0], "start_list");
        listOp_ = a.size() > 1 && a[1] ? static_cast<int>(Lisp::num(a[1], "")) : 3;
        listIdx_ = a.size() > 2 && a[2] ? static_cast<int>(Lisp::num(a[2], "")) : 0;
        listBuf_.clear();
        return a[0];
    });
    L_.defineBuiltin("ADD_LIST", [this](Lisp&, A& a) -> V {
        if (a.empty()) return nullptr;
        listBuf_.push_back(Lisp::str(a[0], "add_list"));
        return a[0];
    });
    L_.defineBuiltin("END_LIST", [this](Lisp&, A&) -> V {
        if (stack_.empty() || listKey_.empty()) return nullptr;
        auto& s = stack_.back();
        auto& l = s.lists[listKey_];
        if (listOp_ == 3) l = listBuf_;
        else if (listOp_ == 2) l.insert(l.end(), listBuf_.begin(), listBuf_.end());
        else if (listOp_ == 1 && !listBuf_.empty() && listIdx_ >= 0 && listIdx_ < static_cast<int>(l.size())) l[static_cast<std::size_t>(listIdx_)] = listBuf_.front();
        if (s.changed) s.changed(listKey_);
        listKey_.clear();
        return nullptr;
    });
    // rasm plitalari — ko'rsatilmaydi (o'lcham qaytariladi)
    for (const char* n : {"START_IMAGE", "END_IMAGE", "FILL_IMAGE", "VECTOR_IMAGE", "SLIDE_IMAGE"}) L_.defineBuiltin(n, [](Lisp&, A& a) -> V { return a.empty() ? nullptr : a.back(); });
    L_.defineBuiltin("DIMX_TILE", [](Lisp&, A&) -> V { return Lisp::mkInt(120); });
    L_.defineBuiltin("DIMY_TILE", [](Lisp&, A&) -> V { return Lisp::mkInt(60); });
}

}  // namespace asiacad::lisp
