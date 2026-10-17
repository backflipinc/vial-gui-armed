#include "keycodes/anykeycode.h"

#include <QList>

#include <stdexcept>

namespace keycodes {
namespace {

struct Fail : std::runtime_error {
    Fail() : std::runtime_error("bad expression") {}
};

class Parser {
public:
    Parser(const QString &s, const QHash<QString, int> &names, const std::function<std::optional<int>(const QString &)> &resolve)
        : s_(s), names_(names), resolve_(resolve)
    {
    }

    qint64 parse()
    {
        qint64 v = bitOr();
        skip();
        if (pos_ != s_.size())
            throw Fail();
        return v;
    }

private:
    void skip()
    {
        while (pos_ < s_.size() && s_[pos_].isSpace())
            ++pos_;
    }

    bool eat(const char *op)
    {
        skip();
        QLatin1StringView o(op);
        if (!QStringView(s_).mid(pos_).startsWith(o))
            return false;
        // "*" must not take the first half of "**", nor "/" of "//"
        if ((o == QLatin1String("*") || o == QLatin1String("/")) && pos_ + 1 < s_.size() && s_[pos_ + 1] == o[0])
            return false;
        pos_ += o.size();
        return true;
    }

    qint64 bitOr()
    {
        qint64 v = bitXor();
        while (eat("|"))
            v |= bitXor();
        return v;
    }

    qint64 bitXor()
    {
        qint64 v = bitAnd();
        while (eat("^"))
            v ^= bitAnd();
        return v;
    }

    qint64 bitAnd()
    {
        qint64 v = sum();
        while (eat("&"))
            v &= sum();
        return v;
    }

    qint64 sum()
    {
        qint64 v = product();
        for (;;) {
            if (eat("+"))
                v += product();
            else if (eat("-"))
                v -= product();
            else
                return v;
        }
    }

