#pragma once

#include <QByteArray>

#include <optional>

namespace lzma {

// .xz or legacy .lzma, like Python's lzma.decompress
std::optional<QByteArray> decompress(const QByteArray &data);
QByteArray compressXz(const QByteArray &data);

}
