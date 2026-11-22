#include <QtWidgets>
#include "uilanguage.h"
#include "downloadlibrary.h"
#include "downloadqueue.h"
#include "mediapreview.h"
#include "mediaformats.h"
#include "usbstorage.h"
#include "settingssidebar.h"

static QString configuredDownloadDirectory(QSettings &settings, QString downloads = {}) {
    if (downloads.isEmpty()) downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloads.isEmpty()) downloads = QDir::home().filePath("Downloads");
    const QString defaultFolder = QDir(downloads).filePath("yt-dlp");
    QString saved = settings.value("folder").toString().trimmed();
    if (saved.isEmpty() || (!settings.value("downloadFolderMigrated", false).toBool()
            && QDir::cleanPath(saved) == QDir::cleanPath(downloads))) {
        saved = defaultFolder;
        settings.setValue("folder", saved);
    }
    settings.setValue("downloadFolderMigrated", true);
    if (QDir::cleanPath(saved) == QDir::cleanPath(defaultFolder)) QDir().mkpath(saved);
    return saved;
}

class Window final : public QWidget {
    QSettings settings;
    UsbMusicDestination usb{settings};
    QComboBox *queueMode;
    QSpinBox *parallelCount;
    QListWidget *settingsSections, *deviceList;
    QLineEdit *settingsSearch;
    QStackedWidget *settingsPages;
    QLabel *deviceError;
    bool deviceChoiceChanged = false;
    QLineEdit *url, *folder, *binary;
    QButtonGroup *type;
    QRadioButton *video, *audio;
    QComboBox *quality, *encoding;
    QLabel *qualityHint, *validation, *encodingLabel;
    QPushButton *start, *playlistButton;
    PlaylistSelection *playlistPanel;
    QList<PlaylistTrack> tracks;
    QSet<int> selectedTracks;
    MediaPreview *preview;
    DownloadQueue *queue;
    DownloadLibrary *library;
    QDialog *preferences;
    QTimer inspectDelay, usbPollTimer;
    QSet<QString> knownUsbConnections;
    QStringList usbSnapshot;
    QList<UsbVolume> pendingUsbPrompts;
    bool checkingUsb = false, autoUsbPrompts = true;
    bool rebuilding = false;
    int videoHeight = 0;
    QString videoSelector, videoSelectionUrl, audioSelector, audioSelectionUrl;