    static qint64 floorDiv(qint64 a, qint64 b)
    {
        if (b == 0)
            throw Fail();
        qint64 q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0)))
            --q;
        return q;
    }

    qint64 product()
    {
        qint64 v = unary();
        for (;;) {
            if (eat("**")) {
                // handled in power(); a stray ** here means bad input
                throw Fail();
            } else if (eat("*")) {
                v *= unary();
            } else if (eat("//")) {
                v = floorDiv(v, unary());
            } else if (eat("%")) {
                qint64 d = unary();
                if (d == 0)
                    throw Fail();
                v = v - floorDiv(v, d) * d;
            } else {
                return v;
            }
        }
    }

    qint64 unary()
    {
        if (eat("-"))
            return -unary();
        if (eat("+"))
            return unary();
        return power();
    }

    qint64 power()
    {
        qint64 base = atom();
        if (eat("**")) {
            qint64 exp = unary(); // right-associative, binds a unary minus on its right
            if (exp < 0 || exp > 62)
                throw Fail();
            qint64 r = 1;
            for (qint64 i = 0; i < exp; ++i)
                r *= base;
            return r;
        }
        return base;
    }

    qint64 number()
    {
        int start = pos_;
        while (pos_ < s_.size() && (s_[pos_].isLetterOrNumber() || s_[pos_] == QLatin1Char('_')))
            ++pos_;
        QString t = s_.mid(start, pos_ - start).remove(QLatin1Char('_'));
        bool ok = false;
        qint64 v;
        if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
            v = t.mid(2).toLongLong(&ok, 16);
        else if (t.startsWith(QLatin1String("0o"), Qt::CaseInsensitive))
            v = t.mid(2).toLongLong(&ok, 8);
        else if (t.startsWith(QLatin1String("0b"), Qt::CaseInsensitive))
            v = t.mid(2).toLongLong(&ok, 2);
        else if (t.size() > 1 && t.startsWith(QLatin1Char('0')))
            ok = false; // Python rejects 0123
        else
            v = t.toLongLong(&ok, 10);
        if (!ok)
            throw Fail();
        return v;
    }

    qint64 call(const QString &fn, const QList<qint64> &a)
    {
        auto r = [this](const char *name) -> qint64 {
            auto v = resolve_(QLatin1String(name));
            if (!v)
                throw Fail();
            return *v;
        };
        auto args = [&a](int n) {
            if (a.size() != n)
                throw Fail();
        };
        auto mt = [&](qint64 mod, qint64 kc) { return r("QK_MOD_TAP") | ((mod & 0x1F) << 8) | (kc & 0xFF); };

        struct Mods {
            const char *name;
            std::initializer_list<const char *> qk;
        };
        static const Mods mods[] = {
            {"LCTL", {"QK_LCTL"}}, {"LSFT", {"QK_LSFT"}}, {"LALT", {"QK_LALT"}}, {"LGUI", {"QK_LGUI"}},
            {"LOPT", {"QK_LALT"}}, {"LCMD", {"QK_LGUI"}}, {"LWIN", {"QK_LGUI"}},
            {"RCTL", {"QK_RCTL"}}, {"RSFT", {"QK_RSFT"}}, {"RALT", {"QK_RALT"}}, {"RGUI", {"QK_RGUI"}},
            {"ALGR", {"QK_RALT"}}, {"ROPT", {"QK_RALT"}}, {"RCMD", {"QK_RGUI"}}, {"RWIN", {"QK_RGUI"}},
            {"HYPR", {"QK_LCTL", "QK_LSFT", "QK_LALT", "QK_LGUI"}}, {"MEH", {"QK_LCTL", "QK_LSFT", "QK_LALT"}},
            {"LCAG", {"QK_LCTL", "QK_LALT", "QK_LGUI"}},
            {"SGUI", {"QK_LGUI", "QK_LSFT"}}, {"SCMD", {"QK_LGUI", "QK_LSFT"}}, {"SWIN", {"QK_LGUI", "QK_LSFT"}},
            {"LSG", {"QK_LGUI", "QK_LSFT"}},
            {"C_S", {"QK_LCTL", "QK_LSFT"}},
            {"LCA", {"QK_LCTL", "QK_LALT"}}, {"LSA", {"QK_LSFT", "QK_LALT"}}, {"LAG", {"QK_LALT", "QK_LGUI"}},
            {"RSA", {"QK_RSFT", "QK_RALT"}}, {"RCS", {"QK_RCTL", "QK_RSFT"}}, {"SAGR", {"QK_RSFT", "QK_RALT"}},
            {"C", {"QK_LCTL"}}, {"S", {"QK_LSFT"}}, {"A", {"QK_LALT"}}, {"G", {"QK_LGUI"}},
            {"LCG", {"QK_LCTL", "QK_LGUI"}}, {"RCG", {"QK_RCTL", "QK_RGUI"}},
        };
        for (const auto &m : mods) {
            if (fn == QLatin1String(m.name)) {
                args(1);
                qint64 v = a[0];
                for (const char *q : m.qk)
                    v |= r(q);
                return v;
            }
        }
        static const Mods modTaps[] = {
            {"LCTL_T", {"MOD_LCTL"}}, {"CTL_T", {"MOD_LCTL"}}, {"RCTL_T", {"MOD_RCTL"}},
            {"LSFT_T", {"MOD_LSFT"}}, {"SFT_T", {"MOD_LSFT"}}, {"RSFT_T", {"MOD_RSFT"}},
            {"LALT_T", {"MOD_LALT"}}, {"LOPT_T", {"MOD_LALT"}}, {"ALT_T", {"MOD_LALT"}}, {"OPT_T", {"MOD_LALT"}},
            {"RALT_T", {"MOD_RALT"}}, {"ROPT_T", {"MOD_RALT"}}, {"ALGR_T", {"MOD_RALT"}},
            {"LGUI_T", {"MOD_LGUI"}}, {"LCMD_T", {"MOD_LGUI"}}, {"LWIN_T", {"MOD_LGUI"}}, {"GUI_T", {"MOD_LGUI"}},
            {"CMD_T", {"MOD_LGUI"}}, {"WIN_T", {"MOD_LGUI"}},
            {"RGUI_T", {"MOD_RGUI"}}, {"RCMD_T", {"MOD_RGUI"}}, {"RWIN_T", {"MOD_RGUI"}},
            {"C_S_T", {"MOD_LCTL", "MOD_LSFT"}}, {"MEH_T", {"MOD_LCTL", "MOD_LSFT", "MOD_LALT"}},
            {"LCAG_T", {"MOD_LCTL", "MOD_LALT", "MOD_LGUI"}}, {"RCAG_T", {"MOD_RCTL", "MOD_RALT", "MOD_RGUI"}},
            {"HYPR_T", {"MOD_LCTL", "MOD_LSFT", "MOD_LALT", "MOD_LGUI"}},
            {"ALL_T", {"MOD_LCTL", "MOD_LSFT", "MOD_LALT", "MOD_LGUI"}},
            {"SGUI_T", {"MOD_LGUI", "MOD_LSFT"}}, {"SCMD_T", {"MOD_LGUI", "MOD_LSFT"}},
            {"SWIN_T", {"MOD_LGUI", "MOD_LSFT"}}, {"LSG_T", {"MOD_LGUI", "MOD_LSFT"}},
            {"LCA_T", {"MOD_LCTL", "MOD_LALT"}}, {"LSA_T", {"MOD_LSFT", "MOD_LALT"}}, {"LAG_T", {"MOD_LALT", "MOD_LGUI"}},
            {"RSA_T", {"MOD_RSFT", "MOD_RALT"}}, {"SAGR_T", {"MOD_RSFT", "MOD_RALT"}},
            {"RCS_T", {"MOD_RCTL", "MOD_RSFT"}},
            {"LCG_T", {"MOD_LCTL", "MOD_LGUI"}}, {"RCG_T", {"MOD_RCTL", "MOD_RGUI"}},
        };
        for (const auto &m : modTaps) {
            if (fn == QLatin1String(m.name)) {
                args(1);
                qint64 mod = 0;
                for (const char *q : m.qk)
                    mod |= r(q);
                return mt(mod, a[0]);
            }
        }
        if (fn == QLatin1String("LT")) {
            args(2);
            return r("QK_LAYER_TAP") | ((a[0] & 0xF) << 8) | (a[1] & 0xFF);
        }
        if (fn.size() >= 3 && fn.startsWith(QLatin1String("LT"))) {
            bool ok = false;
            int layer = fn.mid(2).toInt(&ok);
            if (ok && layer >= 0 && layer < 16 && fn.mid(2) == QString::number(layer)) {
                args(1);
                return r("QK_LAYER_TAP") | ((layer & 0xF) << 8) | (a[0] & 0xFF);
            }
        }
        if (fn == QLatin1String("TO")) {
            args(1);
            return r("QK_TO") | (r("ON_PRESS") << 0x4) | (a[0] & 0xFF);
        }
        struct Layer {
            const char *name;
            const char *qk;
        };
        static const Layer layerFns[] = {
            {"MO", "QK_MOMENTARY"}, {"DF", "QK_DEF_LAYER"}, {"TG", "QK_TOGGLE_LAYER"}, {"OSL", "QK_ONE_SHOT_LAYER"},
            {"OSM", "QK_ONE_SHOT_MOD"}, {"TT", "QK_LAYER_TAP_TOGGLE"}, {"TD", "QK_TAP_DANCE"},
        };
        for (const auto &l : layerFns) {
            if (fn == QLatin1String(l.name)) {
                args(1);
                return r(l.qk) | (a[0] & 0xFF);
            }
        }
        if (fn == QLatin1String("LM")) {
            args(2);
            return r("QK_LAYER_MOD") | ((a[0] & 0xF) << r("QMK_LM_SHIFT")) | (a[1] & r("QMK_LM_MASK"));
        }
        if (fn == QLatin1String("MT")) {
            args(2);
            return mt(a[0], a[1]);
        }
        throw Fail();
    }

    qint64 atom()
    {
        skip();
        if (pos_ >= s_.size())
            throw Fail();
        QChar c = s_[pos_];
        if (c == QLatin1Char('(')) {
            ++pos_;
            qint64 v = bitOr();
            if (!eat(")"))
                throw Fail();
            return v;
        }
        if (c.isDigit())
            return number();
        if (c.isLetter() || c == QLatin1Char('_')) {
            int start = pos_;
            while (pos_ < s_.size() && (s_[pos_].isLetterOrNumber() || s_[pos_] == QLatin1Char('_')))
                ++pos_;
            QString name = s_.mid(start, pos_ - start);
            if (eat("(")) {
                QList<qint64> a;
                if (!eat(")")) {
                    do {
                        a.append(bitOr());
                    } while (eat(","));
                    if (!eat(")"))
                        throw Fail();
                }
                return call(name, a);
            }
            auto it = names_.find(name);
            if (it == names_.end())
                throw Fail();
            return it.value();
        }
        throw Fail();
    }

    QString s_;
    int pos_ = 0;
    const QHash<QString, int> &names_;
    const std::function<std::optional<int>(const QString &)> &resolve_;
};

}

std::optional<qint64> evaluate(const QString &expr, const QHash<QString, int> &names,
                               const std::function<std::optional<int>(const QString &)> &resolve)
{
    try {
        return Parser(expr, names, resolve).parse();
    } catch (const Fail &) {
        return std::nullopt;
    }
}

}
