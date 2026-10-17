#include "model/screenmedia.h"

#include <QImageReader>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrent>

#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace screenmedia {
namespace {

constexpr int OP_LIT = 0, OP_RUN = 1, OP_PREV = 2;
constexpr int MAX_OP = 0xFFFF;
constexpr int CHUNK = 64;

QByteArray rgb565(const QImage &src, int w, int h)
{
    QImage img = src.convertToFormat(QImage::Format_RGB16);
    QByteArray out(w * h * 2, '\0');
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const quint16 *>(img.constScanLine(y));
        char *dst = out.data() + y * w * 2;
        for (int x = 0; x < w; ++x) {
            dst[x * 2] = char(line[x] & 0xFF);
            dst[x * 2 + 1] = char(line[x] >> 8);
        }
    }
    return out;
}

void op(QByteArray &out, int kind, int n)
{
    if (n < 64) {
        out += char(kind << 6 | n);
    } else {
        out += char(kind << 6);
        out += char(n & 0xFF);
        out += char(n >> 8);
    }
}

QList<quint16> words(const QByteArray &raw)
{
    QList<quint16> out(raw.size() / 2);
    for (int i = 0; i < out.size(); ++i)
        out[i] = quint16(quint8(raw[2 * i]) | quint8(raw[2 * i + 1]) << 8);
    return out;
}

QByteArray encodeWords(const QList<quint16> &cur, const QList<quint16> *prev, const QByteArray &raw)
{
    QByteArray out;
    const int n = int(cur.size());
    int i = 0, lit = -1;
    auto flush = [&](int end) {
        while (lit >= 0 && lit < end) {
            int k = std::min(end - lit, MAX_OP);
            op(out, OP_LIT, k);
            out.append(raw.constData() + 2 * lit, 2 * k);
            lit += k;
        }
        lit = -1;
    };
    while (i < n) {
        int pl = 0;
        if (prev && cur[i] == (*prev)[i]) {
            int j = i + 1;
            while (j + CHUNK <= n && std::memcmp(cur.constData() + j, prev->constData() + j, CHUNK * 2) == 0)
                j += CHUNK;
            while (j < n && cur[j] == (*prev)[j])
                ++j;
            pl = j - i;
        }
        quint16 v = cur[i];
        int k = i + 1;
        while (k < n && cur[k] == v)
            ++k;
        int rl = k - i;
        if (pl >= 2 && pl >= rl) {
            flush(i);
            int step = std::min(pl, MAX_OP);
            op(out, OP_PREV, step);
            i += step;
        } else if (rl >= 3) {
            flush(i);
            int step = std::min(rl, MAX_OP);
            op(out, OP_RUN, step);
            out.append(raw.constData() + 2 * i, 2);
            i += step;
        } else {
            if (lit < 0)
                lit = i;
            ++i;
        }
    }
    flush(n);
    return out;
}

QList<Frame> loadImage(const QString &path, int w, int h, Fit mode, int maxFrames, int stillMs)
{
    QImageReader reader(path);
    QList<Frame> frames;
    while (reader.canRead() && frames.size() < maxFrames) {
        QImage img = reader.read();
        if (img.isNull())
            break;
        int delay = reader.nextImageDelay();
        frames.append({rgb565(fit(img, w, h, mode), w, h), double(delay > 0 ? delay : stillMs)});
        if (reader.imageCount() == 1)
            break;
    }
    return frames;
}

QList<Frame> loadVideo(const QString &path, int w, int h, Fit mode, int maxFrames, int fps)
{
    QString exe = ffmpegPath();
    if (exe.isEmpty())
        throw std::runtime_error("ffmpeg is needed for video; install it and make sure it is in PATH");
    QString vf;
    if (mode == FIT_STRETCH)
        vf = QStringLiteral("scale=%1:%2").arg(w).arg(h);
    else if (mode == FIT_COVER)
        vf = QStringLiteral("scale=%1:%2:force_original_aspect_ratio=increase,crop=%1:%2").arg(w).arg(h);
    else
        vf = QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease,pad=%1:%2:(ow-iw)/2:(oh-ih)/2").arg(w).arg(h);
    vf = QStringLiteral("fps=%1,").arg(fps) + vf;
    QProcess p;
    p.start(exe, {"-loglevel", "error", "-i", path, "-vf", vf, "-frames:v", QString::number(maxFrames), "-f",
                  "rawvideo", "-pix_fmt", "rgb565le", "-"});
    if (!p.waitForFinished(-1) || p.exitCode() != 0) {
        QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
        throw std::runtime_error((err.isEmpty() ? QStringLiteral("ffmpeg failed") : err).toStdString());
    }
    QByteArray data = p.readAllStandardOutput();
    const int size = w * h * 2;
    QList<Frame> out;
    for (qsizetype i = 0; i + size <= data.size(); i += size)
        out.append({data.mid(i, size), 1000.0 / fps});
    return out;
}

}

