#include <QGuiApplication>
#include <QImage>
#include <QDebug>

// Render newly authored SVG artwork with the same SVG loader used by Qt.
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc != 3) { qCritical() << "Usage: render-tray-vector source.svg output.png"; return 2; }
    const QImage artwork(QString::fromLocal8Bit(argv[1]));
    if (artwork.isNull() || !artwork.save(QString::fromLocal8Bit(argv[2]), "PNG")) {
        qCritical() << "Cannot render or save" << argv[1];
        return 1;
    }
    return 0;
}
