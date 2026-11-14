#pragma once
#include <QtWidgets>
#include <QtConcurrent>
#include <QStorageInfo>
#include "downloadlibrary.h"
#include "fileoperations.h"
#include "filemanager.h"
#include "usbstorage.h"

struct DownloadJob {
    QString executable, title, url, directory, choice;
    QStringList arguments;
    bool audio = false;
    QString requiredMount, requiredDevice, requiredVolumeId;
};
class DownloadFileList final : public QTreeWidget {
protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) QTreeWidget::mouseDoubleClickEvent(event);
        else event->accept();
    }
};

class DownloadQueue final : public QWidget {
    enum State { Waiting, Running, Done, Failed };
    struct Entry { DownloadJob job; State state = Waiting; QString log; QStringList files; };
    struct Execution { QProcess *process; QByteArray pending; };
    QMap<int, Entry> entries;
    QHash<int, Execution> running;
    int nextId = 1, limit = 1;
    bool closing = false, fileBusy = false;
    QTreeWidget *list;
    QLabel *status;
    QPlainTextEdit *log;
    QAction *trash, *move, *copy, *showInFolder;
    QAction *details;
    QDialog *detailsDialog;
    QFutureWatcher<FileOperationResult> fileWatcher;
    QTreeWidgetItem *row(int id) const {
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            if (list->topLevelItem(i)->data(0, Qt::UserRole).toInt() == id) return list->topLevelItem(i);
        return nullptr;
    }
    QStringList selectedFiles() const {
        QStringList result;
        for (auto *item : list->selectedItems()) {
            const int id = item->data(0, Qt::UserRole).toInt();
            if (id && (entries.value(id).state == Running || entries.value(id).state == Waiting)) continue;
            const QString path = item->data(0, Qt::UserRole + 1).toString();
            if (QFileInfo(path).isFile() && !result.contains(path)) result << path;
        }
        return result;
    }
    void updateControls() {
        const bool files = !fileBusy && !selectedFiles().isEmpty();
        for (auto *button : {trash, move, copy}) button->setEnabled(files);
        showInFolder->setEnabled(!selectedFiles().isEmpty());
        details->setEnabled(list->currentItem() != nullptr);
    }
    void updateStatus() {
        if (fileBusy) status->setText("Обработка файлов…");
        else {
            int waiting = 0;
            for (const auto &entry : entries) if (entry.state == Waiting) ++waiting;
            status->setText(running.isEmpty() && !waiting ? "" : QString("Скачивается: %1 · Ожидает: %2").arg(running.size()).arg(waiting));
        }
        status->setVisible(!status->text().isEmpty()); updateControls();
    }
    void revealLog() {
        const auto *item = list->currentItem(); if (!item) return;
        const int id = item->data(0, Qt::UserRole).toInt();
        const auto entry = entries.value(id);
        log->setPlainText(id ? entry.job.url + "\n" + entry.job.choice + "\n" + entry.job.directory + "\n\n" + entry.log
            : item->data(0, Qt::UserRole + 1).toString());
        detailsDialog->show(); detailsDialog->raise(); detailsDialog->activateWindow();
    }
    void line(int id, const QString &text) {
        if (!entries.contains(id) || text.isEmpty()) return;
        if (text.startsWith("GUI_FILE:")) {
            const auto json = QJsonDocument::fromJson(("[" + text.mid(9) + "]").toUtf8()).array();
            if (json.size() == 1 && json.first().isString()) entries[id].files << QFileInfo(json.first().toString()).absoluteFilePath();
        } else if (text.startsWith("GUI_PROGRESS:")) {
            const auto fields = text.mid(13).split('|'); bool ok;
            const double percent = QString(fields.first()).remove('%').trimmed().toDouble(&ok);
            if (ok && row(id)) {
                QString text = QString("%1%").arg(qBound(0, qRound(percent), 100));
                if (fields.size() > 1 && fields[1].trimmed() != "Unknown") text += " · " + fields[1].trimmed();
                row(id)->setText(1, text);
                if (fields.size() > 2 && fields[2].trimmed() != "Unknown") row(id)->setToolTip(1, "Осталось: " + fields[2].trimmed());
            }
        } else {
            auto &journal = entries[id].log; journal += text + '\n';
            if (journal.size() > 200000) journal.remove(0, journal.size() - 200000);
            if (list->currentItem() == row(id) && detailsDialog->isVisible()) log->appendPlainText(text);
            if (row(id) && (text.startsWith("[Merger]") || text.startsWith("[ExtractAudio]"))) row(id)->setText(1, "Конвертация…");
        }
    }
    void consume(int id, bool flush = false) {
        if (!running.contains(id)) return;
        auto &execution = running[id]; execution.pending += execution.process->readAllStandardOutput();
        int end;
        while ((end = execution.pending.indexOf('\n')) >= 0) {
            const QString text = QString::fromUtf8(execution.pending.left(end)).trimmed();
            execution.pending.remove(0, end + 1); line(id, text);
        }
        if (flush && !execution.pending.isEmpty()) { line(id, QString::fromUtf8(execution.pending).trimmed()); execution.pending.clear(); }
        if (execution.pending.size() > 1024 * 1024) execution.pending = execution.pending.right(1024 * 1024);
    }
    void finish(int id, bool success) {
        if (!running.contains(id)) return;
        consume(id, true);
        auto *worker = running.take(id).process; worker->deleteLater();
        entries[id].state = success ? Done : Failed;
        if (row(id)) row(id)->setText(1, success ? "Готово" : "Ошибка · см. подробности");
        const QStringList files = entries[id].files;
        if (fileReady) for (const auto &path : files) fileReady(path);
        updateStatus(); if (changed) changed();
        QTimer::singleShot(0, this, [this] { startNext(); });
    }
    void startNext() {
        if (closing) return;
        for (int i = 0; i < list->topLevelItemCount() && running.size() < limit; ++i) {
            const int id = list->topLevelItem(i)->data(0, Qt::UserRole).toInt();
            if (!id || !entries.contains(id) || entries[id].state != Waiting) continue;
            const auto job = entries[id].job;
            if (!job.requiredMount.isEmpty()) {
                QStorageInfo disk(job.requiredMount); disk.refresh();
                bool identityMatches = job.requiredVolumeId.isEmpty();
                if (!identityMatches) for (const auto &volume : mountedUsbVolumes())
                    if (volume.id == job.requiredVolumeId && volume.root == job.requiredMount && volume.device == job.requiredDevice) identityMatches = true;
                if (!identityMatches || !disk.isValid() || !disk.isReady() || disk.isReadOnly()
                    || QDir::cleanPath(disk.rootPath()) != QDir::cleanPath(job.requiredMount)
                    || QString::fromLocal8Bit(disk.device()) != job.requiredDevice) {
                    entries[id].state = Failed; line(id, "Носитель отключён или недоступен. Файл не будет сохранён на системный диск.");
                    row(id)->setText(1, "Носитель недоступен"); continue;
                }
            }
            auto *worker = new QProcess(this); worker->setProcessChannelMode(QProcess::MergedChannels);
            entries[id].state = Running; running.insert(id, {worker, {}});
            row(id)->setText(1, "Получение информации…");
            connect(worker, &QProcess::readyReadStandardOutput, this, [this, id] { consume(id); });
            connect(worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, id](int code, QProcess::ExitStatus state) { finish(id, code == 0 && state == QProcess::NormalExit); });
            connect(worker, &QProcess::errorOccurred, this, [this, id, worker](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) { line(id, worker->errorString()); finish(id, false); }
            });
            worker->start(job.executable, job.arguments);
        }
        updateStatus();
    }
    void operate(FileOperation operation) {
        const QStringList sources = selectedFiles(); if (sources.isEmpty() || fileBusy) return;
        QString directory;
        if (operation == FileOperation::Trash) {
            QMessageBox question(QMessageBox::Question, "Удалить файлы", QString("Переместить выбранные файлы в корзину (%1)?").arg(sources.size()), QMessageBox::NoButton, this);
            auto *yes = question.addButton("Удалить", QMessageBox::DestructiveRole);
            auto *no = question.addButton("Отмена", QMessageBox::RejectRole); question.setDefaultButton(no); question.exec();
            if (question.clickedButton() != yes) return;
        } else {
            directory = QFileDialog::getExistingDirectory(this, operation == FileOperation::Copy ? "Копировать в папку" : "Переместить в папку");
            if (directory.isEmpty()) return;
        }
        fileBusy = true; updateStatus();
        fileWatcher.setFuture(QtConcurrent::run([operation, sources, directory] { return operateFiles(operation, sources, directory); }));
    }
    void discardPath(const QString &path) {
        for (int i = list->topLevelItemCount() - 1; i >= 0; --i) {
            auto *item = list->topLevelItem(i);
            if (item->data(0, Qt::UserRole + 1).toString() != path) continue;
            const int id = item->data(0, Qt::UserRole).toInt();
            if (id && entries[id].state == Running) continue;
            entries.remove(id); delete list->takeTopLevelItem(i);
        }
    }
