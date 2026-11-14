#pragma once
#include <QtCore>
#include <algorithm>

struct MediaFormat {
    QString id, extension, codec;
    int height = 0;
    double fps = 0, bitrate = 0;
    qint64 bytes = 0;
    bool approximate = false, hasAudio = false;
};

inline QList<MediaFormat> availableFormats(const QJsonObject &info, bool audioOnly) {
    QList<MediaFormat> result;
    for (const auto &value : info.value("formats").toArray()) {
        const auto f = value.toObject();
        const QString video = f.value("vcodec").toString("none");
        const QString audio = f.value("acodec").toString("none");
        const QString id = f.value("format_id").toString();
        // Only IDs that can be used literally in yt-dlp's format selector.
        static const QRegularExpression literalId("^[A-Za-z0-9_.-]+$");
        if (!literalId.match(id).hasMatch() || f.value("has_drm").toBool()) continue;
        if (audioOnly ? (video != "none" || audio == "none") : video == "none") continue;
        if (!audioOnly && f.value("height").toInt() <= 0) continue;
        MediaFormat item;
        item.id = id; item.extension = f.value("ext").toString();
        item.codec = audioOnly ? audio : video;
        item.height = f.value("height").toInt(); item.fps = f.value("fps").toDouble();
        item.bitrate = f.value(audioOnly ? "abr" : "tbr").toDouble();
        item.hasAudio = audio != "none";
        item.bytes = qint64(f.value("filesize").toDouble());
        if (item.bytes <= 0) {
            item.bytes = qint64(f.value("filesize_approx").toDouble());
            item.approximate = item.bytes > 0;
        }
        result.append(item);
    }
    std::stable_sort(result.begin(), result.end(), [audioOnly](const MediaFormat &a, const MediaFormat &b) {
        if (!audioOnly && a.height != b.height) return a.height > b.height;
        if (!audioOnly && a.fps != b.fps) return a.fps > b.fps;
        return a.bitrate > b.bitrate;
    });
    return result;
}

inline QString formatLabel(const MediaFormat &f, bool audioOnly, qint64 extraAudioBytes = 0) {
    QStringList text;
    text << (audioOnly ? (f.bitrate > 0 ? QString("%1 кбит/с").arg(qRound(f.bitrate)) : "Аудио")
                      : QString("%1p%2").arg(f.height).arg(f.fps > 30 ? QString(" · %1 кадров/с").arg(qRound(f.fps)) : ""));
    text << f.extension.toUpper() << f.codec;
    if (f.bytes > 0) {
        const bool combined = !audioOnly && !f.hasAudio;
        const qint64 size = f.bytes + (combined ? extraAudioBytes : 0);
        text << (f.approximate || combined ? "≈ " : "") + QLocale().formattedDataSize(size)
            + (combined && extraAudioBytes <= 0 ? " + аудио" : "");
    } else text << "размер неизвестен";
    return text.join(" · ");
}
