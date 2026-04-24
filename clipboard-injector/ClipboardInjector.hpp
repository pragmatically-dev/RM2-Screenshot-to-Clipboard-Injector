#pragma once

#include <memory>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QVariant>
#include "rm_Line.hpp"
#include "rm_SceneItem.hpp"

class ClipboardInjector : public QObject
{
    Q_OBJECT
    public:
        explicit ClipboardInjector(QObject *parent = nullptr) : QObject(parent) {}

        Q_INVOKABLE void sleepMs(int ms);

        // Load scene items from a JSON file (xovi-stickers format)
        Q_INVOKABLE QList<std::shared_ptr<SceneItem>> loadFromJSON(const QString& path);

        // Setup vtable pointer from existing clipboard items
        Q_INVOKABLE bool setupVtablePtr(const QList<std::shared_ptr<SceneItem>>& items);

        // Create a helper line for vtable extraction
        Q_INVOKABLE Line createLine(const QPointF& start, const QPointF& end);
        Q_INVOKABLE Line createCircle(const QPointF& center, float radius);

        // Capture a region of the live framebuffer, edge-detect, and write JSON
        Q_INVOKABLE bool captureArea(int x, int y, int w, int h);
};