public:
    std::function<void(const QStringList &)> showFiles = revealInFileManager;
    std::function<void(const QUrl &)> openFile = [](const QUrl &url) { QDesktopServices::openUrl(url); };
    std::function<void(const QString &)> fileReady;
    std::function<void(const QString &, const QString &)> fileRelocated;
    std::function<void()> changed;
    explicit DownloadQueue(QWidget *parent = nullptr) : QWidget(parent) {
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
        auto *title = new QLabel("Загрузки"); QFont bold = title->font(); bold.setBold(true); title->setFont(bold); layout->addWidget(title);
        list = new DownloadFileList; list->setObjectName("queueList"); list->setHeaderLabels({"Файл", "Состояние"});
        list->setRootIsDecorated(false); list->setMinimumHeight(140); list->setIconSize(QSize(64, 36));
        list->setSelectionMode(QAbstractItemView::ExtendedSelection); list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->header()->setSectionResizeMode(0, QHeaderView::Stretch); list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        list->setAccessibleName("Загрузки и сохранённые файлы"); layout->addWidget(list, 1);
        trash = new QAction(QIcon::fromTheme("user-trash"), "Удалить", this);
        move = new QAction(QIcon::fromTheme("go-jump"), "Переместить", this);
        copy = new QAction(QIcon::fromTheme("edit-copy"), "Копировать", this);
        showInFolder = new QAction(QIcon::fromTheme("folder-open"), "Показать в папке", this);
        connect(showInFolder, &QAction::triggered, this, [this] { if (showFiles) showFiles(selectedFiles()); });
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
            auto *item = list->itemAt(point); if (!item) return;
            if (!item->isSelected()) list->setCurrentItem(item);
            updateControls(); QMenu menu; menu.setObjectName("fileContextMenu");
            for (auto *action : {showInFolder, trash, move, copy}) menu.addAction(action);
            menu.exec(list->viewport()->mapToGlobal(point));
        });
        status = new QLabel; status->setWordWrap(true); status->setTextFormat(Qt::PlainText); layout->addWidget(status);
        details = new QAction(QIcon::fromTheme("document-properties"), "Подробности загрузки…", this);
        detailsDialog = new QDialog(this); detailsDialog->setObjectName("downloadDetails"); detailsDialog->setWindowTitle("Подробности загрузки");
        auto *detailsLayout = new QVBoxLayout(detailsDialog); log = new QPlainTextEdit; log->setReadOnly(true); log->setMaximumBlockCount(1500); detailsLayout->addWidget(log);
        auto *close = new QDialogButtonBox(QDialogButtonBox::Close); detailsLayout->addWidget(close);
        connect(close, &QDialogButtonBox::rejected, detailsDialog, &QDialog::hide); detailsDialog->resize(640, 360);
        connect(details, &QAction::triggered, this, [this] { revealLog(); });
        connect(list, &QTreeWidget::itemSelectionChanged, this, [this] { updateControls(); });
        const auto play = [this](QTreeWidgetItem *item) {
            if (!item) return;
            const auto path = item->data(0, Qt::UserRole + 1).toString();
            if (QFileInfo(path).isFile() && openFile) openFile(QUrl::fromLocalFile(path));
        };
        connect(list, &QTreeWidget::itemDoubleClicked, this, [play](QTreeWidgetItem *item) { play(item); });
        for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
            auto *shortcut = new QShortcut(QKeySequence(key), list);
            shortcut->setContext(Qt::WidgetWithChildrenShortcut);
            connect(shortcut, &QShortcut::activated, this, [this, play] { play(list->currentItem()); });
        }
        connect(trash, &QAction::triggered, this, [this] { operate(FileOperation::Trash); });
        connect(move, &QAction::triggered, this, [this] { operate(FileOperation::Move); });
        connect(copy, &QAction::triggered, this, [this] { operate(FileOperation::Copy); });
        connect(&fileWatcher, &QFutureWatcher<FileOperationResult>::finished, this, [this] {
            fileBusy = false; const auto result = fileWatcher.result();
            for (const auto &path : result.removed) { discardPath(path); if (fileRelocated) fileRelocated(path, {}); }
            for (auto it = result.moved.cbegin(); it != result.moved.cend(); ++it) {
                for (auto &entry : entries) for (auto &path : entry.files) if (path == it.key()) path = it.value();
                for (int i = 0; i < list->topLevelItemCount(); ++i)
                    if (list->topLevelItem(i)->data(0, Qt::UserRole + 1).toString() == it.key()) list->topLevelItem(i)->setData(0, Qt::UserRole + 1, it.value());
                if (fileRelocated) fileRelocated(it.key(), it.value());
            }
            if (fileReady) for (const auto &path : result.copied) fileReady(path);
            if (changed) changed(); updateStatus();
            if (!result.errors.isEmpty()) QMessageBox::warning(this, "Не все файлы обработаны", result.errors.join('\n'));
        });
        updateStatus();
    }
    ~DownloadQueue() override { shutdown(); fileWatcher.waitForFinished(); }
    void addActionsToMenu(QMenu *menu) { menu->addAction(details); }
    void setConcurrency(int value) { limit = qBound(1, value, 8); startNext(); }
    int runningCount() const { return running.size(); }
    bool fileOperationInProgress() const { return fileBusy; }
    bool hasPending() const {
        for (const auto &entry : entries) if (entry.state == Waiting || entry.state == Running) return true;
        return false;
    }
    void shutdown() {
        closing = true; fileReady = {}; fileRelocated = {}; changed = {};
        const auto processes = running;
        for (const auto &execution : processes) { execution.process->disconnect(this); execution.process->kill(); }
        for (const auto &execution : processes) execution.process->waitForFinished(1500);
        running.clear();
    }
    bool enqueue(const DownloadJob &job) {
        for (const auto &entry : entries)
            if ((entry.state == Waiting || entry.state == Running) && entry.job.arguments == job.arguments && entry.job.executable == job.executable) return false;
        const int id = nextId++; Entry entry; entry.job = job; entries.insert(id, entry);
        auto *item = new QTreeWidgetItem({job.title, "Ожидает загрузки"});
        int position = 0;
        while (position < list->topLevelItemCount() && list->topLevelItem(position)->data(0, Qt::UserRole).toInt()) ++position;
        item->setData(0, Qt::UserRole, id); list->insertTopLevelItem(position, item);
        item->setIcon(0, QIcon::fromTheme(job.audio ? "audio-x-generic" : "video-x-generic", style()->standardIcon(QStyle::SP_FileIcon)));
        item->setToolTip(0, job.url + "\n" + job.directory); list->setCurrentItem(item); startNext(); return true;
    }
    void setLibraryFiles(const QList<LibraryFile> &files) {
        QStringList selected;
        for (auto *item : list->selectedItems()) selected << item->data(0, Qt::UserRole + 1).toString();
        for (int i = list->topLevelItemCount() - 1; i >= 0; --i)
            if (!list->topLevelItem(i)->data(0, Qt::UserRole).toInt()) delete list->takeTopLevelItem(i);
        for (const auto &file : files) {
            QTreeWidgetItem *item = nullptr;
            for (auto it = entries.cbegin(); it != entries.cend(); ++it) if (it.value().files.contains(file.path) && row(it.key())) { item = row(it.key()); break; }
            if (!item) item = new QTreeWidgetItem(list, {QFileInfo(file.path).fileName(), (file.recent ? "Недавно · " : "") + QLocale().formattedDataSize(QFileInfo(file.path).size())});
            item->setIcon(0, file.icon); item->setData(0, Qt::UserRole + 1, file.path); item->setToolTip(0, file.path);
            if (selected.contains(file.path)) item->setSelected(true);
        }
        updateControls();
    }
};