    bool validLink() const {
        const QUrl link(url->text().trimmed());
        return link.isValid() && !link.host().isEmpty()
            && (link.scheme() == "https" || link.scheme() == "http");
    }
    void refreshLocations() {
        library->setDirectories({folder->text().trimmed(), usb.directory()});
        start->setToolTip(uiText("Музыка: ", "Music: ") + (usb.directory().isEmpty() ? folder->text().trimmed() : usb.directory()));
    }
    void error(const QString &text) { validation->setText(text); validation->show(); }
    void updateActions() {
        const bool playlist = isYoutubePlaylistUrl(url->text().trimmed()) || isPlaylistMetadata(preview->info());
        start->setEnabled(validLink() && !preview->isLoading() && (!playlist || !selectedTracks.isEmpty()));
        start->setText(playlist && !selectedTracks.isEmpty() ? QString(uiText("&Скачать (%1)", "&Download (%1)")).arg(selectedTracks.size()) : uiText("&Скачать", "&Download"));
    }
    void updatePlaylistButton() {
        playlistButton->setText(QString(uiText("%1 треки (%2 из %3)", "%1 tracks (%2 of %3)"))
            .arg(playlistPanel->isHidden() ? uiText("Показать", "Show") : uiText("Скрыть", "Hide")).arg(selectedTracks.size()).arg(tracks.size()));
    }
    void updatePlaylist() {
        tracks = playlistTracks(preview->info(), url->text().trimmed()); selectedTracks.clear();
        playlistPanel->setTracks(tracks);
        selectedTracks = playlistPanel->selected(); playlistPanel->hide();
        playlistButton->setVisible(isPlaylistMetadata(preview->info()));
        playlistButton->setEnabled(!tracks.isEmpty());
        updatePlaylistButton();
        if (isYoutubePlaylistUrl(url->text().trimmed()) && !preview->isLoading() && tracks.isEmpty()
                && !preview->url().isEmpty())
            error(uiText("Не удалось получить доступные треки. Нужна ссылка на доступный плейлист YouTube Music или YouTube.", "Could not load available tracks. A public YouTube Music or YouTube playlist link is required."));
    }
    void rememberQuality() {
        if (rebuilding || quality->currentIndex() < 0) return;
        if (type->checkedId() == 0) {
            const auto format = quality->currentData().toMap();
            videoHeight = format.value("height").toInt();
            videoSelector = format.value("selector").toString();
            videoSelectionUrl = preview->url();
            settings.setValue("preferredHeight", videoHeight);
        } else if (encoding->currentIndex() > 0) {
            settings.setValue("audioQuality/" + encoding->currentData().toString(), quality->currentData());
        } else {
            audioSelector = quality->currentData().toString();
            audioSelectionUrl = preview->url();
        }
    }
    void updateQuality() {
        rebuilding = true;
        quality->clear(); quality->setEnabled(true);
        const bool audioOnly = type->checkedId() == 1;
        encoding->setVisible(audioOnly); encodingLabel->setVisible(audioOnly);
        const auto info = preview->info();
        const auto audioFormats = availableFormats(info, true);
        if (audioOnly && encoding->currentIndex() > 0) {
            const QString codec = encoding->currentData().toString();
            if (codec == "flac" || codec == "wav") {
                quality->addItem(codec == "flac" ? uiText("Без потерь · сжатый файл", "Lossless · compressed file") : uiText("Без потерь · несжатый файл", "Lossless · uncompressed file"), "");
                quality->setEnabled(false);
                qualityHint->setText(uiText("Конвертация в ", "Convert to ") + codec.toUpper() + uiText(". Качество ограничено исходной записью; детали, потерянные в источнике, не восстанавливаются.", ". Quality is limited by the source recording; details lost in the source cannot be restored."));
            } else {
                const QStringList rates = codec == "opus" ? QStringList{"256K", "192K", "128K", "96K", "64K"}
                    : QStringList{"320K", "256K", "192K", "128K"};
                for (const auto &rate : rates) quality->addItem(QString(rate).chopped(1) + uiText(" кбит/с", " kbps"), rate);
                const QString preferred = settings.value("audioQuality/" + codec,
                    codec == "mp3" ? settings.value("mp3Quality", "320K") : QVariant("192K")).toString();
                quality->setCurrentIndex(qMax(0, quality->findData(preferred)));
                qualityHint->setText(uiText("Автоматическая конвертация в ", "Auto-convert to ") + codec.toUpper() + uiText(". Более высокий битрейт не улучшает исходную запись.", ". A higher bitrate does not improve the source recording."));
            }
        } else if (audioOnly) {
            quality->addItem(uiText("Лучшее исходное аудио", "Best source audio"), "ba/b");
            for (const auto &f : audioFormats) quality->addItem(formatLabel(f, true), f.id);
            if (audioSelectionUrl == preview->url() && quality->findData(audioSelector) >= 0)
                quality->setCurrentIndex(quality->findData(audioSelector));
            qualityHint->setText(audioFormats.isEmpty()
                ? uiText("Аудио без перекодирования. Доступные варианты появятся после проверки ссылки.", "Audio without re-encoding. Available options will appear after checking the link.")
                : uiText("Исходные дорожки без перекодирования; указанный размер относится к выбранной дорожке.", "Original tracks without re-encoding; the size shown applies to the selected track."));
        } else {
            quality->addItem(uiText("Лучшее доступное", "Best available"), QVariantMap{{"selector", "bv*+ba/b"}, {"height", 0}});
            const auto formats = availableFormats(info, false);
            if (!formats.isEmpty()) {
                qint64 audioBytes = audioFormats.isEmpty() ? 0 : audioFormats.first().bytes;
                for (const auto &f : formats) {
                    // Preserve yt-dlp's exact stream choice; always include audio when needed.
                    const bool mergeAudio = !f.hasAudio && !audioFormats.isEmpty();
                    const QString selection = mergeAudio ? f.id + "+ba" : f.id;
                    MediaFormat labelFormat = f;
                    if (audioFormats.isEmpty()) labelFormat.hasAudio = true;
                    const QString label = formatLabel(labelFormat, false, audioBytes)
                        + (!f.hasAudio && audioFormats.isEmpty() ? uiText(" · без звука", " · no audio") : "");
                    quality->addItem(label, QVariantMap{{"selector", selection}, {"height", f.height}});
                }
                qualityHint->setText(uiText("Доступные дорожки этого видео. Размер со знаком ≈ — оценка, включая аудио.", "Available streams for this video. A size marked ≈ is an estimate, including audio."));
            } else {
                for (int height : {2160, 1440, 1080, 720, 480, 360})
                    quality->addItem(QString(uiText("До %1p", "Up to %1p")).arg(height), QVariantMap{
                        {"selector", QString("bv*[height<=%1]+ba/b[height<=%1]").arg(height)}, {"height", height}});
                qualityHint->setText(uiText("Общие настройки: будет выбрано разрешение не выше указанного. Доступные варианты определяются автоматически.", "General settings: the selected resolution will not exceed this limit. Available options are detected automatically."));
            }
            for (int i = 0; i < quality->count(); ++i)
                if (quality->itemData(i).toMap().value("height").toInt() == videoHeight) { quality->setCurrentIndex(i); break; }
            if (videoSelectionUrl == preview->url() && !videoSelector.isEmpty())
                for (int i = 0; i < quality->count(); ++i)
                    if (quality->itemData(i).toMap().value("selector").toString() == videoSelector) {
                        quality->setCurrentIndex(i); break;
                    }
        }
        rebuilding = false;
        quality->setToolTip(qualityHint->text());
        updateActions();
    }
    void inspectLink() {
        inspectDelay.stop();
        if (!validLink()) return;
        preview->fetch(binary->text().trimmed(), url->text().trimmed());
    }
    void enqueue() {
        if (!validLink()) { error(uiText("Введите полную HTTP или HTTPS ссылку.", "Enter a complete HTTP or HTTPS link.")); return; }
        const QString executable = binary->text().trimmed();
        if (QStandardPaths::findExecutable(executable).isEmpty()) {
            error(uiText("Не найден yt-dlp. Установите его или укажите путь в меню настроек.", "yt-dlp not found. Install it or set its path in the settings menu.")); return;
        }
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) {
            error(uiText("Не найден FFmpeg. Установите его для обработки видео и аудио.", "FFmpeg not found. Install it to process video and audio.")); return;
        }
        const bool audioOnly = type->checkedId() == 1;
        const QString external = usb.directory(audioOnly);
        const QString destination = external.isEmpty() ? folder->text().trimmed() : external;
        if (destination.isEmpty() || !QDir().mkpath(destination)) {
            error(uiText("Не удалось создать папку. Выберите другую папку в настройках.", "Could not create the folder. Choose another folder in settings.")); return;
        }
        QStringList arguments{"--ignore-config", "--no-playlist", "--newline", "--no-colors", "--progress",
            "--progress-template", "download:GUI_PROGRESS:%(progress._percent_str)s|%(progress._speed_str)s|%(progress._eta_str)s",
            "--print", "after_move:GUI_FILE:%(filepath)j", "--no-simulate",
            "-P", destination};
        QString variant = "%(height)sp-%(format_id).32B";
        if (audioOnly) {
            variant = "audio-%(format_id).32B";
            if (encoding->currentIndex() > 0) {
                variant = encoding->currentData().toString();
                const QString rate = quality->currentData().toString();
                if (!rate.isEmpty()) variant += "-" + rate;
            }
        }
        arguments << "-o" << ("%(title).150B [%(id)s] [" + variant + "].%(ext)s");
        if (audioOnly) {
            if (encoding->currentIndex() == 0)
                arguments << "-f" << quality->currentData().toString() << "-x" << "--audio-format" << "best";
            else {
                const QString codec = encoding->currentData().toString();
                arguments << "-f" << "ba/b" << "-x" << "--audio-format" << codec;
                if (codec != "flac" && codec != "wav") arguments << "--audio-quality" << quality->currentData().toString();
            }
        } else arguments << "-f" << quality->currentData().toMap().value("selector").toString()
                         << "--merge-output-format" << "mkv";
        if (audioOnly) arguments << "--embed-metadata";
        const bool playlist = isYoutubePlaylistUrl(url->text().trimmed()) || isPlaylistMetadata(preview->info());
        if (playlist && selectedTracks.isEmpty()) { error(uiText("Дождитесь списка треков и выберите композиции.", "Wait for the track list, then select tracks.")); return; }
        const auto addJob = [&](const QString &address, const QString &title) {
            DownloadJob job;
            job.executable = executable; job.arguments = arguments;
            job.directory = destination; job.url = address; job.title = title; job.audio = audioOnly;
            if (!external.isEmpty()) { job.requiredMount = usb.volume().root; job.requiredDevice = usb.volume().device; job.requiredVolumeId = usb.volume().id; }
            job.choice = audioOnly ? (encoding->currentIndex() == 0 ? uiText("Исходное аудио", "Source audio") : encoding->currentData().toString().toUpper() + " · " + quality->currentText()) : quality->currentText();
            const QString tempKey = destination + "\n" + address + "\n" + arguments.join(QChar(0));
            const QString staging = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/partial/"
                + QCryptographicHash::hash(tempKey.toUtf8(), QCryptographicHash::Sha256).toHex();
            job.arguments << "-P" << "temp:" + staging << "--" << address;
            return queue->enqueue(job);
        };
        int added = 0, skipped = 0;
        if (playlist) {
            for (int i = 0; i < tracks.size(); ++i) if (selectedTracks.contains(i) && tracks[i].available) {
                if (addJob(tracks[i].url, tracks[i].title)) ++added; else ++skipped;
            }
        } else {
            const QString address = url->text().trimmed();
            if (addJob(address, preview->info().value("title").toString(QUrl(address).host()))) ++added; else ++skipped;
        }
        if (!added) { error(uiText("Эти файлы уже скачиваются или ожидают загрузки.", "These files are already downloading or queued.")); return; }
        validation->hide();
        if (playlist) error(QString(uiText("Добавлено композиций: %1.%2", "Added tracks: %1.%2")).arg(added)
            .arg(skipped ? QString(uiText(" Уже загружаются: %1.", " Already downloading: %1.")).arg(skipped) : ""));
        settings.setValue("mediaType", type->checkedId());
        settings.setValue("audioEncoding", encoding->currentIndex());
        rememberQuality();
        refreshLocations();
    }
    void populateDevices(bool preserveSelection = false) {
        QVariantMap desired;
        if (preserveSelection && deviceList->currentItem()) desired = deviceList->currentItem()->data(Qt::UserRole).toMap();
        else if (usb.available()) {
            const auto selected = usb.volume(); desired = {{"id", selected.id}, {"root", selected.root}, {"device", selected.device}};
        }
        const QSignalBlocker blocker(deviceList); deviceList->clear();
        auto *computer = new QListWidgetItem(QIcon::fromTheme("computer"), uiText("Компьютер", "Computer"), deviceList);
        computer->setToolTip(folder->text()); deviceList->setCurrentItem(computer);
        for (const auto &volume : usb.volumes()) {
            auto *item = new QListWidgetItem(QIcon::fromTheme("drive-removable-media-usb", QIcon::fromTheme("drive-removable-media")),
                usbVolumeName(volume.name, {}, 0), deviceList);
            item->setData(Qt::UserRole, QVariantMap{{"id", volume.id}, {"root", volume.root}, {"device", volume.device}, {"name", volume.name}});
            item->setToolTip(QDir(volume.root).filePath("yt-dlp"));
            if (!desired.isEmpty() && desired.value("root").toString() == volume.root
                && desired.value("device").toString() == volume.device && desired.value("id").toString() == volume.id) deviceList->setCurrentItem(item);
        }
        deviceError->hide();
    }
    void selectSettingsPage(int page) {
        settingsSearch->clear();
        for (int i = 0; i < settingsSections->count(); ++i)
            if (settingsSections->item(i)->data(SettingsNavigation::PageRole).toInt() == page) { settingsSections->setCurrentRow(i); return; }
    }
    void setupPreferences() {
        preferences = new QDialog(this); preferences->setObjectName("preferences"); preferences->setWindowTitle(uiText("Настроить приложение", "Configure application"));
        auto *layout = new QVBoxLayout(preferences);
        auto *body = new QHBoxLayout; layout->addLayout(body, 1);
        auto *sidebar = new QWidget; sidebar->setObjectName("settingsSidebar");
        sidebar->setFixedWidth(qMax(230, fontMetrics().horizontalAdvance(uiText("Дополнительно", "Advanced")) + 80));
        sidebar->setAutoFillBackground(true); sidebar->setBackgroundRole(QPalette::Base);
        auto *sidebarLayout = new QVBoxLayout(sidebar); sidebarLayout->setContentsMargins(2, 4, 2, 4); sidebarLayout->setSpacing(4);
        settingsSearch = new QLineEdit; settingsSearch->setObjectName("settingsSearch"); settingsSearch->setPlaceholderText(uiText("Поиск…", "Search…"));
        settingsSearch->setAccessibleName(uiText("Поиск в настройках", "Search settings")); settingsSearch->setClearButtonEnabled(true);
        settingsSearch->addAction(QIcon::fromTheme("edit-find"), QLineEdit::LeadingPosition); sidebarLayout->addWidget(settingsSearch);
        settingsSections = new QListWidget; settingsSections->setObjectName("settingsSections"); settingsSections->setFrameShape(QFrame::NoFrame);
        settingsSections->setItemDelegate(new SettingsNavigation::Delegate(settingsSections));
        settingsSections->setMouseTracking(true); settingsSections->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        settingsSections->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        SettingsNavigation::addGroup(settingsSections, uiText("Файлы и устройства", "Files and devices"));
        SettingsNavigation::addPage(settingsSections, 0, uiText("Загрузки", "Downloads"), "folder-download", uiText("папка расположение очередь параллельно", "folder location queue parallel"), QStyle::SP_ArrowDown);
        SettingsNavigation::addPage(settingsSections, 1, uiText("Устройства", "Devices"), "drive-removable-media-usb", uiText("USB флешка компьютер носитель", "USB drive computer storage"), QStyle::SP_DriveHDIcon);
        SettingsNavigation::addGroup(settingsSections, uiText("Приложение", "Application"));
        SettingsNavigation::addPage(settingsSections, 2, uiText("Дополнительно", "Advanced"), "configure", uiText("yt-dlp путь программа", "yt-dlp path program"), QStyle::SP_FileDialogDetailedView);
        settingsSections->setAccessibleName(uiText("Разделы настроек", "Settings sections")); sidebarLayout->addWidget(settingsSections, 1);
        auto *noResults = new QLabel(uiText("Ничего не найдено", "No results found")); noResults->hide(); sidebarLayout->addWidget(noResults); body->addWidget(sidebar);
        settingsPages = new QStackedWidget; settingsPages->setObjectName("settingsPages"); body->addWidget(settingsPages, 1);
        connect(settingsSections, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
            if (item && !item->data(SettingsNavigation::GroupRole).toBool()) settingsPages->setCurrentIndex(item->data(SettingsNavigation::PageRole).toInt());
        });
        connect(settingsSearch, &QLineEdit::textChanged, this, [this, noResults](const QString &query) {
            QListWidgetItem *group = nullptr, *first = nullptr; bool groupVisible = false;
            const QString search = query.trimmed();
            for (int i = 0; i < settingsSections->count(); ++i) {
                auto *item = settingsSections->item(i);
                if (item->data(SettingsNavigation::GroupRole).toBool()) {
                    if (group) group->setHidden(!groupVisible);
                    group = item; groupVisible = false;
                } else {
                    const bool visible = item->data(SettingsNavigation::SearchRole).toString().contains(search, Qt::CaseInsensitive);
                    item->setHidden(!visible); groupVisible |= visible;
                    if (visible && !first) first = item;
                }
            }
            if (group) group->setHidden(!groupVisible);
            noResults->setVisible(!first);
            if (first && (!settingsSections->currentItem() || settingsSections->currentItem()->isHidden())) settingsSections->setCurrentItem(first);
        });
        auto *downloads = new QWidget; auto *form = new QFormLayout(downloads);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        folder = new QLineEdit(configuredDownloadDirectory(settings)); folder->setObjectName("downloadFolder"); folder->setAccessibleName(uiText("Папка сохранения", "Save folder"));
        auto *folderRow = new QHBoxLayout; auto *browse = new QPushButton(QIcon::fromTheme("folder-open"), uiText("Выбрать…", "Choose…"));
        folderRow->addWidget(folder); folderRow->addWidget(browse); form->addRow(uiText("Расположение:", "Location:"), folderRow);
        queueMode = new QComboBox; queueMode->setObjectName("queueMode"); queueMode->addItems({uiText("По очереди", "Sequentially"), uiText("Параллельно", "In parallel")});
        queueMode->setCurrentIndex(settings.value("parallelDownloads", false).toBool() ? 1 : 0); form->addRow(uiText("Загрузка файлов:", "File downloads:"), queueMode);
        parallelCount = new QSpinBox; parallelCount->setObjectName("parallelCount"); parallelCount->setRange(2, 8);
        parallelCount->setValue(qBound(2, settings.value("parallelCount", 3).toInt(), 8)); parallelCount->setEnabled(queueMode->currentIndex() == 1);
        form->addRow(uiText("Одновременно:", "At the same time:"), parallelCount);
        connect(queueMode, qOverload<int>(&QComboBox::currentIndexChanged), parallelCount, [this](int index) { parallelCount->setEnabled(index == 1); });
        settingsPages->addWidget(downloads);
        auto *devices = new QWidget; auto *devicesLayout = new QVBoxLayout(devices);
        devicesLayout->addWidget(new QLabel(uiText("Сохранять новые файлы на:", "Save new files to:")));
        deviceList = new QListWidget; deviceList->setObjectName("deviceList"); deviceList->setWordWrap(true); deviceList->setIconSize(QSize(32, 32));
        deviceList->setAccessibleName(uiText("Устройство для сохранения файлов", "Device for saving files")); devicesLayout->addWidget(deviceList, 1);
        auto *deviceHint = new QLabel(uiText("Компьютер использует папку из раздела «Загрузки». На USB файлы сохраняются в папку yt-dlp.", "The computer uses the folder from the “Downloads” section. Files on USB are saved to the yt-dlp folder."));
        deviceHint->setWordWrap(true); devicesLayout->addWidget(deviceHint);
        deviceError = new QLabel; deviceError->setObjectName("deviceError"); deviceError->setWordWrap(true); deviceError->hide(); devicesLayout->addWidget(deviceError);
        auto *refresh = new QPushButton(QIcon::fromTheme("view-refresh"), uiText("Обновить устройства", "Refresh devices")); devicesLayout->addWidget(refresh, 0, Qt::AlignTrailing);
        connect(refresh, &QPushButton::clicked, this, [this] { populateDevices(true); });
        connect(deviceList, &QListWidget::currentRowChanged, this, [this] { deviceChoiceChanged = true; });
        connect(deviceList, &QListWidget::itemClicked, this, [this] { deviceChoiceChanged = true; });
        settingsPages->addWidget(devices);
        auto *advanced = new QWidget; auto *advancedForm = new QFormLayout(advanced);
        binary = new QLineEdit(settings.value("binary", "yt-dlp").toString()); binary->setAccessibleName(uiText("Путь к yt-dlp", "yt-dlp path"));
        advancedForm->addRow(uiText("Путь к yt-dlp:", "yt-dlp path:"), binary); settingsPages->addWidget(advanced);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
        connect(browse, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getExistingDirectory(preferences, uiText("Выбрать папку", "Choose folder"), folder->text()); if (!path.isEmpty()) folder->setText(path);
        });
        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            const QString destination = folder->text().trimmed();
            if (destination.isEmpty() || !QDir().mkpath(destination)) { selectSettingsPage(0); folder->setFocus(); return; }
            if (deviceChoiceChanged && deviceList->currentItem()) {
                const auto wanted = deviceList->currentItem()->data(Qt::UserRole).toMap();
                const auto mounted = usb.volumes();
                if (wanted.isEmpty()) {
                    usb.clear(); for (const auto &volume : mounted) usb.remember(volume, false);
                } else {
                    bool found = false;
                    for (const auto &volume : mounted)
                        if (volume.root == wanted.value("root").toString() && volume.device == wanted.value("device").toString() && volume.id == wanted.value("id").toString()) {
                            usb.select(volume, true); found = true; break;
                        }
                    if (!found) { selectSettingsPage(1); deviceError->setText(uiText("Носитель отключён. Выберите компьютер или обновите список устройств.", "Storage disconnected. Select Computer or refresh the device list.")); deviceError->show(); return; }
                    for (const auto &volume : mounted) usb.remember(volume, volume.root == usb.volume().root && volume.device == usb.volume().device, true);
                }
            }
            settings.setValue("folder", destination); settings.setValue("binary", binary->text().trimmed());
            settings.setValue("parallelDownloads", queueMode->currentIndex() == 1); settings.setValue("parallelCount", parallelCount->value());
            queue->setConcurrency(queueMode->currentIndex() == 1 ? parallelCount->value() : 1);
            refreshLocations(); preferences->accept(); if (validLink()) inspectLink();
        });
        connect(buttons, &QDialogButtonBox::rejected, preferences, &QDialog::reject);
        connect(preferences, &QDialog::rejected, this, [this] {
            folder->setText(configuredDownloadDirectory(settings)); binary->setText(settings.value("binary", "yt-dlp").toString());
            queueMode->setCurrentIndex(settings.value("parallelDownloads", false).toBool() ? 1 : 0);
            parallelCount->setValue(qBound(2, settings.value("parallelCount", 3).toInt(), 8)); populateDevices(); deviceChoiceChanged = false;
        });
        populateDevices(); deviceChoiceChanged = false; selectSettingsPage(0);
        preferences->resize(760, 400);
        queue->setConcurrency(settings.value("parallelDownloads", false).toBool() ? parallelCount->value() : 1);
    }

