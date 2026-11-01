#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <QStringList>
class WallpaperBackdrop {
public:
    bool reload(QSize decodeLimit = QSize(3840,2160));
    QImage sample(const QRect &globalArea,const QRect &screen,qreal dpr) const;
    QSize decodedSize() const { return m_source.size(); }
    QString path() const { return m_path; }
    QStringList sourceWatchPaths() const { return {m_path,m_videoPath,m_videoPoster}; }
private:
    QString m_path;
    QString m_videoPath,m_videoPoster;
    qint64 m_modified=0,m_size=0;
    QImage m_source;
    QSize m_originalSize,m_decodeLimit;
    int m_mode=0;
};
