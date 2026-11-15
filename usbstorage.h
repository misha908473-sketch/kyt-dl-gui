#pragma once
#include "uilanguage.h"
#include <QtWidgets>
#include <QStorageInfo>
#include <functional>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

struct UsbVolume { QString id, root, name, device; };
inline QString usbConnectionKey(const UsbVolume &volume) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonArray{volume.id, volume.root, volume.device}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
inline QString usbVolumeName(const QString &label, const QString &model, qint64 bytes) {
    if (!label.trimmed().isEmpty()) return label.trimmed();
    const QString title = model.trimmed().isEmpty() ? uiText("USB-накопитель", "USB drive") : model.trimmed();
    return bytes > 0 ? title + " · " + QLocale().formattedDataSize(bytes) : title;
}
inline QList<UsbVolume> mountedUsbVolumes() {
    QList<UsbVolume> result;
    for (const auto &disk : QStorageInfo::mountedVolumes()) {
        if (!disk.isValid() || !disk.isReady() || disk.isReadOnly() || disk.isRoot()) continue;
        QString identity, model;
        const QString device = QString::fromLocal8Bit(disk.device());
#ifdef Q_OS_LINUX
        const QString canonicalDevice = QFileInfo(device).canonicalFilePath();
        const QString block = QFileInfo(canonicalDevice).fileName();
        const QString sys = QFileInfo("/sys/class/block/" + block).canonicalFilePath();
        if (!QRegularExpression("/usb[0-9]+/").match(sys).hasMatch()) continue;
        QDir node(sys);
        for (int depth = 0; depth < 3 && !node.isRoot(); ++depth) {
            QFile product(node.filePath("device/model"));
            if (product.open(QIODevice::ReadOnly)) { model = QString::fromLocal8Bit(product.read(256)).trimmed(); break; }
            node.cdUp();
        }
        for (const QString &directory : {QString("/dev/disk/by-uuid"), QString("/dev/disk/by-id")}) {
            for (const auto &entry : QDir(directory).entryInfoList(QDir::System | QDir::Files | QDir::NoDotAndDotDot, QDir::Name))
                if (entry.canonicalFilePath() == canonicalDevice) { identity = directory + "/" + entry.fileName(); break; }
            if (!identity.isEmpty()) break;
        }
#elif defined(Q_OS_WIN)
        const QString root = QDir::toNativeSeparators(disk.rootPath());
        if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16())) != DRIVE_REMOVABLE) continue;
        DWORD serial = 0;
        if (GetVolumeInformationW(reinterpret_cast<LPCWSTR>(root.utf16()), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) identity = QString::number(serial, 16);
#else
        continue;
#endif
        result.append({identity, disk.rootPath(), usbVolumeName(disk.name(), model, disk.bytesTotal()), device});
    }
    return result;
}
class UsbMusicDestination {
    QSettings &settings;
    UsbVolume selected;
    bool chosen = false, allFiles = false;
    QString preferenceKey(const UsbVolume &volume) const {
        return "usbChoices/" + QCryptographicHash::hash(volume.id.toUtf8(), QCryptographicHash::Sha256).toHex();
    }
public:
    std::function<QList<UsbVolume>()> volumes = mountedUsbVolumes;
    explicit UsbMusicDestination(QSettings &value) : settings(value) {}
    void remember(const UsbVolume &volume, bool use, bool forAllFiles = false) {
        if (!volume.id.isEmpty()) { settings.setValue(preferenceKey(volume), use); settings.setValue(preferenceKey(volume) + "AllFiles", use && forAllFiles); }
    }
    bool hasPreference(const UsbVolume &volume) const { return !volume.id.isEmpty() && settings.contains(preferenceKey(volume)); }
    bool preferred(const UsbVolume &volume) const { return settings.value(preferenceKey(volume), false).toBool(); }
    void select(const UsbVolume &volume, bool forAllFiles = false) { selected = volume; chosen = true; allFiles = forAllFiles; }
    void clear() { chosen = false; allFiles = false; selected = {}; }
    bool available() const {
        if (!chosen) return false;
        for (const auto &volume : volumes())
            if (QDir::cleanPath(volume.root) == QDir::cleanPath(selected.root)
                && volume.device == selected.device && (selected.id.isEmpty() || volume.id == selected.id)) return true;
        return false;
    }
    QString directory(bool audio = true) const { return (audio || allFiles) && available() ? QDir(selected.root).filePath("yt-dlp") : QString(); }
    UsbVolume volume() const { return selected; }
    void offer(QWidget *parent) { offer(parent, volumes()); }
    void offer(QWidget *parent, const QList<UsbVolume> &mounted) {
        // Remembered acceptance takes priority; remembered refusals never prompt again.
        for (const auto &volume : mounted) if (hasPreference(volume) && preferred(volume)) { select(volume, settings.value(preferenceKey(volume) + "AllFiles", false).toBool()); return; }
        for (const auto &volume : mounted) {
            if (hasPreference(volume)) continue;
            QDialog dialog(parent); dialog.setObjectName("usbDestinationPrompt"); dialog.setWindowTitle(uiText("Сохранять музыку на USB?", "Save music to USB?"));
            auto *layout = new QVBoxLayout(&dialog);
            auto *message = new QLabel(uiText("Обнаружен носитель «", "Storage “") + volume.name + "».\nСохранять музыку в " + QDir(volume.root).filePath("yt-dlp") + "?\nОбычная папка загрузок останется в настройках.");
            message->setTextFormat(Qt::PlainText); message->setWordWrap(true); layout->addWidget(message);
            auto *rememberChoice = new QCheckBox(uiText("Запомнить выбор", "Remember my choice")); rememberChoice->setObjectName("rememberUsb");
            rememberChoice->setEnabled(!volume.id.isEmpty());
            if (volume.id.isEmpty()) rememberChoice->setToolTip(uiText("Не удалось определить постоянный идентификатор носителя.", "Could not determine the storage device’s permanent ID."));
            layout->addWidget(rememberChoice);
            auto *buttons = new QDialogButtonBox;
            auto *yes = buttons->addButton(uiText("Сохранять на USB", "Save to USB"), QDialogButtonBox::AcceptRole);
            auto *no = buttons->addButton(uiText("Оставить обычную папку", "Keep regular folder"), QDialogButtonBox::RejectRole);
            no->setDefault(true); layout->addWidget(buttons);
            QObject::connect(yes, &QPushButton::clicked, &dialog, &QDialog::accept);
            QObject::connect(no, &QPushButton::clicked, &dialog, &QDialog::reject);
            const bool use = dialog.exec() == QDialog::Accepted;
            if (rememberChoice->isChecked()) remember(volume, use);
            if (use) { select(volume); return; }
        }
    }
};
