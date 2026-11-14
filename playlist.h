#pragma once
#include <QtWidgets>
#include <functional>

struct PlaylistTrack {
    QString title, artist, url;
    int seconds = 0;
    bool available = false;
};

inline bool isYoutubePlaylistUrl(const QString &address) {
    const QUrl url(address);
    const QString host = url.host().toLower();
    return (host == "youtube.com" || host.endsWith(".youtube.com") || host == "youtu.be")
        && !QUrlQuery(url).queryItemValue("list").isEmpty();
}
inline QString playlistMetadataUrl(const QString &address) {
    if (!isYoutubePlaylistUrl(address)) return address;
    const QUrl original(address);
    QUrl result;
    result.setScheme("https");
    result.setHost(original.host() == "music.youtube.com" ? "music.youtube.com" : "www.youtube.com");
    result.setPath("/playlist");
    QUrlQuery query; query.addQueryItem("list", QUrlQuery(original).queryItemValue("list")); result.setQuery(query);
    return result.toString();
}
inline bool isPlaylistMetadata(const QJsonObject &info) {
    return info.value("_type").toString() == "playlist" || info.contains("entries");
}
inline QList<PlaylistTrack> playlistTracks(const QJsonObject &info, const QString &source) {
    QList<PlaylistTrack> result;
    const QUrl sourceUrl(source);
    const bool youtube = sourceUrl.host() == "youtube.com" || sourceUrl.host().endsWith(".youtube.com") || sourceUrl.host() == "youtu.be";
    for (const auto &value : info.value("entries").toArray()) {
        PlaylistTrack track;
        const QJsonObject item = value.toObject();
        track.title = item.value("title").toString("Недоступный трек");
        track.artist = item.value("uploader").toString(item.value("channel").toString());
        track.seconds = qMax(0, item.value("duration").toInt());
        const QString id = item.value("id").toString();
        static const QRegularExpression videoId("^[A-Za-z0-9_-]{11}$");
        if (youtube && videoId.match(id).hasMatch()) {
            QUrl link; link.setScheme("https");
            link.setHost(sourceUrl.host() == "music.youtube.com" ? "music.youtube.com" : "www.youtube.com");
            link.setPath("/watch"); QUrlQuery query; query.addQueryItem("v", id); link.setQuery(query);
            track.url = link.toString();
        } else {
            QUrl link(item.value("webpage_url").toString(item.value("url").toString()));
            if ((link.scheme() == "https" || link.scheme() == "http") && !link.host().isEmpty()) track.url = link.toString();
        }
        const QString availability = item.value("availability").toString();
        track.available = !track.url.isEmpty() && !item.isEmpty() && availability != "private"
            && track.title != "[Deleted video]" && track.title != "[Private video]";
        result.append(track);
    }
    return result;
}

class PlaylistSelection final : public QWidget {
    QTreeWidget *list;
    QLabel *count;
    QList<PlaylistTrack> tracks;
    void updateCount() {
        count->setText(QString("Выбрано: %1 из %2").arg(selected().size()).arg(tracks.size()));
        if (changed) changed();
    }
public:
    std::function<void()> changed;
    explicit PlaylistSelection(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("playlistSelection");
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
        list = new QTreeWidget; list->setObjectName("playlistTracks"); list->setRootIsDecorated(false);
        list->setHeaderLabels({"Композиция", "Длительность"});
        list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        list->setAccessibleName("Выбор композиций плейлиста");
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        const int rowHeight = qMax(list->fontMetrics().height() + 6, list->style()->pixelMetric(QStyle::PM_SmallIconSize) + 4);
        list->setUniformRowHeights(true);
        list->setMinimumHeight(list->header()->sizeHint().height() + rowHeight * 5 + list->frameWidth() * 2);
        list->setMaximumHeight(list->minimumHeight()); layout->addWidget(list);
        auto *actions = new QHBoxLayout;
        auto *all = new QPushButton("Выбрать все"); auto *none = new QPushButton("Снять выбор"); count = new QLabel;
        actions->addWidget(all); actions->addWidget(none); actions->addStretch(); actions->addWidget(count); layout->addLayout(actions);
        connect(list, &QTreeWidget::itemChanged, this, [this] { updateCount(); });
        const auto check = [this](bool checked) {
            const QSignalBlocker blocker(list);
            for (int i = 0; i < list->topLevelItemCount(); ++i)
                if (tracks[i].available) list->topLevelItem(i)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
            updateCount();
        };
        connect(all, &QPushButton::clicked, this, [check] { check(true); });
        connect(none, &QPushButton::clicked, this, [check] { check(false); });
    }
    void setTracks(const QList<PlaylistTrack> &items) {
        const QSignalBlocker blocker(list);
        tracks = items; list->clear();
        for (int i = 0; i < tracks.size(); ++i) {
            const auto &track = tracks[i];
            const QString duration = track.available ? (track.seconds > 0
                ? QString("%1:%2").arg(track.seconds / 60).arg(track.seconds % 60, 2, 10, QChar('0')) : "—") : "Недоступно";
            auto *row = new QTreeWidgetItem(list, {QString("%1. %2").arg(i + 1).arg(track.title), duration});
            row->setToolTip(0, track.artist);
            row->setSizeHint(0, QSize(0, qMax(list->fontMetrics().height() + 6, list->style()->pixelMetric(QStyle::PM_SmallIconSize) + 4)));
            row->setFlags(track.available ? row->flags() | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
            if (track.available) row->setCheckState(0, Qt::Checked);
        }
        updateCount();
    }
    QSet<int> selected() const {
        QSet<int> result;
        for (int i = 0; i < tracks.size(); ++i)
            if (tracks[i].available && list->topLevelItem(i)->checkState(0) == Qt::Checked) result.insert(i);
        return result;
    }
};
