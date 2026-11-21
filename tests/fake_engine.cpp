#include <QtCore>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const QString url = args.last();
    if (args.contains("--dump-single-json")) {
        QThread::msleep(url.contains("slow") ? 800 : 40);
        if (url.contains("error")) return 1;
        if (url.contains("malformed")) { std::cout << "not-json\n"; return 0; }
        if (args.contains("--flat-playlist")) {
            if (!args.contains("--yes-playlist") || QUrl(url).path() != "/playlist") return 3;
            const QJsonObject playlist{{"_type", "playlist"}, {"title", "Test playlist"}, {"_test_url", url},
                {"entries", QJsonArray{
                    QJsonObject{{"id", "aaaaaaaaaaa"}, {"title", "First song"}, {"duration", 180}},
                    QJsonObject{{"id", "bbbbbbbbbbb"}, {"title", "[Private video]"}, {"availability", "private"}},
                    QJsonObject{{"id", "ccccccccccc"}, {"title", "Second song"}, {"duration", 120}}, QJsonValue::Null}}};
            std::cout << QJsonDocument(playlist).toJson(QJsonDocument::Compact).constData() << '\n'; return url.contains("PLpartial") ? 1 : 0;
        }
        const QJsonObject info{{"title", url.contains("second") ? "Second video" : "Test video"},
            {"uploader", "Creator"}, {"duration", 123}, {"formats", QJsonArray{
                QJsonObject{{"format_id", "137"}, {"height", 1080}, {"vcodec", "avc1"}, {"acodec", "none"}, {"ext", "mp4"}, {"filesize", 1000000}},
                QJsonObject{{"format_id", "22"}, {"height", 720}, {"vcodec", "avc1"}, {"acodec", "aac"}, {"ext", "mp4"}, {"filesize", 700000}},
                QJsonObject{{"format_id", "140"}, {"vcodec", "none"}, {"acodec", "aac"}, {"ext", "m4a"}, {"abr", 128}, {"filesize", 100000}}
            }}};
        std::cout << QJsonDocument(info).toJson(QJsonDocument::Compact).constData() << '\n'; return 0;
    }
    const int record = args.indexOf("--record");
    if (record > 0) {
        QFile file(args.value(record + 1)); if (!file.open(QIODevice::Append)) return 2;
        file.write(url.toUtf8() + '\n');
    }
    QThread::msleep(url.contains("slow") ? 1200 : 120);
    if (url.contains("fail")) { std::cerr << "ERROR: test failure\n"; return 1; }
    std::cout << "GUI_PROGRESS:100.0%\n";
    const int result = args.indexOf("--result");
    if (result > 0) {
        const QJsonArray path{args.value(result + 1)};
        const QByteArray json = QJsonDocument(path).toJson(QJsonDocument::Compact);
        std::cout << "GUI_FILE:" << json.mid(1, json.size() - 2).constData() << '\n';
    }
    return 0;
}
