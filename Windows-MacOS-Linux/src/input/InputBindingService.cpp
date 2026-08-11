#include "InputBindingService.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>

namespace {
const QStringList kActions {
    QStringLiteral("dpad_up"), QStringLiteral("dpad_down"), QStringLiteral("dpad_left"), QStringLiteral("dpad_right"),
    QStringLiteral("triangle"), QStringLiteral("circle"), QStringLiteral("cross"), QStringLiteral("square"),
    QStringLiteral("l1"), QStringLiteral("r1"), QStringLiteral("l2"), QStringLiteral("r2"),
    QStringLiteral("l3"), QStringLiteral("r3"), QStringLiteral("select"), QStringLiteral("start"),
    QStringLiteral("left_input_toggle"), QStringLiteral("pressure")
};
}

InputBindingService::InputBindingService(QObject* parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope,
          QCoreApplication::organizationName(), QCoreApplication::applicationName())
{
    if (auto* application = QCoreApplication::instance())
        application->installEventFilter(this);
}

QString InputBindingService::settingsKey(int player, const QString& action)
{
    return QStringLiteral("controls/player%1/%2").arg(qBound(1, player, 2)).arg(action);
}

QString InputBindingService::defaultBinding(int player, const QString& action)
{
    if (player != 1)
        return {};
    static const QHash<QString, QString> defaults {
        {QStringLiteral("dpad_up"), QStringLiteral("Keyboard/Up")},
        {QStringLiteral("dpad_down"), QStringLiteral("Keyboard/Down")},
        {QStringLiteral("dpad_left"), QStringLiteral("Keyboard/Left")},
        {QStringLiteral("dpad_right"), QStringLiteral("Keyboard/Right")},
        {QStringLiteral("triangle"), QStringLiteral("Keyboard/W")},
        {QStringLiteral("circle"), QStringLiteral("Keyboard/D")},
        {QStringLiteral("cross"), QStringLiteral("Keyboard/S")},
        {QStringLiteral("square"), QStringLiteral("Keyboard/A")},
        {QStringLiteral("l1"), QStringLiteral("Keyboard/Q")},
        {QStringLiteral("r1"), QStringLiteral("Keyboard/E")},
        {QStringLiteral("l2"), QStringLiteral("Keyboard/1")},
        {QStringLiteral("r2"), QStringLiteral("Keyboard/3")},
        {QStringLiteral("l3"), QStringLiteral("Keyboard/2")},
        {QStringLiteral("r3"), QStringLiteral("Keyboard/4")},
        {QStringLiteral("select"), QStringLiteral("Keyboard/Backspace")},
        {QStringLiteral("start"), QStringLiteral("Keyboard/Return")},
        {QStringLiteral("left_input_toggle"), QStringLiteral("Keyboard/F6")},
        {QStringLiteral("pressure"), QStringLiteral("Keyboard/Shift")}
    };
    return defaults.value(action);
}

QString InputBindingService::binding(int player, const QString& action) const
{
    const QString key = settingsKey(player, action);
    return m_settings.contains(key) ? m_settings.value(key).toString() : defaultBinding(player, action);
}

QString InputBindingService::bindingDisplay(int player, const QString& action) const
{
    QString value = binding(player, action);
    value.replace(QStringLiteral("Keyboard/"), QString());
    value.replace(QStringLiteral("Mouse/"), QStringLiteral("Mouse "));
    return value;
}

void InputBindingService::beginListening(int player, const QString& action)
{
    if (!kActions.contains(action))
        return;
    m_listeningPlayer = qBound(1, player, 2);
    m_listeningAction = action;
    if (!m_listening) {
        m_listening = true;
        emit listeningChanged();
    } else {
        emit listeningChanged();
    }
}

void InputBindingService::cancelListening()
{
    if (!m_listening)
        return;
    m_listening = false;
    m_listeningAction.clear();
    emit listeningChanged();
}

void InputBindingService::clearBinding(int player, const QString& action)
{
    m_settings.setValue(settingsKey(player, action), QString());
    ++m_revision;
    emit mappingsChanged();
    if (m_listening && m_listeningPlayer == player && m_listeningAction == action)
        cancelListening();
}

void InputBindingService::resetPlayer(int player)
{
    const int boundedPlayer = qBound(1, player, 2);
    for (const QString& action : kActions)
        m_settings.remove(settingsKey(boundedPlayer, action));
    ++m_revision;
    emit mappingsChanged();
    cancelListening();
}

void InputBindingService::finishListening(const QString& value)
{
    if (!m_listening || value.isEmpty())
        return;
    m_settings.setValue(settingsKey(m_listeningPlayer, m_listeningAction), value);
    ++m_revision;
    emit mappingsChanged();
    cancelListening();
}

bool InputBindingService::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched)
    if (!m_listening)
        return false;

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->isAutoRepeat())
            return true;
        if (keyEvent->key() == Qt::Key_Escape) {
            cancelListening();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Delete) {
            clearBinding(m_listeningPlayer, m_listeningAction);
            return true;
        }
        const int modifiers = keyEvent->modifiers().toInt() & ~Qt::KeypadModifier;
        const QKeySequence sequence(QKeyCombination::fromCombined(modifiers | keyEvent->key()));
        QString keyName = sequence.toString(QKeySequence::NativeText);
        if (keyName.isEmpty())
            keyName = QKeySequence(keyEvent->key()).toString(QKeySequence::NativeText);
        finishListening(QStringLiteral("Keyboard/") + keyName);
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        finishListening(QStringLiteral("Mouse/%1").arg(static_cast<int>(mouseEvent->button())));
        return true;
    }
    return false;
}
