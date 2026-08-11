#include "NativeRenderItem.h"

#include <QPointF>
#include <QDebug>
#include <QQuickWindow>
#include <QScreen>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <mutex>

namespace {
LRESULT CALLBACK renderSurfaceWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    } else if (message == WM_LBUTTONDBLCLK) {
        if (auto* item = reinterpret_cast<NativeRenderItem*>(
                GetWindowLongPtrW(window, GWLP_USERDATA))) {
            QMetaObject::invokeMethod(item, "surfaceDoubleClicked", Qt::QueuedConnection);
        }
    }

    return DefWindowProcW(window, message, wParam, lParam);
}
}
#endif

NativeRenderItem::NativeRenderItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, false);
    connect(this, &QQuickItem::visibleChanged, this, &NativeRenderItem::syncSurface);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* itemWindow) {
        if (!itemWindow) {
            destroySurface();
            return;
        }
        connect(itemWindow, &QQuickWindow::widthChanged, this, &NativeRenderItem::syncSurface,
            Qt::UniqueConnection);
        connect(itemWindow, &QQuickWindow::heightChanged, this, &NativeRenderItem::syncSurface,
            Qt::UniqueConnection);
        createSurface();
        syncSurface();
    });
}

NativeRenderItem::~NativeRenderItem()
{
    destroySurface();
}

void NativeRenderItem::setSurfaceVisible(bool visible)
{
    if (m_surfaceVisible == visible)
        return;
    m_surfaceVisible = visible;
    emit surfaceVisibleChanged();
    syncSurface();
}

void NativeRenderItem::componentComplete()
{
    QQuickItem::componentComplete();
    createSurface();
    syncSurface();
}

void NativeRenderItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    syncSurface();
}

void NativeRenderItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange) {
        if (data.window) {
            connect(data.window, &QQuickWindow::widthChanged, this, &NativeRenderItem::syncSurface, Qt::UniqueConnection);
            connect(data.window, &QQuickWindow::heightChanged, this, &NativeRenderItem::syncSurface, Qt::UniqueConnection);
            connect(data.window, &QQuickWindow::screenChanged, this,
                [this](QScreen*) { syncSurface(); });
            createSurface();
            syncSurface();
        } else {
            destroySurface();
        }
    }
}

void NativeRenderItem::createSurface()
{
    if (m_nativeHandle || !window())
        return;

#if defined(Q_OS_WIN)
    static std::once_flag registration;
    static constexpr wchar_t className[] = L"EmuCoreXRenderSurface";
    std::call_once(registration, [] {
        WNDCLASSW windowClass {};
        windowClass.style = CS_DBLCLKS;
        windowClass.lpfnWndProc = renderSurfaceWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = className;
        RegisterClassW(&windowClass);
    });

    const HWND parentWindow = reinterpret_cast<HWND>(window()->winId());
    const HWND childWindow = CreateWindowExW(WS_EX_NOACTIVATE, className, L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 1, 1, parentWindow, nullptr, GetModuleHandleW(nullptr), this);
    if (!childWindow) {
        qWarning() << "EmuCoreX native render surface creation failed:" << GetLastError();
    }
    m_nativeHandle = reinterpret_cast<quintptr>(childWindow);
#endif
    emit surfaceChanged();
}

void NativeRenderItem::destroySurface()
{
#if defined(Q_OS_WIN)
    if (m_nativeHandle)
        DestroyWindow(reinterpret_cast<HWND>(m_nativeHandle));
#endif
    if (m_nativeHandle || m_surfaceWidth || m_surfaceHeight) {
        m_nativeHandle = 0;
        m_surfaceWidth = 0;
        m_surfaceHeight = 0;
        emit surfaceChanged();
    }
}

void NativeRenderItem::syncSurface()
{
    if (!window())
        return;
    createSurface();

    const qreal scale = window()->effectiveDevicePixelRatio();
    const QPointF scenePosition = mapToScene(QPointF(0, 0));
    const int x = qRound(scenePosition.x() * scale);
    const int y = qRound(scenePosition.y() * scale);
    const int widthPixels = qMax(1, qRound(width() * scale));
    const int heightPixels = qMax(1, qRound(height() * scale));
    const qreal rate = window()->screen() ? window()->screen()->refreshRate() : 60.0;

#if defined(Q_OS_WIN)
    if (m_nativeHandle) {
        const HWND childWindow = reinterpret_cast<HWND>(m_nativeHandle);
        SetWindowPos(childWindow, HWND_TOP, x, y, widthPixels, heightPixels,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        ShowWindow(childWindow, isVisible() && m_surfaceVisible ? SW_SHOWNA : SW_HIDE);
    }
#endif

    if (m_surfaceWidth != widthPixels || m_surfaceHeight != heightPixels
        || !qFuzzyCompare(m_surfaceScale, scale) || !qFuzzyCompare(m_refreshRate, rate)) {
        m_surfaceWidth = widthPixels;
        m_surfaceHeight = heightPixels;
        m_surfaceScale = scale;
        m_refreshRate = rate;
        emit surfaceChanged();
    }
}