QImage fit(const QImage &img, int w, int h, Fit mode, QColor background)
{
    QImage canvas(w, h, QImage::Format_RGB32);
    canvas.fill(background);
    Qt::AspectRatioMode aspect = mode == FIT_STRETCH ? Qt::IgnoreAspectRatio
        : mode == FIT_COVER                          ? Qt::KeepAspectRatioByExpanding
                                                     : Qt::KeepAspectRatio;
    QImage scaled = img.scaled(w, h, aspect, Qt::SmoothTransformation);
    QPainter qp(&canvas);
    auto half = [](int d) { return d >= 0 ? d / 2 : -((-d + 1) / 2); }; // Python's // 2
    qp.drawImage(half(w - scaled.width()), half(h - scaled.height()), scaled);
    return canvas;
}

QImage frameImage(const Frame &f, int w, int h)
{
    QImage img(w, h, QImage::Format_RGB16);
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<quint16 *>(img.scanLine(y));
        const char *src = f.pixels.constData() + y * w * 2;
        for (int x = 0; x < w; ++x)
            line[x] = quint16(quint8(src[x * 2]) | quint8(src[x * 2 + 1]) << 8);
    }
    return img;
}

bool isImage(const QString &path)
{
    return QImageReader(path).canRead();
}

QString ffmpegPath()
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QList<Frame> load(const QStringList &paths, int w, int h, Fit mode, int maxFrames, int fps, int stillMs)
{
    QList<Frame> frames;
    for (const QString &path : paths) {
        int left = maxFrames - int(frames.size());
        if (left <= 0)
            break;
        frames += isImage(path) ? loadImage(path, w, h, mode, left, stillMs) : loadVideo(path, w, h, mode, left, fps);
    }
    return stretchDelays(frames, maxFrames);
}

QList<Frame> stretchDelays(const QList<Frame> &frames, int maxFrames)
{
    QList<Frame> out;
    for (const Frame &f : frames) {
        int n = frames.size() > 1 ? std::max(1, int(std::ceil(f.delayMs / MAX_DELAY_MS))) : 1;
        for (int k = 0; k < n; ++k)
            out.append({f.pixels, f.delayMs / n});
    }
    return out.mid(0, maxFrames);
}

QByteArray header(const QList<Frame> &frames)
{
    QByteArray h(HEADER, '\xFF');
    h[0] = char(frames.size());
    for (int i = 0; i < frames.size() && 1 + i < HEADER; ++i)
        h[1 + i] = char(std::clamp(int(std::nearbyint(frames[i].delayMs / DELAY_UNIT_MS)), 0, 255));
    return h;
}

qint64 storedSize(const QList<Frame> &frames, int w, int h)
{
    return HEADER + qint64(frames.size()) * w * h * 2;
}

QList<Frame> fromBackup(const QByteArray &blob, int w, int h, int maxFrames)
{
    const int size = w * h * 2;
    int n = blob.isEmpty() ? 0 : std::min({int((blob.size() - HEADER) / size), int(quint8(blob[0])), maxFrames});
    QList<Frame> out;
    for (int i = 0; i < n; ++i)
        out.append({blob.mid(HEADER + i * size, size), double(quint8(blob[1 + i]) * DELAY_UNIT_MS)});
    return out;
}

QByteArray encode(const QList<Frame> &frames, const std::function<void(int, int)> &progress)
{
    const QByteArray hdr = header(frames);
    QByteArray out = encodeWords(words(hdr), nullptr, hdr);
    QList<QList<quint16>> w;
    w.reserve(frames.size());
    for (const Frame &f : frames)
        w.append(words(f.pixels));
    std::atomic<int> done{0};
    QList<int> idx(frames.size());
    std::iota(idx.begin(), idx.end(), 0);
    // each frame only looks back at the one before it, so they encode independently
    const QList<QByteArray> parts = QtConcurrent::blockingMapped(idx, [&](int i) {
        QByteArray part = encodeWords(w[i], i ? &w[i - 1] : nullptr, frames[i].pixels);
        if (progress)
            progress(++done, int(frames.size()));
        return part;
    });
    for (const QByteArray &p : parts)
        out += p;
    return out;
}

}
