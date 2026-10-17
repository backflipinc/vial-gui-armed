#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

// Pictures, GIFs and video -> a qmk-armed screen animation (docs/armed.md in qmk-armed): frames fitted to the
// screen, RGB565 low byte first, a 256-byte header, and the compressed upload stream.
namespace screenmedia {

inline constexpr int HEADER = 256;
inline constexpr int DELAY_UNIT_MS = 2; // header delay unit, as the stock configurator writes it
inline constexpr int MAX_DELAY_MS = 255 * DELAY_UNIT_MS;

enum Fit { FIT_CONTAIN, FIT_COVER, FIT_STRETCH };

struct Frame {
    QByteArray pixels; // RGB565 low byte first, row by row
    double delayMs = 0;
};

QImage fit(const QImage &img, int w, int h, Fit mode, QColor background = Qt::black);
QImage frameImage(const Frame &f, int w, int h); // for previews
bool isImage(const QString &path);
QString ffmpegPath();

// throws std::runtime_error (video without ffmpeg, a broken file)
QList<Frame> load(const QStringList &paths, int w, int h, Fit mode, int maxFrames, int fps = 10, int stillMs = 1000);
// a frame can stay up to 510 ms; longer ones are repeated (cheap: the repeats compress to nothing)
QList<Frame> stretchDelays(const QList<Frame> &frames, int maxFrames);
QByteArray header(const QList<Frame> &frames);
qint64 storedSize(const QList<Frame> &frames, int w, int h);
// frames of a .bin saved by "save backup" (header + frames), cut to what fits
QList<Frame> fromBackup(const QByteArray &blob, int w, int h, int maxFrames);

// the upload stream for header + frames; frames are encoded in parallel, so progress comes from worker threads
QByteArray encode(const QList<Frame> &frames, const std::function<void(int done, int total)> &progress = {});

}
