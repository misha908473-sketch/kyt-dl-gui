#pragma once
#include <QtWidgets>
#include <QProcess>
#include <QMimeDatabase>
#include <QCryptographicHash>
#include <functional>

struct LibraryFile {
    QString path;
    QIcon icon;
    bool recent = false;
};

// Files and thumbnails for the shared download list; there is no separate view.
class DownloadLibrary final : public QObject {
    QStringList directories;
    QStringList session;
    QHash<QString, QIcon> thumbnails;
    QSet<QString> attempted;
    QQueue<QString> queue;
    QProcess decoder;
    QTimer deadline, refreshDelay;
    QFileSystemWatcher watcher;
    QString decoding, cacheFile;
    QList<LibraryFile> current;
    QString key(const QString &path) const {
        QFileInfo f(path);
        return path + QString::number(f.size()) + f.lastModified().toString(Qt::ISODateWithMs);
    }
    void append(const QString &path, bool recent, QSet<QString> &seen) {
        QFileInfo file(path);
        if (!file.isFile() || seen.contains(path)) return;
        const QString mime = QMimeDatabase().mimeTypeForFile(file, QMimeDatabase::MatchExtension).name();
        const bool audio = mime.startsWith("audio/");
        if (!audio && !mime.startsWith("video/")) return;
        if (QFile::exists(path + ".part") || QRegularExpression("\\.f[0-9]+\\.").match(file.fileName()).hasMatch()) return;
        seen.insert(path);
        const QIcon fallback = QIcon::fromTheme(audio ? "audio-x-generic" : "video-x-generic",
            QApplication::style()->standardIcon(QStyle::SP_FileIcon));
        current.append({path, thumbnails.value(key(path), fallback), recent});
        if (!audio && !attempted.contains(key(path))) { attempted.insert(key(path)); queue.enqueue(path); }
    }
    void nextThumbnail() {
        if (decoder.state() != QProcess::NotRunning || queue.isEmpty()) return;
        decoding = queue.dequeue();
        const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
        QDir().mkpath(root);
        cacheFile = root + "/" + QCryptographicHash::hash(key(decoding).toUtf8(), QCryptographicHash::Sha256).toHex() + ".jpg";
        QPixmap cached(cacheFile);
        if (!cached.isNull()) {
            applyThumbnail(QIcon(cached));
            QTimer::singleShot(0, this, [this] { nextThumbnail(); }); return;
        }
        decoder.start("ffmpeg", {"-nostdin", "-v", "error", "-y", "-i", decoding,
            "-frames:v", "1", "-vf", "scale=160:90:force_original_aspect_ratio=decrease", cacheFile});
        deadline.start(5000);
    }
    void applyThumbnail(const QIcon &icon) {
        thumbnails.insert(key(decoding), icon);
        for (auto &file : current) if (file.path == decoding) file.icon = icon;
        if (changed) changed();
    }
public:
    std::function<void()> changed;
    explicit DownloadLibrary(QObject *parent = nullptr) : QObject(parent) {
        refreshDelay.setSingleShot(true); refreshDelay.setInterval(400);
        connect(&watcher, &QFileSystemWatcher::directoryChanged, this, [this] { refreshDelay.start(); });
        connect(&refreshDelay, &QTimer::timeout, this, [this] { refreshFiles(); });
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, &decoder, &QProcess::kill);
        connect(&decoder, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus state) {
                deadline.stop(); QPixmap image(cacheFile);
                if (code == 0 && state == QProcess::NormalExit && !image.isNull()) applyThumbnail(QIcon(image));
                else QFile::remove(cacheFile);
                QTimer::singleShot(0, this, [this] { nextThumbnail(); });
            });
        connect(&decoder, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                deadline.stop(); QTimer::singleShot(0, this, [this] { nextThumbnail(); });
            }
        });
    }
    ~DownloadLibrary() override {
        changed = {}; decoder.disconnect(this);
        if (decoder.state() != QProcess::NotRunning) { decoder.kill(); decoder.waitForFinished(1000); }
    }
    QList<LibraryFile> files() const { return current; }
    void setDirectory(const QString &path) { setDirectories({path}); }
    void setDirectories(const QStringList &paths) {
        directories.clear();
        for (const auto &path : paths) if (!path.isEmpty() && !directories.contains(QDir(path).absolutePath())) directories << QDir(path).absolutePath();
        if (!watcher.directories().isEmpty()) watcher.removePaths(watcher.directories());
        for (const auto &path : directories) if (QDir(path).exists()) watcher.addPath(path);
        refreshFiles();
    }
    void relocate(const QString &oldPath, const QString &newPath) {
        session.removeAll(oldPath);
        if (!newPath.isEmpty()) { session.removeAll(newPath); session.prepend(newPath); }
        refreshFiles();
    }
    void recordDownload(const QString &path) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (!QFileInfo(absolute).isFile()) return;
        session.removeAll(absolute); session.prepend(absolute); refreshFiles();
    }
    void refreshFiles() {
        current.clear(); QSet<QString> seen;
        for (const auto &path : session) append(path, true, seen);
        for (const auto &directory : directories)
            for (const auto &file : QDir(directory).entryInfoList(QDir::Files | QDir::Readable, QDir::Time))
                append(file.absoluteFilePath(), false, seen);
        if (changed) changed();
        nextThumbnail();
    }
};
