#include "CoreRuntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSysInfo>

#include <vector>

#if defined(Q_OS_WIN)
#  include <windows.h>
#endif

namespace {
QByteArray corePath(const QString& path)
{
    const QString cleanPath = QDir::cleanPath(path);
#if defined(Q_OS_WIN)
    return QDir::toNativeSeparators(cleanPath).toUtf8();
#else
    return cleanPath.toUtf8();
#endif
}
}

CoreRuntime::CoreRuntime()
{
    load();
}

CoreRuntime::~CoreRuntime()
{
    shutdown();
}

bool CoreRuntime::inspectGame(const QString& path, EmuCoreXGameMetadata* metadata) const
{
    if (!m_inspectGame || !metadata)
        return false;
    const QByteArray utf8Path = corePath(QFileInfo(path).absoluteFilePath());
    return m_inspectGame(utf8Path.constData(), metadata) != 0;
}

bool CoreRuntime::configure(const QString& dataPath, const QString& biosPath) const
{
    if (!m_configure || m_coreDirectory.isEmpty() || dataPath.isEmpty())
        return false;
    const QByteArray resources = corePath(QDir(m_coreDirectory).filePath(QStringLiteral("resources")));
    const QByteArray data = corePath(dataPath);
    const QByteArray bios = corePath(QFileInfo(biosPath).absoluteFilePath());
    const EmuCoreXCoreConfiguration configuration {
        resources.constData(), data.constData(), biosPath.isEmpty() ? nullptr : bios.constData()
    };
    return m_configure(&configuration) != 0;
}

void CoreRuntime::setRenderSurface(const EmuCoreXRenderSurface* surface) const
{
    if (m_setRenderSurface)
        m_setRenderSurface(surface);
}

bool CoreRuntime::startGame(const QString& path) const
{
    if (!m_startGame)
        return false;
    const QByteArray utf8Path = corePath(QFileInfo(path).absoluteFilePath());
    return m_startGame(utf8Path.constData()) != 0;
}

bool CoreRuntime::startBios() const
{
    return m_startBios && m_startBios() != 0;
}

EmuCoreXCoreState CoreRuntime::state() const
{
    return m_state ? static_cast<EmuCoreXCoreState>(m_state()) : EMUCOREX_CORE_STATE_ERROR;
}

void CoreRuntime::setPaused(bool paused) const
{
    if (m_setPaused)
        m_setPaused(paused ? 1 : 0);
}

void CoreRuntime::shutdown() const
{
    if (m_shutdown)
        m_shutdown();
}

bool CoreRuntime::saveState(int slot) const
{
    return m_saveState && m_saveState(slot) != 0;
}

bool CoreRuntime::loadState(int slot) const
{
    return m_loadState && m_loadState(slot) != 0;
}

QString CoreRuntime::lastCoreError() const
{
    if (!m_lastError)
        return m_errorString;
    const uint32_t required = m_lastError(nullptr, 0);
    if (required <= 1)
        return {};
    std::vector<char> buffer(required);
    m_lastError(buffer.data(), required);
    return QString::fromUtf8(buffer.data());
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
#if defined(Q_OS_WIN)
        // The core and its upstream PCSX2 dependencies are intentionally kept
        // together under cores/<arch>. Windows does not search a dynamically
        // loaded DLL's directory for all transitive dependencies by default.
        // Keep this trusted application directory in the loader search path for
        // the lifetime of the process, including delayed graphics/audio loads.
        const QString nativeCoreDirectory = QDir::toNativeSeparators(QFileInfo(path).absolutePath());
        SetDllDirectoryW(reinterpret_cast<LPCWSTR>(nativeCoreDirectory.utf16()));
#endif
        m_library.setFileName(path);
        if (!m_library.load()) {
            errors.append(m_library.errorString());
            continue;
        }

        const auto abiVersion = reinterpret_cast<AbiVersionFunction>(m_library.resolve("emucorex_core_abi_version"));
        const auto architecture = reinterpret_cast<ArchitectureFunction>(m_library.resolve("emucorex_core_architecture"));
        const auto initialize = reinterpret_cast<InitializeFunction>(m_library.resolve("emucorex_core_initialize"));
        const auto inspectGame = reinterpret_cast<InspectGameFunction>(m_library.resolve("emucorex_core_inspect_game"));
        const auto configure = reinterpret_cast<ConfigureFunction>(m_library.resolve("emucorex_core_configure"));
        const auto setRenderSurface = reinterpret_cast<SetRenderSurfaceFunction>(m_library.resolve("emucorex_core_set_render_surface"));
        const auto startGame = reinterpret_cast<StartGameFunction>(m_library.resolve("emucorex_core_start_game"));
        const auto startBios = reinterpret_cast<StartBiosFunction>(m_library.resolve("emucorex_core_start_bios"));
        const auto state = reinterpret_cast<StateFunction>(m_library.resolve("emucorex_core_state"));
        const auto setPaused = reinterpret_cast<SetPausedFunction>(m_library.resolve("emucorex_core_set_paused"));
        const auto shutdown = reinterpret_cast<ShutdownFunction>(m_library.resolve("emucorex_core_shutdown"));
        const auto saveState = reinterpret_cast<StateSlotFunction>(m_library.resolve("emucorex_core_save_state"));
        const auto loadState = reinterpret_cast<StateSlotFunction>(m_library.resolve("emucorex_core_load_state"));
        const auto lastError = reinterpret_cast<LastErrorFunction>(m_library.resolve("emucorex_core_last_error"));

        if (!abiVersion || !architecture || !initialize || !inspectGame || !configure || !setRenderSurface
            || !startGame || !startBios || !state || !setPaused || !shutdown || !saveState || !loadState
            || !lastError || abiVersion() != EMUCOREX_CORE_ABI_VERSION) {
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

        m_coreDirectory = QFileInfo(path).absolutePath();
        const QByteArray resourcesPath = corePath(QDir(m_coreDirectory).filePath(QStringLiteral("resources")));
        if (!initialize(resourcesPath.constData())) {
            errors.append(QStringLiteral("%1: failed to initialize core resources").arg(path));
            m_library.unload();
            m_coreDirectory.clear();
            m_architecture.clear();
            continue;
        }

        m_inspectGame = inspectGame;
        m_configure = configure;
        m_setRenderSurface = setRenderSurface;
        m_startGame = startGame;
        m_startBios = startBios;
        m_state = state;
        m_setPaused = setPaused;
        m_shutdown = shutdown;
        m_saveState = saveState;
        m_loadState = loadState;
        m_lastError = lastError;
        m_errorString.clear();
        return;
    }
    m_errorString = errors.isEmpty() ? QStringLiteral("EmuCoreX core module was not found") : errors.join(QLatin1Char('\n'));
}
