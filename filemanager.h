#pragma once
#include <QtWidgets>
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include <QtDBus>
#endif

inline void revealInFileManager(const QStringList &paths) {
    QStringList uris;
    for (const auto &path : paths) if (QFileInfo(path).isFile()) uris << QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded);
    if (uris.isEmpty()) return;
    const QString first = QUrl(uris.first()).toLocalFile();
    const QUrl parent = QUrl::fromLocalFile(QFileInfo(first).absolutePath());
#ifdef Q_OS_WIN
    if (!QProcess::startDetached("explorer.exe", {"/select," + QDir::toNativeSeparators(first)})) QDesktopServices::openUrl(parent);
#elif defined(Q_OS_MACOS)
    if (!QProcess::startDetached("/usr/bin/open", {"-R", first})) QDesktopServices::openUrl(parent);
#else
    auto message = QDBusMessage::createMethodCall("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1", "ShowItems");
    message << uris << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 3000), qApp);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, qApp, [watcher, parent] {
        const QDBusPendingReply<> result = *watcher;
        if (result.isError()) QDesktopServices::openUrl(parent);
        watcher->deleteLater();
    });
#endif
}
