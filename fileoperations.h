#pragma once
#include "uilanguage.h"
#include <QtCore>

enum class FileOperation { Trash, Move, Copy };
struct FileOperationResult {
    QStringList errors, removed, copied;
    QMap<QString, QString> moved;
};
inline bool copyFileNoOverwrite(const QString &source, const QString &target, QString *error = nullptr) {
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) *error = input.errorString();
        return false;
    }

    // NewOnly asks the OS to create the destination exclusively, so another
    // process cannot slip a file in after an exists() check and have it overwritten.
    QFile output(target);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error) *error = output.errorString();
        return false;
    }

    const auto fail = [&](const QString &message) {
        output.close();
        output.remove();
        if (error) *error = message;
        return false;
    };
    QByteArray buffer(1024 * 1024, '\0');
    while (true) {
        const qint64 bytesRead = input.read(buffer.data(), buffer.size());
        if (bytesRead < 0) return fail(input.errorString());
        if (bytesRead == 0) {
            if (input.atEnd()) break;
            return fail(uiText("Не удалось прочитать файл", "Could not read file"));
        }
        qint64 offset = 0;
        while (offset < bytesRead) {
            const qint64 bytesWritten = output.write(buffer.constData() + offset, bytesRead - offset);
            if (bytesWritten <= 0) return fail(output.errorString());
            offset += bytesWritten;
        }
    }
    if (!output.flush()) return fail(output.errorString());
    output.close();
    if (!output.setPermissions(input.permissions())) return fail(output.errorString());
    return true;
}

inline FileOperationResult operateFiles(FileOperation operation, const QStringList &sources, const QString &directory = {}) {
    FileOperationResult result;
    for (const auto &source : sources) {
        const QFileInfo info(source);
        if (!info.isFile()) { result.errors << source + uiText(": файл недоступен", ": file is unavailable"); continue; }
        if (operation == FileOperation::Trash) {
            if (QFile::moveToTrash(source)) result.removed << source;
            else result.errors << source + uiText(": не удалось переместить в корзину", ": could not move to Trash");
            continue;
        }
        const QString target = QDir(directory).absoluteFilePath(info.fileName());
        if (QFileInfo(target).absoluteFilePath() == info.absoluteFilePath()) {
            result.errors << source + uiText(": выбрана исходная папка", ": source folder selected"); continue;
        }
        const QFileInfo targetInfo(target);
        if (targetInfo.exists() || targetInfo.isSymLink()) {
            result.errors << target + uiText(": файл с таким именем уже существует", ": a file with this name already exists"); continue;
        }
        if (operation == FileOperation::Copy) {
            QString error;
            if (copyFileNoOverwrite(source, target, &error)) result.copied << target;
            else result.errors << source + ": " + error;
            continue;
        }
        QFile file(source);
        if (!file.rename(target)) result.errors << source + ": " + file.errorString();
        else result.moved.insert(source, target);
    }
    return result;
}
