#pragma once

#include <QQuickItem>

class NativeRenderItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(quintptr nativeHandle READ nativeHandle NOTIFY surfaceChanged)
    Q_PROPERTY(int surfaceWidth READ surfaceWidth NOTIFY surfaceChanged)
    Q_PROPERTY(int surfaceHeight READ surfaceHeight NOTIFY surfaceChanged)
    Q_PROPERTY(qreal surfaceScale READ surfaceScale NOTIFY surfaceChanged)
    Q_PROPERTY(qreal refreshRate READ refreshRate NOTIFY surfaceChanged)
    Q_PROPERTY(bool surfaceVisible READ surfaceVisible WRITE setSurfaceVisible NOTIFY surfaceVisibleChanged)

public:
    explicit NativeRenderItem(QQuickItem* parent = nullptr);
    ~NativeRenderItem() override;

    quintptr nativeHandle() const { return m_nativeHandle; }
    int surfaceWidth() const { return m_surfaceWidth; }
    int surfaceHeight() const { return m_surfaceHeight; }
    qreal surfaceScale() const { return m_surfaceScale; }
    qreal refreshRate() const { return m_refreshRate; }
    bool surfaceVisible() const { return m_surfaceVisible; }
    void setSurfaceVisible(bool visible);

signals:
    void surfaceChanged();
    void surfaceVisibleChanged();
    void surfaceDoubleClicked();

protected:
    void componentComplete() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    void createSurface();
    void destroySurface();
    void syncSurface();

    quintptr m_nativeHandle = 0;
    int m_surfaceWidth = 0;
    int m_surfaceHeight = 0;
    qreal m_surfaceScale = 1.0;
    qreal m_refreshRate = 60.0;
    bool m_surfaceVisible = true;
};
