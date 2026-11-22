#pragma once
// AsiaCAD AutoLISP — AutoCAD AutoLISP / Visual LISP (vl-*) tilining yadrosi.
// O'quvchi (reader), dinamik ko'rinish sohali baholovchi, ~250 tayyor funksiya.
// Chizma bilan ishlash (command, get*, ent*, ss*, tbl*, getvar) — LispHost orqali (CLI/GUI beradi).
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace asiacad::lisp {

struct Value;
using V = std::shared_ptr<Value>;  // nullptr = nil
class Lisp;
using Builtin = std::function<V(Lisp&, std::vector<V>&)>;

enum class Kind { Int, Real, Str, Sym, Cons, Ename, PickSet, Subr, Lambda, File, Variant };

struct Value {
    Kind kind;
    long long i = 0;
    double r = 0;
    std::string s;            // Str, Sym (katta harf), Subr nomi
    V car, cdr;               // Cons
    std::uint64_t ename = 0;  // Ename
    std::shared_ptr<std::vector<std::uint64_t>> ss;  // PickSet
    Builtin fn;               // Subr
    V params, body;           // Lambda: (argumentlar / lokal) va tana
    std::shared_ptr<std::FILE> file;
    explicit Value(Kind k) : kind(k) {}
};

// LISP xatosi: AutoLISP matnlari (masalan "bad argument type: numberp: \"a\"") — *error* ga shu matn beriladi
struct LispError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
// (exit) / (quit) — "quit / exit abort"
struct LispExit : LispError {
    LispExit() : LispError("quit / exit abort") {}
};

// Chizma bilan bog'lanish
class LispHost {
public:
    virtual ~LispHost() = default;
    // (command ...) — tayyor bo'laklar: "LINE" "0,0" "10,0" "" ... ; "" = Enter (buyruq tugadi)
    virtual void command(const std::vector<std::string>& tokens) = 0;
    // getpoint/getreal ... — kiritish navbatidan keyingi bo'lak; bo'sh optional — kiritish yo'q (nil)
    virtual bool nextInput(std::string& out, const std::string& prompt) = 0;
    virtual void alert(const std::string& msg) = 0;
    // Obyektlar: DXF guruh ro'yxati ((0 . "LINE") (8 . "0") (10 x y z) ...)
    virtual std::uint64_t entLast() = 0;
    virtual std::uint64_t entNext(std::uint64_t after) = 0;  // 0 — birinchi
    virtual V entGet(Lisp& L, std::uint64_t id) = 0;          // nil — yo'q
    virtual std::uint64_t entMake(Lisp& L, const V& dxf) = 0; // 0 — yaratilmadi
    virtual bool entMod(Lisp& L, const V& dxf) = 0;
    virtual bool entDel(std::uint64_t id) = 0;
    // ssget: mode ("X", "_X", "L", "P", "W", "C", "" ...), nuqtalar, filtr (DXF ro'yxat)
    virtual std::vector<std::uint64_t> ssGet(Lisp& L, const std::string& mode, const std::vector<V>& pts, const V& filter) = 0;
    virtual V tblSearch(Lisp& L, const std::string& table, const std::string& name) = 0;
    virtual V tblNext(Lisp& L, const std::string& table, bool rewind) = 0;
    virtual V getVar(Lisp& L, const std::string& name) = 0;
    virtual bool setVar(Lisp& L, const std::string& name, const V& val) = 0;
    virtual std::string findFile(const std::string& name) = 0;
};

bool lispWcmatch(const std::string& s, const std::string& pat);  // AutoLISP wcmatch

class Lisp {
public:
    explicit Lisp(std::ostream& out, LispHost* host = nullptr);

    // Matnni o'qib, barcha ifodalarni bajaradi; oxirgi natija
    V evalString(const std::string& src);
    // .lsp faylni yuklash (load); topilmasa LispError
    V loadFile(const std::string& path);
    V eval(const V& x);
    V apply(const V& f, std::vector<V>& args);
    std::vector<V> read(const std::string& src);   // barcha ifodalar

    // C:XXX buyruqlari (katta harfda, "C:" siz)
    std::vector<std::string> commands() const;
    bool hasCommand(const std::string& name) const;
    V callCommand(const std::string& name);

    // Belgilar
    V sym(const std::string& name);  // intern (katta harf)
    V T() const { return t_; }
    V getGlobal(const std::string& name) const;
    void setGlobal(const std::string& name, V v);
    void defineBuiltin(const std::string& name, Builtin fn);

    // Konstruktorlar
    static V mkInt(long long i);
    static V mkReal(double r);
    static V mkStr(std::string s);
    static V cons(V a, V d);
    static V list(std::initializer_list<V> xs);
    static V fromVector(const std::vector<V>& xs, V tail = nullptr);
    static std::vector<V> toVector(const V& l);
    V mkEname(std::uint64_t id) const;
    V mkPickSet(std::vector<std::uint64_t> ids) const;
    V mkPoint(double x, double y, double z) const;

    // Yordamchilar (host uchun ham)
    static bool isNum(const V& v) { return v && (v->kind == Kind::Int || v->kind == Kind::Real); }
    static double num(const V& v, const char* fn);
    static std::string str(const V& v, const char* fn);
    bool truthy(const V& v) const { return v != nullptr; }
    std::string toString(const V& v, bool readable) const;  // prin1 (readable) / princ
    static bool equalV(const V& a, const V& b, double fuzz = 0);
    static bool eqV(const V& a, const V& b);
    bool pointOf(const V& v, double& x, double& y, double& z) const;  // (x y [z]) yoki "x,y"

    std::ostream& out() { return out_; }
    LispHost* host() { return host_; }
    void setHost(LispHost* h) { host_ = h; }
    std::vector<std::string> warnings;  // qo'llab-quvvatlanmaydigan funksiyalar (vla-*, DCL ...) — bir marta yoziladi

    // initget holati (getkword, getint ...)
    // Aniqlanmagan funksiya nomi uchun tayyorlovchi (masalan vla-get-<Xossa>); bo'sh Builtin — yo'q
    std::function<Builtin(const std::string&)> resolver;
    std::function<std::vector<V>(const V&)> collectionItems;  // vlax-for
    int initBits = 0;
    std::vector<std::string> initKeywords;
    // Oxirgi xato foydalanuvchi *error* funksiyasida ko'rsatildimi (bir marta o'qiladi)
    bool takeErrorHandled() { const bool h = errHandled_; errHandled_ = false; return h; }

private:
    friend struct Builtins;
    V evalList(const V& x);
    V callLambda(const V& lam, std::vector<V>& args);
    V progn(const V& body);
    void installBuiltins();
    void installHostBuiltins();

    std::ostream& out_;
    LispHost* host_;
    std::map<std::string, V> symbols_;   // intern jadvali
    std::map<std::string, V> globals_;   // qiymatlar (dinamik ko'rinish — vaqtincha almashtirish)
    V t_;
    int depth_ = 0;
    bool errHandled_ = false;  // *error* shu xato uchun chaqirildi
};

} // namespace asiacad::lisp
