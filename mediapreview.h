#pragma once
#include <QtWidgets>
#include <QtNetwork>
#include <functional>
#include "playlist.h"

class MediaPreview final : public QWidget {
    QLabel *cover, *title, *description;
    QProcess *fetcher = nullptr;
    QPointer<QNetworkReply> imageReply;
    QNetworkAccessManager network;
    QByteArray output;
    quint64 generation = 0;
    bool loading = false;
    QJsonObject metadata;
    QString currentUrl;
    void cancel() {
        ++generation;
        if (fetcher) { fetcher->disconnect(this); fetcher->kill(); fetcher->deleteLater(); fetcher = nullptr; }
        if (imageReply) { imageReply->abort(); imageReply->deleteLater(); imageReply = nullptr; }
        loading = false;
    }
    void loadCover(const QString &address, quint64 requestId) {
        const QUrl link(address);
        if (link.scheme() != "https" && link.scheme() != "http") return;
        QNetworkRequest request(link);
        request.setTransferTimeout(10000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto *reply = network.get(request);
        imageReply = reply;
        connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64 total) {
            if (received > 5 * 1024 * 1024 || total > 5 * 1024 * 1024) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, requestId] {
            if (requestId == generation && reply->error() == QNetworkReply::NoError) {
                QImage image;
                image.loadFromData(reply->readAll());
                if (!image.isNull()) cover->setPixmap(QPixmap::fromImage(image).scaled(
                    cover->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
            reply->deleteLater();
        });
    }
public:
    std::function<void()> changed;
    explicit MediaPreview(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("mediaPreview");
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        cover = new QLabel;
        cover->setFixedSize(96, 54); cover->setAlignment(Qt::AlignCenter);
        cover->setAccessibleName("Обложка видео");
        row->addWidget(cover);
        auto *text = new QVBoxLayout;
        title = new QLabel; title->setObjectName("previewTitle");
        title->setTextFormat(Qt::PlainText); title->setWordWrap(true);
        QFont bold = title->font(); bold.setBold(true); title->setFont(bold);
        description = new QLabel; description->setWordWrap(true); description->setTextFormat(Qt::PlainText);
        text->addWidget(title); text->addWidget(description); row->addLayout(text, 1);
        reset();
    }
    ~MediaPreview() override {
        changed = {};
        if (fetcher) { fetcher->disconnect(this); fetcher->kill(); fetcher->waitForFinished(1000); }
    }
    void reset() {
        cancel(); metadata = {}; currentUrl.clear(); output.clear();
        title->setText("Предпросмотр появится после вставки ссылки");
        description->setText("Название, длительность и доступное качество");
        cover->setPixmap(QIcon::fromTheme("video-x-generic", style()->standardIcon(QStyle::SP_FileIcon)).pixmap(48, 48));
        if (changed) changed();
    }
    bool isLoading() const { return loading; }
    QJsonObject info() const { return metadata; }
    QString url() const { return currentUrl; }
    void fetch(const QString &executable, const QString &address) {
        reset(); currentUrl = address; loading = true;
        title->setText("Получение информации…");
        description->setText("Проверяем доступное качество и размер файлов.");
        if (changed) changed();
        const quint64 requestId = generation;
        const bool playlist = isYoutubePlaylistUrl(address);
        if (playlist) description->setText("Получение списка композиций плейлиста…");
        auto *worker = new QProcess(this); fetcher = worker;
        auto *timeout = new QTimer(worker); timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, worker, &QProcess::kill);
        timeout->start(playlist ? 90000 : 45000);
        connect(worker, &QProcess::readyReadStandardOutput, this, [this, worker] {
            output += worker->readAllStandardOutput();
            if (output.size() > 16 * 1024 * 1024) worker->kill();
        });
        connect(worker, &QProcess::readyReadStandardError, worker, [worker] { worker->readAllStandardError(); });
        const auto finish = [this, worker, requestId](bool success) {
            if (generation != requestId || fetcher != worker) return;
            output += worker->readAllStandardOutput();
            loading = false; fetcher = nullptr;
            const QJsonDocument json = QJsonDocument::fromJson(output);
            if (json.isObject() && (success || (isPlaylistMetadata(json.object()) && json.object().value("entries").isArray()))) {
                metadata = json.object();
                title->setText(metadata.value("title").toString("Без названия"));
                QStringList details;
                const QString author = metadata.value("uploader").toString(metadata.value("channel").toString());
                if (!author.isEmpty()) details << author;
                const auto seconds = qint64(metadata.value("duration").toDouble());
                if (seconds > 0) details << QString("%1:%2:%3").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
                if (metadata.value("is_live").toBool()) details << "Прямой эфир";
                if (isPlaylistMetadata(metadata)) details << QString("Плейлист · %1 композиций").arg(metadata.value("entries").toArray().size());
                description->setText(details.join(" · "));
                QString thumbnail = metadata.value("thumbnail").toString();
                if (thumbnail.isEmpty()) {
                    const auto images = metadata.value("thumbnails").toArray();
                    if (!images.isEmpty()) thumbnail = images.last().toObject().value("url").toString();
                }
                loadCover(thumbnail, requestId);
            } else {
                title->setText("Не удалось получить предпросмотр");
                description->setText("Можно скачать с общими настройками. Для повторного запроса вставьте ссылку заново.");
            }
            worker->deleteLater();
            if (changed) changed();
        };
        connect(worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [finish, timeout](int code, QProcess::ExitStatus status) { timeout->stop(); finish(code == 0 && status == QProcess::NormalExit); });
        connect(worker, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish(false);
        });
        QStringList arguments{"--ignore-config", "--no-colors", "--skip-download", "--dump-single-json",
            "--socket-timeout", "15", "--retries", "1"};
        if (playlist) arguments << "--yes-playlist" << "--flat-playlist" << "--ignore-errors";
        else arguments << "--no-playlist";
        arguments << "--" << playlistMetadataUrl(address);
        worker->start(executable, arguments);
    }
};
