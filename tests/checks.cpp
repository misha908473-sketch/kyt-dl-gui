#define main guiMain
#include "../main.cpp"
#undef main
#include <iostream>
#include <functional>

static void require(bool value, const char *message) {
    if (!value) { std::cerr << "FAIL: " << message << std::endl; std::exit(1); }
}
static bool waitFor(const std::function<bool()> &condition, int timeout = 3000) {
    QElapsedTimer elapsed; elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(5);
    }
    return condition();
}
static QPushButton *button(QWidget &widget, const QString &text) {
    for (auto *b : widget.findChildren<QPushButton *>()) if (b->text() == text) return b;
    require(false, "button exists"); return nullptr;
}
static QAction *action(QWidget &widget, const QString &text) {
    for (auto *a : widget.findChildren<QAction *>()) if (a->text() == text) return a;
    require(false, "action exists"); return nullptr;
}
int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    require(argc > 1, "fake engine path provided");
    const QString engine = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory");
    QCoreApplication::setOrganizationName("YtdlpGuiTests");
    QCoreApplication::setApplicationName("Checks");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings settings; settings.setValue("binary", engine); settings.setValue("folder", temporary.path());

    QSettings pathSettings(temporary.filePath("paths.ini"), QSettings::IniFormat);
    const QString downloads = temporary.filePath("Downloads");
    const QString dedicated = QDir(downloads).filePath("yt-dlp");
    require(configuredDownloadDirectory(pathSettings, downloads) == dedicated && QDir(dedicated).exists(), "dedicated default folder created");
    pathSettings.clear(); pathSettings.setValue("folder", downloads);
    require(configuredDownloadDirectory(pathSettings, downloads) == dedicated, "legacy downloads root migrated");
    pathSettings.setValue("folder", downloads);
    require(configuredDownloadDirectory(pathSettings, downloads) == downloads, "explicit root retained after migration");
    pathSettings.clear(); pathSettings.setValue("folder", temporary.path());
    require(configuredDownloadDirectory(pathSettings, downloads) == temporary.path(), "custom folder preserved");

    MediaPreview preview;
    preview.fetch(engine, "https://example.org/slow");
    preview.fetch(engine, "https://example.org/second");
    require(waitFor([&] { return !preview.isLoading(); }), "metadata completes");
    require(preview.info().value("title").toString() == "Второе видео", "stale preview ignored");
    auto videos = availableFormats(preview.info(), false);
    auto audios = availableFormats(preview.info(), true);
    require(videos.size() == 2 && videos.first().height == 1080, "video formats sorted");
    require(audios.size() == 1 && audios.first().extension == "m4a", "audio-only formats");
    require(formatLabel(videos.first(), false, audios.first().bytes).contains("≈"), "combined size marked approximate");
    preview.fetch(engine, "https://example.org/malformed");
    require(waitFor([&] { return !preview.isLoading(); }) && preview.info().isEmpty(), "malformed metadata fallback");
    preview.fetch("/nonexistent/yt-dlp", "https://example.org/test");
    require(waitFor([&] { return !preview.isLoading(); }), "failed metadata executable handled");

    const QString musicPlaylist = "https://music.youtube.com/watch?v=aaaaaaaaaaa&list=PLtest&index=1";
    require(isYoutubePlaylistUrl(musicPlaylist), "music playlist URL detected");
    require(!isYoutubePlaylistUrl("https://notyoutube.com/playlist?list=PLtest"), "unrelated host not treated as YouTube playlist");
    require(!isYoutubePlaylistUrl("https://music.youtube.com/watch?v=aaaaaaaaaaa"), "single music track stays single");
    require(playlistMetadataUrl(musicPlaylist) == "https://music.youtube.com/playlist?list=PLtest", "watch link normalized to full playlist");
    MediaPreview playlistPreview;
    playlistPreview.fetch(engine, musicPlaylist);
    require(waitFor([&] { return !playlistPreview.isLoading(); }), "playlist metadata completes");
    require(isPlaylistMetadata(playlistPreview.info()), "playlist metadata accepted");
    require(playlistPreview.info().value("_test_url").toString() == playlistMetadataUrl(musicPlaylist), "playlist fetched using flat extraction");
    auto tracks = playlistTracks(playlistPreview.info(), musicPlaylist);
    require(tracks.size() == 4 && tracks[0].available && !tracks[1].available && tracks[2].available && !tracks[3].available, "unavailable playlist tracks excluded");
    require(tracks[0].url == "https://music.youtube.com/watch?v=aaaaaaaaaaa", "track URL does not recursively include playlist");
    playlistPreview.fetch(engine, "https://music.youtube.com/playlist?list=PLpartial");
    require(waitFor([&] { return !playlistPreview.isLoading(); }) && isPlaylistMetadata(playlistPreview.info()), "usable playlist survives partial extractor errors");
    PlaylistSelection selection;
    selection.setTracks(tracks);
    auto *trackList = selection.findChild<QTreeWidget *>("playlistTracks");
    trackList->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    require(selection.selected() == QSet<int>{2}, "playlist selection retained");
    button(selection, "Снять выбор")->click(); require(selection.selected().isEmpty(), "playlist clear selection");
    button(selection, "Выбрать все")->click(); require(selection.selected() == QSet<int>({0, 2}), "playlist select all excludes unavailable");

    DownloadQueue queue;
    auto *list = queue.findChild<QTreeWidget *>("queueList");
    const QString record = temporary.filePath("order.txt");
    const QString media = temporary.filePath("music.mp3");
    QFile file(media); require(file.open(QIODevice::WriteOnly), "create media fixture"); file.write("fixture"); file.close();
    QStringList completed;
    queue.fileReady = [&](const QString &path) { completed.append(path); };
    auto job = [&](const QString &url) {
        DownloadJob j; j.executable = engine; j.url = url; j.title = url;
        j.arguments = {"--record", record, "--result", media, "--", url}; return j;
    };
    require(queue.enqueue(job("https://example.org/first")), "first enqueue");
    require(!queue.enqueue(job("https://example.org/first")), "duplicate rejected");
    require(queue.enqueue(job("https://example.org/second")), "second enqueue");
    require(queue.runningCount() == 1, "sequential mode starts one task");
    require(waitFor([&] { return !queue.hasPending(); }), "queue drains");
    QFile order(record); require(order.open(QIODevice::ReadOnly), "execution record exists");
    require(order.readAll() == "https://example.org/first\nhttps://example.org/second\n", "sequential order preserved");
    require(completed.size() == 2, "final file callbacks emitted");
    queue.enqueue(job("https://example.org/fail"));
    require(waitFor([&] { return !queue.hasPending(); }), "failed job completes");
    require(list->currentItem()->text(1).startsWith("Ошибка"), "failed state shown");
    queue.setConcurrency(3);
    queue.enqueue(job("https://example.org/slow-one"));
    queue.enqueue(job("https://example.org/slow-two"));
    queue.enqueue(job("https://example.org/slow-three"));
    queue.enqueue(job("https://example.org/slow-four"));
    require(queue.runningCount() == 3, "parallel mode obeys configured limit");
    queue.setConcurrency(1);
    require(queue.runningCount() == 3, "reducing limit lets current jobs finish");
    require(waitFor([&] { return queue.runningCount() == 1; }), "last task waits for sequential capacity");
    require(waitFor([&] { return !queue.hasPending(); }), "parallel tasks finish without losing jobs");
    DownloadJob missing = job("https://example.org/missing"); missing.executable = "/nonexistent/yt-dlp";
    queue.enqueue(missing); queue.enqueue(job("https://example.org/after-missing"));
    require(waitFor([&] { return !queue.hasPending(); }), "queue continues after failed start");
    require(list->topLevelItem(list->topLevelItemCount()-1)->text(1) == "Готово", "next job succeeds");

    const QString target = temporary.filePath("target"); QDir().mkpath(target);
    auto copied = operateFiles(FileOperation::Copy, {media}, target);
    require(copied.errors.isEmpty() && QFileInfo(copied.copied.first()).isFile() && QFile::exists(media), "copy preserves original");
    auto collision = operateFiles(FileOperation::Move, {media}, target);
    require(!collision.errors.isEmpty() && QFile::exists(media), "collision never overwrites or removes original");
    const QString movedDirectory = temporary.filePath("moved"); QDir().mkpath(movedDirectory);
    auto movedResult = operateFiles(FileOperation::Move, copied.copied, movedDirectory);
    require(movedResult.errors.isEmpty() && !QFile::exists(copied.copied.first()) && QFile::exists(movedResult.moved.first()), "move transfers file");

    QSettings usbSettings(temporary.filePath("usb.ini"), QSettings::IniFormat);
    const UsbVolume flash{"uuid-first", temporary.filePath("usb"), "Test USB", "/dev/test-usb"};
    UsbMusicDestination usb(usbSettings); usb.volumes = [flash] { return QList<UsbVolume>{flash}; };
    require(!usb.hasPreference(flash), "new USB has no preference");
    usb.remember(flash, false); usb.offer(nullptr);
    require(usb.hasPreference(flash) && !usb.preferred(flash) && usb.directory().isEmpty(), "remembered refusal keeps default without dialog");
    usb.remember(flash, true);
    UsbVolume remounted = flash; remounted.root = temporary.filePath("remounted"); remounted.device = "/dev/changed-name";
    UsbMusicDestination nextSession(usbSettings); nextSession.volumes = [remounted] { return QList<UsbVolume>{remounted}; };
    nextSession.offer(nullptr);
    require(nextSession.directory() == QDir(remounted.root).filePath("yt-dlp"), "remembered acceptance follows device identity after remount");
    nextSession.volumes = [] { return QList<UsbVolume>{}; };
    require(nextSession.directory().isEmpty(), "unmounted device cannot remain a destination");
    UsbVolume other = flash; other.id = "uuid-other";
    require(!usb.hasPreference(other), "other USB does not inherit choice");
    usbSettings.clear();
    QTimer::singleShot(0, [] {
        for (auto *widget : QApplication::topLevelWidgets()) if (widget->objectName() == "usbDestinationPrompt") {
            widget->findChild<QCheckBox *>("rememberUsb")->setChecked(true); static_cast<QDialog *>(widget)->accept();
        }
    });
    usb.offer(nullptr); require(usb.directory() == QDir(flash.root).filePath("yt-dlp") && usb.preferred(flash), "USB prompt acceptance remembered");
    usbSettings.clear();
    UsbMusicDestination declined(usbSettings); declined.volumes = [flash] { return QList<UsbVolume>{flash}; };
    QTimer::singleShot(0, [] {
        for (auto *widget : QApplication::topLevelWidgets()) if (widget->objectName() == "usbDestinationPrompt" && widget->isVisible()) {
            widget->findChild<QCheckBox *>("rememberUsb")->setChecked(true); static_cast<QDialog *>(widget)->reject();
        }
    });
    declined.offer(nullptr);
    require(declined.hasPreference(flash) && !declined.preferred(flash) && declined.directory().isEmpty(), "prompt refusal with remember keeps default");
    declined.offer(nullptr);
    require(declined.directory().isEmpty(), "remembered refusal is reused without prompt");
    DownloadJob missingUsb = job("https://example.org/usb-unmounted");
    missingUsb.requiredMount = temporary.filePath("not-mounted"); missingUsb.requiredDevice = "/dev/test-usb";
    queue.enqueue(missingUsb);
    require(!queue.hasPending() && !QDir(missingUsb.requiredMount).exists(), "missing USB fails without recreating mount folder");

    Window window(false); window.show();
    auto *url = window.findChild<QLineEdit *>("url");
    auto *quality = window.findChild<QComboBox *>("quality");
    auto *enqueue = window.findChild<QPushButton *>("enqueue");
    require(!enqueue->isEnabled(), "empty URL disabled");
    require(enqueue->text() == "&Скачать", "download action label");
    for (auto *b : window.findChildren<QPushButton *>()) require(b->text() != "Проверить", "manual inspect button removed");
    url->setText("https://example.org/second");
    require(waitFor([&] { return quality->count() == 3; }), "actual video formats appear in window");
    quality->setCurrentIndex(1);
    require(quality->currentData().toMap().value("selector") == "137+ba", "selected video includes audio");
    auto *type = window.findChild<QButtonGroup *>(); type->button(1)->setChecked(true);
    require(quality->count() == 2 && quality->itemData(1) == "140", "original audio options");
    window.findChild<QComboBox *>("audioEncoding")->setCurrentIndex(1);
    require(quality->count() == 4 && quality->itemData(0) == "320K", "MP3 bitrate options");
    auto *encoding = window.findChild<QComboBox *>("audioEncoding");
    require(encoding->count() == 7, "all audio formats offered");
    encoding->setCurrentIndex(encoding->findData("opus"));
    require(quality->count() == 5 && quality->findData("64K") >= 0, "Opus bitrate options");
    quality->setCurrentIndex(quality->findData("128K"));
    encoding->setCurrentIndex(encoding->findData("flac"));
    require(quality->count() == 1 && !quality->isEnabled(), "FLAC has no lossy bitrate control");
    encoding->setCurrentIndex(encoding->findData("wav"));
    require(!quality->isEnabled(), "WAV has no lossy bitrate control");
    encoding->setCurrentIndex(encoding->findData("m4a"));
    require(quality->isEnabled() && quality->count() == 4, "M4A enables bitrate controls");
    encoding->setCurrentIndex(encoding->findData("opus"));
    require(quality->currentData() == "128K", "bitrate remembered separately per format");
    type->button(0)->setChecked(true);
    require(quality->currentData().toMap().value("height").toInt() == 1080, "preferred resolution retained");
    url->setText("bad link"); require(!enqueue->isEnabled(), "invalid URL disabled");
    url->setText("https://example.org/second");
    require(waitFor([&] { return quality->count() == 3; }), "preview restored after invalid input");
    auto *dialog = window.findChild<QDialog *>("preferences");
    require(dialog && !dialog->isVisible(), "settings start hidden");
    for (auto *action : window.actions()) if (action->text() == "Настроить приложение…") action->trigger();
    require(dialog->isVisible(), "menu opens settings dialog");
    auto *directory = dialog->findChild<QLineEdit *>("downloadFolder");
    directory->setText("/tmp/cancelled-choice"); dialog->reject();
    require(directory->text() == temporary.path(), "cancel restores settings");
    auto *settingsSections = dialog->findChild<QListWidget *>("settingsSections");
    auto *settingsPages = dialog->findChild<QStackedWidget *>("settingsPages");
    require(settingsSections && settingsSections->count() == 5 && settingsSections->item(2)->text() == "Устройства", "settings sidebar has grouped device section");
    require(!(settingsSections->item(0)->flags() & Qt::ItemIsSelectable) && !(settingsSections->item(3)->flags() & Qt::ItemIsSelectable), "group headers cannot be selected");
    auto *settingsSearch = dialog->findChild<QLineEdit *>("settingsSearch");
    settingsSearch->setText("USB");
    require(settingsSections->item(1)->isHidden() && !settingsSections->item(2)->isHidden() && settingsPages->currentIndex() == 1, "search filters sections and selects matching page");
    settingsSearch->setText("no-such-setting");
    require(settingsSections->item(0)->isHidden() && settingsSections->item(2)->isHidden(), "search hides empty groups");
    settingsSearch->clear();
    settingsSections->setCurrentRow(4); require(settingsPages->currentIndex() == 2, "grouped navigation maps advanced page correctly");
    settingsSections->setCurrentRow(1); require(settingsPages->currentIndex() == 0, "grouped navigation maps downloads page correctly");
    require(window.findChildren<QTabWidget *>().isEmpty(), "settings and main window have no top tabs");
    for (auto *label : dialog->findChildren<QLabel *>()) require(!label->text().contains("Музыка в этом сеансе"), "session music row removed");
    auto *mode = dialog->findChild<QComboBox *>("queueMode");
    auto *parallelCount = dialog->findChild<QSpinBox *>("parallelCount");
    mode->setCurrentIndex(1); parallelCount->setValue(4); require(parallelCount->isEnabled(), "parallel count enabled");
    dialog->reject(); require(mode->currentIndex() == 0, "cancel restores queue mode");
    auto *appMenu = window.findChild<QToolButton *>()->menu();
    int visibleActions = 0;
    for (auto *a : appMenu->actions()) if (!a->isSeparator()) ++visibleActions;
    require(visibleActions == 2, "application menu only has settings and details");
    require(!queue.findChild<QCheckBox *>(), "old pause control removed");
    require(action(queue, "Удалить") && action(queue, "Переместить") && action(queue, "Копировать"), "file operation context actions exist");
    for (auto *button : queue.findChildren<QPushButton *>()) require(button->text() != "Удалить" && button->text() != "Переместить" && button->text() != "Копировать", "file actions removed from main view");
    auto *unified = window.findChild<QTreeWidget *>("queueList");
    require(unified && unified->topLevelItemCount() == 1, "folder file appears in shared list");
    require(unified->topLevelItem(0)->data(0, Qt::UserRole + 1).toString() == media, "shared list carries openable path");
    auto *details = window.findChild<QDialog *>("downloadDetails");
    require(details && !details->isVisible(), "details start hidden in separate dialog");
    unified->setCurrentItem(unified->topLevelItem(0));
    action(window, "Подробности загрузки…")->trigger();
    require(details->isVisible(), "menu opens details dialog");
    details->hide();
    window.resize(574, 480); QCoreApplication::processEvents();
    require(window.width() <= 600 && window.height() <= 540, "main window fits compact size");

    DownloadLibrary library;
    library.setDirectory(temporary.path());
    library.recordDownload(media); library.recordDownload(media);
    require(library.files().size() == 1 && library.files().first().recent, "library deduplicates recent and folder file");
    // All successful test jobs target this same fixture: only test one row for deduplication.
    DownloadQueue merged;
    merged.setLibraryFiles(library.files());
    auto *mergedList = merged.findChild<QTreeWidget *>("queueList");
    require(mergedList->topLevelItemCount() == 1, "existing file initially visible");
    merged.fileReady = [&](const QString &path) { library.recordDownload(path); };
    library.changed = [&] { merged.setLibraryFiles(library.files()); };
    merged.enqueue(job("https://example.org/shared"));
    require(waitFor([&] { return !merged.hasPending(); }), "merged list job finishes");
    require(mergedList->topLevelItemCount() == 1, "finished job and existing file share one row");
    require(!mergedList->topLevelItem(0)->icon(0).isNull(), "shared row retains media icon");
    require(mergedList->topLevelItem(0)->data(0, Qt::UserRole + 1).toString() == media, "finished row can open file");
    library.changed = {};
    url->setText(musicPlaylist);
    auto *choose = window.findChild<QPushButton *>("choosePlaylist");
    require(waitFor([&] { return choose->isVisible() && choose->isEnabled(); }), "playlist chooser appears only for playlist");
    require(type->checkedId() == 1 && enqueue->text().contains("2"), "Music defaults to audio and counts available tracks");
    auto *inlineTracks = window.findChild<QTreeWidget *>("playlistTracks");
    require(!inlineTracks->isVisible(), "playlist list hidden by default");
    choose->click(); require(inlineTracks->isVisible(), "playlist list expands inline");
    inlineTracks->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    require(enqueue->text().contains("1"), "download count follows track selection");
    choose->click(); require(!inlineTracks->isVisible(), "playlist list collapses");
    choose->click(); require(inlineTracks->topLevelItem(0)->checkState(0) == Qt::Unchecked, "collapsed selection preserved");
    button(window, "Выбрать все")->click();
    choose->click();
    enqueue->click();
    require(waitFor([&] {
        int done = 0;
        for (int i = 0; i < unified->topLevelItemCount(); ++i) if (unified->topLevelItem(i)->text(1) == "Готово") ++done;
        return done == 2;
    }), "two selected tracks download as independent tasks");
    require(unified->topLevelItemCount() == 3, "playlist tasks coexist with existing file");
    for (int i = 0; i < 2; ++i)
        require(unified->topLevelItem(i)->toolTip(0).contains("music.youtube.com/watch?v=")
            && !unified->topLevelItem(i)->toolTip(0).contains("list="), "each playlist task targets one track");
    // Exercise the asynchronous UI copy and cancellation of destructive confirmation.
    const QString asyncDirectory = temporary.filePath("async-copy"); QDir().mkpath(asyncDirectory);
    for (int i = 0; i < unified->topLevelItemCount(); ++i)
        if (unified->topLevelItem(i)->data(0, Qt::UserRole + 1).toString() == media) unified->setCurrentItem(unified->topLevelItem(i));
    require(action(window, "Копировать")->isEnabled(), "copy enabled for saved file");
    QTimer::singleShot(100, [asyncDirectory] {
        if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
            dialog->setDirectory(asyncDirectory);
            QTimer::singleShot(250, dialog, [dialog] { QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection); });
        }
    });
    QTimer::singleShot(1500, [] { if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    QTimer::singleShot(700, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            std::cerr << "COPY WARNING: " << box->text().toStdString() << std::endl; box->reject();
        }
    });
    action(window, "Копировать")->trigger();
    const QString copiedPath = QDir(asyncDirectory).filePath(QFileInfo(media).fileName());
    auto *uiQueue = static_cast<DownloadQueue *>(unified->parentWidget());
    require(waitFor([&] { return QFile::exists(copiedPath) && !uiQueue->fileOperationInProgress(); }), "async copy completes and updates UI");
    for (int i = 0; i < unified->topLevelItemCount(); ++i)
        if (unified->topLevelItem(i)->data(0, Qt::UserRole + 1).toString() == media) unified->setCurrentItem(unified->topLevelItem(i));
    QTimer::singleShot(0, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->reject(); });
    action(window, "Удалить")->trigger();
    require(QFile::exists(media), "cancelled trash confirmation preserves original");
    QUrl played;
    const auto player = uiQueue->openFile;
    uiQueue->openFile = [&](const QUrl &url) { played = url; };
    auto *selectedFile = unified->currentItem();
    require(QMetaObject::invokeMethod(unified, "itemActivated", Qt::DirectConnection, Q_ARG(QTreeWidgetItem*, selectedFile), Q_ARG(int, 0)), "activation signal available");
    require(played.isEmpty(), "single activation does not start playback");
    require(QMetaObject::invokeMethod(unified, "itemDoubleClicked", Qt::DirectConnection, Q_ARG(QTreeWidgetItem*, selectedFile), Q_ARG(int, 0)), "double click signal available");
    require(played == QUrl::fromLocalFile(media), "double click opens selected file in player"); uiQueue->openFile = player;
    QStringList revealed;
    uiQueue->showFiles = [&](const QStringList &paths) { revealed = paths; };
    require(action(window, "Показать в папке")->isEnabled(), "show in folder enabled for saved file");
    action(window, "Показать в папке")->trigger();
    require(revealed.contains(media), "show in folder receives selected file path");
    QTimer::singleShot(0, [] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        require(menu && menu->objectName() == "fileContextMenu" && menu->actions().size() == 4 && menu->actions().first()->text() == "Показать в папке", "right click includes show in folder"); menu->close();
    });
    const QPoint point = unified->visualItemRect(selectedFile).center();
    QMetaObject::invokeMethod(unified, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point));
    QList<UsbVolume> connected{flash};
    Window deviceWindow(false, [&] { return connected; }); deviceWindow.show();
    auto *devicePreferences = deviceWindow.findChild<QDialog *>("preferences");
    for (auto *a : deviceWindow.actions()) if (a->text() == "Настроить приложение…") a->trigger();
    auto *sections = devicePreferences->findChild<QListWidget *>("settingsSections");
    auto *pages = devicePreferences->findChild<QStackedWidget *>("settingsPages");
    QCoreApplication::processEvents();
    require(sections->mapTo(devicePreferences, QPoint(sections->width(), 0)).x() < pages->mapTo(devicePreferences, QPoint()).x(), "settings navigation positioned left of content");
    sections->setCurrentRow(2);
    auto *devices = devicePreferences->findChild<QListWidget *>("deviceList");
    require(devices->count() == 2 && devices->currentRow() == 0, "devices lists computer and mounted USB");
    require(devices->item(1)->text() == flash.name && !devices->item(1)->text().contains(flash.root), "device list displays name instead of mount path");
    require(devices->item(1)->toolTip().contains(flash.root), "mount path remains in device tooltip");
    deviceWindow.checkUsbDevices();
    UsbVolume extra{"uuid-extra", temporary.filePath("another-usb"), "My Music", "/dev/another-usb"};
    connected << extra; deviceWindow.checkUsbDevices();
    require(devices->count() == 3 && devices->item(2)->text() == "My Music", "open settings discovers newly mounted device automatically");
    connected.removeLast(); deviceWindow.checkUsbDevices();
    require(devices->count() == 2, "open settings removes unmounted device automatically");
    connected[0].name = "Renamed USB"; deviceWindow.checkUsbDevices();
    require(devices->item(1)->text() == "Renamed USB", "device name updates without changing selection");
    require(usbVolumeName("My USB", "Model", 1024) == "My USB", "filesystem label preferred as display name");
    require(usbVolumeName("", "", 1024).startsWith("USB-накопитель"), "unnamed USB gets readable fallback instead of path");
    QCoreApplication::processEvents(); devicePreferences->grab().save("/tmp/ytdlp-devices-preview.png");
    devices->setCurrentRow(1);
    auto *deviceButtons = devicePreferences->findChild<QDialogButtonBox *>();
    connected.clear(); deviceButtons->button(QDialogButtonBox::Ok)->click();
    require(devicePreferences->isVisible() && devicePreferences->findChild<QLabel *>("deviceError")->isVisible(), "unmounted USB cannot be applied");
    connected << flash; deviceButtons->button(QDialogButtonBox::Ok)->click();
    require(!devicePreferences->isVisible(), "mounted device selection applied");
    UsbMusicDestination applied(settings); applied.volumes = [&] { return connected; }; applied.offer(nullptr);
    require(applied.directory(false) == QDir(flash.root).filePath("yt-dlp"), "explicit device choice applies to video as well as audio");
    for (auto *a : deviceWindow.actions()) if (a->text() == "Настроить приложение…") a->trigger();
    require(devices->currentRow() == 1, "devices section reflects current USB choice");
    devices->setCurrentRow(0); devicePreferences->reject();
    for (auto *a : deviceWindow.actions()) if (a->text() == "Настроить приложение…") a->trigger();
    require(devices->currentRow() == 1, "cancel does not change destination");
    devices->setCurrentRow(0); deviceButtons->button(QDialogButtonBox::Ok)->click();
    UsbMusicDestination computerChoice(settings); computerChoice.volumes = [&] { return connected; }; computerChoice.offer(nullptr);
    require(computerChoice.directory().isEmpty(), "computer choice overrides remembered USB preference");
    deviceWindow.hide();
    QList<UsbVolume> hotConnected;
    UsbVolume hotVolume{"uuid-hotplug", temporary.filePath("hot-usb"), "Hot USB", "/dev/hot-usb"};
    Window hotWindow(true, [&] { return hotConnected; }); hotWindow.show(); hotWindow.checkUsbDevices();
    int offers = 0;
    QTimer responder; responder.setInterval(5);
    QObject::connect(&responder, &QTimer::timeout, &hotWindow, [&] {
        auto *prompt = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (prompt && prompt->objectName() == "usbDestinationPrompt") {
            prompt->findChild<QCheckBox *>("rememberUsb")->setChecked(offers > 0); ++offers; prompt->reject();
        }
    });
    responder.start(); hotConnected << hotVolume; hotWindow.checkUsbDevices();
    require(offers == 1, "newly mounted USB prompts during app session");
    hotWindow.checkUsbDevices(); require(offers == 1, "unchanged USB does not prompt on every scan");
    hotConnected.clear(); hotWindow.checkUsbDevices(); hotConnected << hotVolume; hotWindow.checkUsbDevices();
    require(offers == 2, "unremembered choice prompts again after reconnect");
    hotConnected.clear(); hotWindow.checkUsbDevices(); hotConnected << hotVolume; hotWindow.checkUsbDevices();
    require(offers == 2, "remembered refusal suppresses reconnect prompt");
    responder.stop(); hotWindow.close();
    QList<PlaylistTrack> fiveTracks;
    for (int i = 0; i < 6; ++i) fiveTracks << PlaylistTrack{QString("Track %1").arg(i), "", "https://example.org/song", 120, true};
    selection.setTracks(fiveTracks); selection.show(); QCoreApplication::processEvents();
    require(trackList->visualItemRect(trackList->topLevelItem(4)).bottom() < trackList->viewport()->height(), "five playlist rows fit without scrolling");
    window.grab().save(temporary.filePath("preview.png"));
    window.grab().save("/tmp/ytdlp-next-preview.png");
    std::cout << "PASS: preview, formats, playlists, five visible tracks, serial/parallel limits, file copy/move safety, async copy, trash cancellation, USB choices/remount/unmount, main UI\n";
}
