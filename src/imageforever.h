#ifndef DUST3D_IMAGE_FOREVER_H
#define DUST3D_IMAGE_FOREVER_H
#include <QImage>
#include <QUuid>
#include <QByteArray>

class ImageForever
{
public:
    static const QImage *get(const QUuid &id);
    static void copy(const QUuid &id, QImage &image);
    static const QByteArray *getPngByteArray(const QUuid &id);
    static QUuid add(const QImage *image, QUuid toId=QUuid());
    static void remove(const QUuid &id);
};

#endif
