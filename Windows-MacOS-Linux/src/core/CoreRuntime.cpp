#include "CoreRuntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSysInfo>

CoreRuntime::CoreRuntime()
{
    load();
}

bool CoreRuntime::inspectGame(const QString& path, EmuCoreXGameMetadata* metadata) const
{
    if (!m_inspectGame || !metadata)
        return false;
    const QByteArray utf8Path = QFileInfo(path).absoluteFilePath().toUtf8();
    return m_inspectGame(utf8Path.constData(), metadata) != 0;
}

QStringList CoreRuntime::candidatePaths() const
{
    QStringList paths;
    const QString overridePath = QProcessEnvironment::systemEnvironment().value(QStringLiteral("EMUCOREX_CORE_PATH"));
    if (!overridePath.isEmpty())
        paths.append(QDir::cleanPath(overridePath));

    const QString appDirectory = QCoreApplication::applicationDirPath();
    const QString architecture = QSysInfo::currentCpuArchitecture().toLower().contains(QStringLiteral("arm"))
        ? QStringLiteral("arm64") : QStringLiteral("x64");
#if defined(Q_OS_WIN)
    const QString fileName = QStringLiteral("EmuCoreXCore.dll");
#elif defined(Q_OS_MACOS)
    const QString fileName = QStringLiteral("libEmuCoreXCore.dylib");
#else
    const QString fileName = QStringLiteral("libEmuCoreXCore.so");
#endif
    paths.append(QDir(appDirectory).filePath(QStringLiteral("cores/%1/%2").arg(architecture, fileName)));
    paths.append(QDir(appDirectory).filePath(fileName));
    return paths;
}

void CoreRuntime::load()
{
    QStringList errors;
    for (const QString& path : candidatePaths()) {
        if (!QFileInfo::exists(path))
            continue;
        m_library.setFileName(path);
        if (!m_library.load()) {
            errors.append(m_library.errorString());
            continue;
        }

        const auto abiVersion = reinterpret_cast<AbiVersionFunction>(m_library.resolve("emucorex_core_abi_version"));
        const auto architecture = reinterpret_cast<ArchitectureFunction>(m_library.resolve("emucorex_core_architecture"));
        const auto initialize = reinterpret_cast<InitializeFunction>(m_library.resolve("emucorex_core_initialize"));
        const auto inspectGame = reinterpret_cast<InspectGameFunction>(m_library.resolve("emucorex_core_inspect_game"));
        if (!abiVersion || !architecture || !initialize || !inspectGame || abiVersion() != EMUCOREX_CORE_ABI_VERSION) {
            errors.append(QStringLiteral("%1: incompatible EmuCoreX core ABI").arg(path));
            m_library.unload();
            continue;
        }

        m_architecture = QString::fromLatin1(architecture());
        const QString expected = QSysInfo::currentCpuArchitecture().toLower().contains(QStringLiteral("arm"))
            ? QStringLiteral("arm64") : QStringLiteral("x64");
        if (m_architecture != expected) {
            errors.append(QStringLiteral("%1: expected %2 core, found %3").arg(path, expected, m_architecture));
            m_library.unload();
            m_architecture.clear();
            continue;
        }
        const QByteArray resourcesPath = QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral("resources")).toUtf8();
        if (!initialize(resourcesPath.constData())) {
            errors.append(QStringLiteral("%1: failed to initialize core resources").arg(path));
            m_library.unload();
            m_architecture.clear();
            continue;
        }
        m_inspectGame = inspectGame;
        m_errorString.clear();
        return;
    }
    m_errorString = errors.isEmpty() ? QStringLiteral("EmuCoreX core module was not found") : errors.join(QLatin1Char('\n'));
}
