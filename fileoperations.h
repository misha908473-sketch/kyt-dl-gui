#pragma once
#include <QtCore>

enum class FileOperation { Trash, Move, Copy };
struct FileOperationResult {
    QStringList errors, removed, copied;
    QMap<QString, QString> moved;
};
inline FileOperationResult operateFiles(FileOperation operation, const QStringList &sources, const QString &directory = {}) {
    FileOperationResult result;
    for (const auto &source : sources) {
        const QFileInfo info(source);
        if (!info.isFile()) { result.errors << source + ": файл недоступен"; continue; }
        if (operation == FileOperation::Trash) {
            if (QFile::moveToTrash(source)) result.removed << source;
            else result.errors << source + ": не удалось переместить в корзину";
            continue;
        }
        const QString target = QDir(directory).absoluteFilePath(info.fileName());
        if (QFileInfo(target).absoluteFilePath() == info.absoluteFilePath()) {
            result.errors << source + ": выбрана исходная папка"; continue;
        }
        // QFile never overwrites an existing target. Across filesystems rename uses a safe copy/remove fallback.
        if (QFile::exists(target)) { result.errors << target + ": файл с таким именем уже существует"; continue; }
        QFile file(source);
        const bool success = operation == FileOperation::Copy ? file.copy(target) : file.rename(target);
        if (!success) result.errors << source + ": " + file.errorString();
        else if (operation == FileOperation::Copy) result.copied << target;
        else result.moved.insert(source, target);
    }
    return result;
}
