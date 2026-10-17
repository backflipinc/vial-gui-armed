#include "core/lzma.h"

#include <lzma.h>

namespace lzma {

std::optional<QByteArray> decompress(const QByteArray &data)
{
    lzma_stream s = LZMA_STREAM_INIT;
    if (lzma_auto_decoder(&s, UINT64_MAX, 0) != LZMA_OK)
        return std::nullopt;
    s.next_in = reinterpret_cast<const uint8_t *>(data.constData());
    s.avail_in = size_t(data.size());

    QByteArray out;
    char buf[64 * 1024];
    lzma_ret ret;
    do {
        s.next_out = reinterpret_cast<uint8_t *>(buf);
        s.avail_out = sizeof(buf);
        ret = lzma_code(&s, LZMA_FINISH);
        out.append(buf, qsizetype(sizeof(buf) - s.avail_out));
    } while (ret == LZMA_OK);
    lzma_end(&s);
    if (ret != LZMA_STREAM_END)
        return std::nullopt;
    return out;
}

QByteArray compressXz(const QByteArray &data)
{
    size_t cap = lzma_stream_buffer_bound(size_t(data.size()));
    QByteArray out(qsizetype(cap), '\0');
    size_t pos = 0;
    lzma_easy_buffer_encode(6, LZMA_CHECK_CRC64, nullptr, reinterpret_cast<const uint8_t *>(data.constData()),
                            size_t(data.size()), reinterpret_cast<uint8_t *>(out.data()), &pos, cap);
    out.truncate(qsizetype(pos));
    return out;
}

}