protected:
    void closeEvent(QCloseEvent *event) override {
        if (queue->fileOperationInProgress()) { error(uiText("Дождитесь завершения операции с файлами перед закрытием.", "Wait for the file operation to finish before closing.")); event->ignore(); return; }
        if (queue->hasPending()) {
            QMessageBox question(QMessageBox::Question, uiText("Закрыть загрузчик", "Close downloader"), uiText("Остановить загрузку и закрыть приложение? Незавершённые загрузки за этот сеанс не сохранятся.", "Stop the download and close the app? Unfinished downloads from this session will not be saved."), QMessageBox::NoButton, this);
            auto *close = question.addButton(uiText("Остановить и закрыть", "Stop and close"), QMessageBox::AcceptRole);
            auto *back = question.addButton(uiText("Продолжить загрузки", "Continue downloads"), QMessageBox::RejectRole);
            question.setDefaultButton(back); question.exec();
            if (question.clickedButton() != close) { event->ignore(); return; }
        }
        usbPollTimer.stop(); inspectDelay.stop(); preview->reset(); queue->shutdown(); event->accept();
    }
public:
    void checkUsbDevices() {
        if (checkingUsb) return;
        const auto mounted = usb.volumes();
        QSet<QString> connections; QStringList snapshot;
        for (const auto &volume : mounted) {
            const QString key = usbConnectionKey(volume);
            connections.insert(key); snapshot << key + volume.name;
            if (autoUsbPrompts && !knownUsbConnections.contains(key)) pendingUsbPrompts << volume;
        }
        snapshot.sort();
        const bool changed = snapshot != usbSnapshot;
        knownUsbConnections = connections; usbSnapshot = snapshot;
        for (int i = pendingUsbPrompts.size() - 1; i >= 0; --i)
            if (!connections.contains(usbConnectionKey(pendingUsbPrompts[i]))) pendingUsbPrompts.removeAt(i);
        if (changed) {
            refreshLocations();
            if (preferences->isVisible()) populateDevices(true);
        }
        // Wait until another dialog is closed; a plugged-in device must not steal its focus.
        if (pendingUsbPrompts.isEmpty() || QApplication::activeModalWidget() || !isVisible()) return;
        const auto candidates = pendingUsbPrompts; pendingUsbPrompts.clear(); checkingUsb = true;
        usb.offer(this, candidates); checkingUsb = false; refreshLocations();
        if (preferences->isVisible()) populateDevices(true);
    }
    explicit Window(bool detectUsb = true, std::function<QList<UsbVolume>()> volumeProvider = {}) {
        autoUsbPrompts = detectUsb;
        if (volumeProvider) usb.volumes = std::move(volumeProvider);
        setWindowTitle(uiText("Загрузчик — yt-dlp", "Downloader — yt-dlp")); setWindowIcon(QIcon::fromTheme("download"));
        auto *layout = new QVBoxLayout(this);
        const int margin = style()->pixelMetric(QStyle::PM_LayoutLeftMargin, nullptr, this);
        layout->setContentsMargins(margin, margin, margin, margin);
        auto *heading = new QHBoxLayout;
        auto *title = new QLabel(uiText("Скачать по ссылке", "Download from a link")); QFont bold = font(); bold.setBold(true); title->setFont(bold);
        heading->addWidget(title); heading->addStretch();
        auto *menuButton = new QToolButton; menuButton->setText(uiText("Меню", "Menu")); menuButton->setIcon(QIcon::fromTheme("application-menu"));
        menuButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); menuButton->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(menuButton);
        auto *settingsAction = menu->addAction(QIcon::fromTheme("configure"), uiText("Настроить приложение…", "Configure application…"));
        settingsAction->setShortcut(QKeySequence("Ctrl+,")); addAction(settingsAction);
        menuButton->setMenu(menu); heading->addWidget(menuButton); layout->addLayout(heading);
        auto *form = new QFormLayout; form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        url = new QLineEdit; url->setObjectName("url"); url->setClearButtonEnabled(true);
        url->setPlaceholderText("https://www.youtube.com/watch?v=example"); url->setAccessibleName(uiText("Ссылка на видео или аудио", "Video or audio link"));
        form->addRow(uiText("Ссылка:", "Link:"), url);
        validation = new QLabel; validation->setWordWrap(true); validation->setTextFormat(Qt::PlainText); validation->hide();
        form->addRow("", validation); layout->addLayout(form);
        preview = new MediaPreview; preview->hide(); layout->addWidget(preview);
        playlistButton = new QPushButton(uiText("Выбрать треки…", "Select tracks…")); playlistButton->setObjectName("choosePlaylist");
        playlistButton->hide(); layout->addWidget(playlistButton, 0, Qt::AlignLeading);
        playlistPanel = new PlaylistSelection; playlistPanel->hide(); layout->addWidget(playlistPanel);
        playlistPanel->changed = [this] {
            selectedTracks = playlistPanel->selected(); updatePlaylistButton(); updateActions();
        };
        connect(playlistButton, &QPushButton::clicked, this, [this] {
            const bool show = playlistPanel->isHidden(); playlistPanel->setVisible(show); updatePlaylistButton();
            if (!show) { this->layout()->activate(); resize(width(), qMax(480, minimumSizeHint().height())); }
        });
        auto *options = new QFormLayout; options->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        options->setRowWrapPolicy(QFormLayout::WrapLongRows);
        type = new QButtonGroup(this);
        video = new QRadioButton(uiText("&Видео со звуком", "&Video with audio")); audio = new QRadioButton(uiText("Только &аудио", "&Audio only"));
        type->addButton(video, 0); type->addButton(audio, 1);
        type->button(qBound(0, settings.value("mediaType", 0).toInt(), 1))->setChecked(true);
        auto *typeRow = new QHBoxLayout; typeRow->addWidget(video); typeRow->addWidget(audio); typeRow->addStretch();
        start = new QPushButton(QIcon::fromTheme("download"), uiText("&Скачать", "&Download")); start->setObjectName("enqueue"); start->setDefault(true);
        typeRow->addWidget(start);
        options->addRow(uiText("Скачать:", "Download:"), typeRow);
        encoding = new QComboBox; encoding->setObjectName("audioEncoding"); encoding->addItem(uiText("Исходное аудио", "Source audio"), "best");
        for (const auto &codec : {"mp3", "m4a", "aac", "opus", "flac", "wav"}) encoding->addItem(QString(codec).toUpper(), codec);
        encoding->setCurrentIndex(qBound(0, settings.value("audioEncoding", 0).toInt(), encoding->count() - 1));
        encodingLabel = new QLabel(uiText("Аудиоформат:", "Audio format:")); encodingLabel->setBuddy(encoding); options->addRow(encodingLabel, encoding);
        quality = new QComboBox; quality->setObjectName("quality"); quality->setAccessibleName(uiText("Качество загрузки", "Download quality")); quality->setMaxVisibleItems(10);
        quality->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); quality->setMinimumContentsLength(22);
        options->addRow(uiText("Качество:", "Quality:"), quality);
        qualityHint = new QLabel(this); qualityHint->hide();
        layout->addLayout(options);
        queue = new DownloadQueue; layout->addWidget(queue, 1);
        library = new DownloadLibrary(this);
        queue->addActionsToMenu(menu);
        library->changed = [this] { queue->setLibraryFiles(library->files()); };
        setupPreferences(); refreshLocations();
        queue->fileReady = [this](const QString &path) { library->recordDownload(path); };
        queue->fileRelocated = [this](const QString &oldPath, const QString &newPath) { library->relocate(oldPath, newPath); };
        queue->changed = [this] { library->refreshFiles(); };
        preview->changed = [this] { updatePlaylist(); updateQuality(); };
        videoHeight = settings.value("preferredHeight", 0).toInt();
        updateQuality();
        connect(quality, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { rememberQuality(); });
        connect(video, &QRadioButton::toggled, this, [this] { updateQuality(); });
        connect(encoding, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateQuality(); });
        inspectDelay.setSingleShot(true); inspectDelay.setInterval(800);
        connect(&inspectDelay, &QTimer::timeout, this, [this] { inspectLink(); });
        connect(url, &QLineEdit::textChanged, this, [this] {
            inspectDelay.stop(); preview->reset();
            preview->setVisible(validLink());
            if (validLink() && QUrl(url->text().trimmed()).host().toLower() == "music.youtube.com") audio->setChecked(true);
            if (validLink()) { validation->hide(); inspectDelay.start(); }
            else if (!url->text().trimmed().isEmpty()) error(uiText("Введите полную HTTP или HTTPS ссылку.", "Enter a complete HTTP or HTTPS link."));
            else validation->hide();
            updateActions();
        });
        connect(start, &QPushButton::clicked, this, [this] { enqueue(); });
        connect(settingsAction, &QAction::triggered, this, [this] {
            settingsSearch->clear(); populateDevices(); deviceChoiceChanged = false;
            preferences->setModal(true); preferences->show(); preferences->raise(); preferences->activateWindow();
        });
        auto *focus = new QShortcut(QKeySequence("Ctrl+L"), this);
        connect(focus, &QShortcut::activated, this, [this] { url->setFocus(); url->selectAll(); });
        url->setFocus(); resize(fontMetrics().averageCharWidth() * 82, 480);
        if (detectUsb) {
            usbPollTimer.setInterval(1500);
            connect(&usbPollTimer, &QTimer::timeout, this, [this] { checkUsbDevices(); });
            QTimer::singleShot(0, this, [this] { checkUsbDevices(); usbPollTimer.start(); });
        }
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("LocalTools"); QCoreApplication::setApplicationName("YtDlpGui");
    Window window; window.show(); return app.exec();
}
