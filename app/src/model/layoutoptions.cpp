#include "model/layoutoptions.h"

#include <QJsonArray>

#include <bit>

namespace layoutoptions {

int Choice::bits() const
{
    if (options.isEmpty())
        return 1;
    return int(std::bit_width(unsigned(options.size() - 1)));
}

QList<Choice> parse(const QJsonValue &labels)
{
    QList<Choice> out;
    for (const auto &v : labels.toArray()) {
        if (v.isString()) {
            out.append({v.toString(), {}});
        } else {
            QJsonArray a = v.toArray();
            Choice c{a.first().toString(), {}};
            for (int i = 1; i < a.size(); ++i)
                c.options.append(a[i].toString());
            out.append(c);
        }
    }
    return out;
}

QList<int> unpack(quint32 value, const QList<Choice> &choices)
{
    // VIA stores the choices backwards: the last one in the lowest bits
    QList<int> out(choices.size(), 0);
    for (int i = int(choices.size()) - 1; i >= 0; --i) {
        const int b = choices[i].bits();
        out[i] = int(value & ((1u << b) - 1));
        value >>= b;
    }
    return out;
}

quint32 pack(const QList<int> &values, const QList<Choice> &choices)
{
    quint32 v = 0;
    for (int i = 0; i < choices.size(); ++i) {
        const int b = choices[i].bits();
        v = (v << b) | (quint32(values.value(i)) & ((1u << b) - 1));
    }
    return v;
}

}
