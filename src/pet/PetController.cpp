#include "PetController.h"

#include "agent/AgentSessionController.h"
#include "config/AppSettings.h"
#include "config/DeepSeekBalanceController.h"

#include <QCoreApplication>
#include <QDateTime>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QUuid>
#include <QUrl>

/** 将桥接请求编号转为与 PetBridge 相同的比较键。 */
static QString bridgeIdKey(const QJsonValue &id)
{
    if (id.isString())
        return QStringLiteral("s:") + id.toString();
    if (id.isDouble() && qIsFinite(id.toDouble()))
        return QStringLiteral("n:") + QString::number(id.toDouble(), 'g', 17);
    return {};
}

Q_LOGGING_CATEGORY(petLifecycleLog, "pidesktop.pet.lifecycle")
Q_LOGGING_CATEGORY(petAssetsLog, "pidesktop.pet.assets")

namespace {
constexpr int kMaximumRestarts = 3;
constexpr qint64 kRestartWindowMs = 5 * 60 * 1000;
const QString kRoutePrefix = QStringLiteral("/dsh-pet-7340");

/** 返回固定后缀对应的资源媒体类型。 */
QString contentTypeForPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("webm"))
        return QStringLiteral("video/webm");
    if (suffix == QStringLiteral("mov"))
        return QStringLiteral("video/quicktime");
    if (suffix == QStringLiteral("ttf"))
        return QStringLiteral("font/ttf");
    if (suffix == QStringLiteral("woff"))
        return QStringLiteral("font/woff");
    if (suffix == QStringLiteral("woff2"))
        return QStringLiteral("font/woff2");
    if (suffix == QStringLiteral("png"))
        return QStringLiteral("image/png");
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        return QStringLiteral("image/jpeg");
    if (suffix == QStringLiteral("svg"))
        return QStringLiteral("image/svg+xml");
    if (suffix == QStringLiteral("json"))
        return QStringLiteral("application/json");
    if (suffix == QStringLiteral("txt"))
        return QStringLiteral("text/plain");
    return {};
}

/** 判断一个路径是否位于指定规范化目录下。 */
bool isInside(const QString &root, const QString &path)
{
    const QString normalizedRoot = QDir::cleanPath(QDir::fromNativeSeparators(root));
    const QString normalizedPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    const QString rootKey = normalizedRoot.toCaseFolded();
    const QString pathKey = normalizedPath.toCaseFolded();
#else
    const QString rootKey = normalizedRoot;
    const QString pathKey = normalizedPath;
#endif
    return pathKey == rootKey || pathKey.startsWith(rootKey + u'/');
}
}

/** 创建桌宠控制器，并连接设置、状态和余额变化。 */
PetController::PetController(AgentSessionController *agent, DeepSeekBalanceController *balance,
                             AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_agent(agent)
    , m_balance(balance)
    , m_settings(settings)
    , m_stateAdapter(agent, this)
    , m_bridge(this)
    , m_process(this)
{
    Q_ASSERT(m_agent && m_balance && m_settings);
    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout, this, &PetController::start);
    connect(&m_process, &QProcess::started, this, [this] {
        m_stopping = false;
        m_ready = false;
        setStatus(tr("桌宠进程已启动，等待渲染端就绪"));
        emit runningChanged();
        emit readyChanged();
        qCInfo(petLifecycleLog) << "[PetLifecycle] helper 启动成功；generation=" << m_generation;
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_stopping)
            return;
        setStatus(tr("桌宠启动失败"), m_process.errorString());
        qCWarning(petLifecycleLog) << "[PetLifecycle] helper 运行异常；error=" << error
                                   << "reason=" << m_process.errorString();
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        const QString diagnostic = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
        if (diagnostic.isEmpty() || diagnostic == m_lastHelperDiagnostic)
            return;
        m_lastHelperDiagnostic = diagnostic;
        qCWarning(petLifecycleLog) << "[PetLifecycle] helper 诊断输出；message=" << diagnostic.left(2000);
        if (diagnostic.contains(QStringLiteral("bridge file read failed"), Qt::CaseInsensitive)) {
            setStatus(tr("桌宠素材读取失败"), tr("Electron 无法读取 Qt 返回的动画文件。"));
        } else if (diagnostic.contains(QStringLiteral("page load failed"), Qt::CaseInsensitive)) {
            setStatus(tr("桌宠页面加载失败"), tr("Electron 渲染页面加载失败。"));
        }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PetController::handleProcessFinished);
    connect(&m_bridge, &PetBridge::requestReceived, this, &PetController::routeRequest);
    connect(&m_bridge, &PetBridge::responseCompleted, this,
            [this](const QString &id, bool success) {
        if (success && id == m_configRequestId && !m_ready) {
            m_ready = true;
            setStatus(tr("桌宠已就绪"));
            emit readyChanged();
            qCInfo(petLifecycleLog) << "[PetLifecycle] 配置回调成功，桌宠已就绪";
        }
    });
    connect(&m_bridge, &PetBridge::fatalError, this, [this](const QString &message) {
        setStatus(tr("桌宠桥接协议错误"), message);
        qCWarning(petLifecycleLog) << "[PetBridge] 协议错误，停止桌宠；reason=" << message;
        stop();
    });
    connect(&m_bridge, &PetBridge::diagnostic, this, [this](const QString &message) {
        qCWarning(petLifecycleLog) << "[PetBridge]" << message;
    });
    connect(&m_stateAdapter, &PetStateAdapter::snapshotChanged, this, [this] {
        emit workStatusChanged();
    });
    connect(m_balance, &DeepSeekBalanceController::changed,
            this, &PetController::balanceChanged);
    connect(m_settings, &AppSettings::petEnabledChanged, this, [this] {
        emit enabledChanged();
        if (enabled())
            start();
        else
            stop();
    });
    connect(m_settings, &AppSettings::petSizeChanged, this, &PetController::settingsChanged);
    connect(m_settings, &AppSettings::petPositionChanged, this, &PetController::settingsChanged);
    connect(m_settings, &AppSettings::petFollowWorkStatusChanged, this, &PetController::settingsChanged);
    connect(m_settings, &AppSettings::petFollowBalanceChanged, this, &PetController::settingsChanged);
    connect(m_settings, &AppSettings::petRuntimePathChanged, this, [this] {
        emit settingsChanged();
        if (enabled())
            restart();
    });
    if (enabled())
        QTimer::singleShot(0, this, &PetController::start);
    else
        setStatus(tr("桌宠未启用"));
}

/** 主动回收 helper，必要时等待短暂退出。 */
PetController::~PetController()
{
    m_stopping = true;
    m_restartTimer.stop();
    m_bridge.detach();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(1200)) {
            m_process.kill();
            m_process.waitForFinished(1200);
        }
    }
}

/** 返回持久化启用意愿。 */
bool PetController::enabled() const { return m_settings->petEnabled(); }

/** 保存启用意愿，实际生命周期由设置变化连接统一驱动。 */
void PetController::setEnabled(bool enabled)
{
    m_settings->setPetEnabled(enabled);
}

/** 返回 helper 是否仍在运行。 */
bool PetController::running() const
{
    return m_process.state() != QProcess::NotRunning;
}

/** 返回 helper 是否完成过配置请求。 */
bool PetController::ready() const { return m_ready; }

/** 返回桌宠生命周期状态。 */
QString PetController::statusText() const { return m_statusText; }

/** 返回最近一次可展示故障。 */
QString PetController::errorText() const { return m_errorText; }

/** 返回桌宠大小。 */
int PetController::size() const { return m_settings->petSize(); }

/** 保存桌宠大小。 */
void PetController::setSize(int size)
{
    m_settings->setPetSize(size);
}

/** 返回桌宠横坐标。 */
int PetController::positionX() const { return m_settings->petPositionX(); }

/** 保存桌宠横坐标。 */
void PetController::setPositionX(int value)
{
    m_settings->setPetPositionX(value);
}

/** 返回桌宠纵坐标。 */
int PetController::positionY() const { return m_settings->petPositionY(); }

/** 保存桌宠纵坐标。 */
void PetController::setPositionY(int value)
{
    m_settings->setPetPositionY(value);
}

/** 返回工作状态联动设置。 */
bool PetController::followWorkStatus() const { return m_settings->petFollowWorkStatus(); }

/** 保存工作状态联动设置。 */
void PetController::setFollowWorkStatus(bool enabled)
{
    m_settings->setPetFollowWorkStatus(enabled);
}

/** 返回余额联动设置。 */
bool PetController::followBalance() const { return m_settings->petFollowBalance(); }

/** 保存余额联动设置。 */
void PetController::setFollowBalance(bool enabled)
{
    m_settings->setPetFollowBalance(enabled);
}

/** 返回可选运行包目录，默认位于程序同级 pet 目录。 */
QString PetController::runtimePath() const
{
    return m_settings->petRuntimePath();
}

/** 返回状态适配器快照；关闭联动时仍保持真实状态供调试。 */
QVariantMap PetController::workStatus() const
{
    QVariantMap result = m_stateAdapter.snapshot();
    result.insert(QStringLiteral("enabled"), followWorkStatus());
    return result;
}

/** 返回余额控制器快照；关闭联动时仍不暴露认证信息。 */
QVariantMap PetController::balanceSnapshot() const
{
    QVariantMap result = m_balance->snapshot();
    result.insert(QStringLiteral("enabled"), followBalance());
    return result;
}

/** 检查运行包并启动独立 helper。 */
void PetController::start()
{
    if (!enabled()) {
        setStatus(tr("桌宠未启用"));
        qCInfo(petLifecycleLog) << "[PetLifecycle] 跳过启动；reason=disabled";
        return;
    }
    if (running()) {
        qCInfo(petLifecycleLog) << "[PetLifecycle] 跳过启动；reason=already-running";
        return;
    }
    QString program;
    QStringList arguments;
    QString reason;
    if (!resolveRuntime(program, arguments, reason)) {
        setStatus(tr("桌宠不可用"), reason);
        qCWarning(petLifecycleLog) << "[PetAssets] 运行包校验失败；reason=" << reason
                                   << "path=" << runtimePath();
        return;
    }
    launchProcess();
}

/** 主动停止 helper，不安排异常重启。 */
void PetController::stop()
{
    m_restartRequested = false;
    m_restartTimer.stop();
    m_stopping = true;
    m_ready = false;
    m_configRequestId.clear();
    m_bridge.detach();
    emit readyChanged();
    if (m_process.state() == QProcess::NotRunning) {
        setStatus(enabled() ? tr("桌宠已关闭") : tr("桌宠未启用"));
        emit runningChanged();
        qCInfo(petLifecycleLog) << "[PetLifecycle] 正常关闭；reason=not-running";
        return;
    }
    setStatus(tr("正在关闭桌宠"));
    m_process.terminate();
    QTimer::singleShot(1500, this, [this] {
        if (m_stopping && m_process.state() != QProcess::NotRunning) {
            qCWarning(petLifecycleLog) << "[PetLifecycle] 优雅退出超时，强制结束 helper";
            m_process.kill();
        }
    });
}

/** 关闭当前 helper 后重新启动，设置变化不会影响 Pi 连接。 */
void PetController::restart()
{
    if (!enabled()) {
        stop();
        return;
    }
    if (running()) {
        stop();
        // stop() 清除主动关闭标记后再恢复重启意图，等待 finished 信号统一启动。
        m_restartRequested = true;
    } else {
        start();
    }
}

/** 解析完整运行包，支持独立 helper 可执行文件或 Electron + helper 脚本。 */
bool PetController::resolveRuntime(QString &program, QStringList &arguments, QString &reason) const
{
    const QDir root(runtimePath());
    if (!root.isAbsolute() || !QFileInfo(root.absolutePath()).isDir()) {
        reason = tr("桌宠运行包目录不存在。");
        return false;
    }
    QFile manifest(root.filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 2 * 1024 * 1024) {
        reason = tr("桌宠运行包缺少有效 manifest.json。");
        return false;
    }
    const QJsonObject manifestObject = QJsonDocument::fromJson(manifest.readAll()).object();
    const QJsonArray manifestFiles = manifestObject.value(QStringLiteral("files")).toArray();
    if (manifestObject.value(QStringLiteral("version")).toString().isEmpty() || manifestFiles.isEmpty()) {
        reason = tr("桌宠运行包 manifest.json 格式无效。");
        return false;
    }
    for (const QJsonValue &entry : manifestFiles) {
        const QString relative = entry.toString();
        if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(QStringLiteral(".."))
            || relative.contains(u'\\') || relative.contains(u':')) {
            reason = tr("桌宠运行包 manifest.json 包含非法文件路径。");
            return false;
        }
        const QFileInfo fileInfo(root.filePath(relative));
        if (!fileInfo.isFile()) {
            reason = tr("桌宠运行包缺少文件：%1").arg(relative);
            return false;
        }
    }
    if (!manifestObject.value(QStringLiteral("sha256")).isObject()) {
        reason = tr("桌宠运行包 manifest.json 缺少文件校验信息。");
        return false;
    }
    const QStringList standalone{
#ifdef Q_OS_WIN
        root.filePath(QStringLiteral("helper/pet-helper.exe")),
        root.filePath(QStringLiteral("helper/electron-helper.exe")),
#else
        root.filePath(QStringLiteral("helper/pet-helper")),
        root.filePath(QStringLiteral("helper/electron-helper")),
#endif
    };
    for (const QString &candidate : standalone) {
        if (QFileInfo(candidate).isFile() && QFileInfo(candidate).isExecutable()) {
            program = candidate;
            return true;
        }
    }
#ifdef Q_OS_WIN
    const QString electron = root.filePath(QStringLiteral("runtime/electron.exe"));
#else
    const QString electron = root.filePath(QStringLiteral("runtime/electron"));
#endif
    const QString helper = root.filePath(QStringLiteral("helper/main.js"));
    if (QFileInfo(electron).isFile() && QFileInfo(helper).isFile()) {
        program = electron;
        arguments = {helper};
        return true;
    }
    reason = tr("桌宠运行包缺少 helper 或 Electron 运行时。");
    return false;
}

/** 注入隔离环境并启动当前 helper 代次。 */
bool PetController::launchProcess()
{
    QString program;
    QStringList arguments;
    QString reason;
    if (!resolveRuntime(program, arguments, reason))
        return false;
    m_stopping = false;
    m_ready = false;
    m_configRequestId.clear();
    ++m_generation;
    m_token = createToken();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const QString &key : {QStringLiteral("ELECTRON_RUN_AS_NODE"), QStringLiteral("PI_API_KEY"),
                               QStringLiteral("PI_CODING_AGENT_DIR"), QStringLiteral("PI_CODING_AGENT_SESSION_DIR"),
                               QStringLiteral("DEEPSEEK_API_KEY"), QStringLiteral("OPENAI_API_KEY"),
                               QStringLiteral("ANTHROPIC_API_KEY"), QStringLiteral("GOOGLE_API_KEY"),
                               QStringLiteral("AWS_ACCESS_KEY_ID"), QStringLiteral("AWS_SECRET_ACCESS_KEY")}) {
        environment.remove(key);
        environment.remove(key.toLower());
    }
    environment.insert(QStringLiteral("DSH_PET_BRIDGE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("DSH_PET_HOST_PID"), QString::number(QCoreApplication::applicationPid()));
    const QJsonObject runtimeConfig = buildConfig();
    const QJsonObject mainConfig = runtimeConfig.value(QStringLiteral("main")).toObject();
    const QJsonObject petConfig = mainConfig.value(QStringLiteral("pets")).toArray().at(0).toObject();
    environment.insert(QStringLiteral("DSH_PET_PETS"),
                       QString::fromUtf8(QJsonDocument(QJsonArray{
                           QJsonObject{{QStringLiteral("id"), petConfig.value(QStringLiteral("id")).toString()},
                                       {QStringLiteral("size"), petConfig.value(QStringLiteral("size")).toInt(size())}}
                       }).toJson(QJsonDocument::Compact)));
    environment.insert(QStringLiteral("DSH_PET_BRIDGE_TOKEN"), QString::fromLatin1(m_token.toBase64()));
    // helper 只在 bridge 模式下使用该 URL 的 origin 校验，仍需传入合法绝对 URL。
    environment.insert(QStringLiteral("DSH_PET_CONFIG_URL"),
                       QStringLiteral("http://127.0.0.1") + kRoutePrefix + QStringLiteral("/config"));
    // 每个 Pi Desktop 进程使用独立 Electron userData，避免多开实例被 Chromium 单实例锁互相影响。
    environment.insert(QStringLiteral("DSH_PET_USER_DATA"),
                       QDir(runtimePath()).filePath(QStringLiteral("user-data/%1")
                           .arg(QCoreApplication::applicationPid())));
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(runtimePath());
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
        arguments->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        arguments->startupInfo->wShowWindow = SW_HIDE;
    });
#endif
    m_bridge.attach(&m_process, m_generation, m_token);
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.start();
    qCInfo(petLifecycleLog) << "[PetLifecycle] 正在启动 helper；generation=" << m_generation;
    return true;
}

/** 仅路由固定前缀和实现过的只读接口。 */
void PetController::routeRequest(const QJsonObject &request)
{
    const QString method = request.value(QStringLiteral("method")).toString();
    const QUrl url(request.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
    const QString path = url.path();
    if (!url.isValid() || url.hasFragment() || !path.startsWith(kRoutePrefix)) {
        m_bridge.respondError(request, 404, QStringLiteral("unknown_route"), QStringLiteral("route unavailable"));
        return;
    }
    const QString route = path.mid(kRoutePrefix.size());
    if (method == QStringLiteral("GET") && route == QStringLiteral("/config")) {
        const QJsonObject config = buildConfig();
        if (config.isEmpty()) {
            m_bridge.respondError(request, 500, QStringLiteral("config_unavailable"),
                                  QStringLiteral("pet configuration unavailable"));
            return;
        }
        m_configRequestId = bridgeIdKey(request.value(QStringLiteral("id")));
        m_bridge.respondJson(request, 200, config);
        return;
    }
    if (method == QStringLiteral("GET") && route == QStringLiteral("/ready")) {
        if (!m_ready) {
            m_ready = true;
            setStatus(tr("桌宠已就绪"));
            emit readyChanged();
            qCInfo(petLifecycleLog) << "[PetLifecycle] renderer 就绪回调已确认";
        }
        m_bridge.respondJson(request, 200, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (method == QStringLiteral("GET") && route == QStringLiteral("/work-status")) {
        const QVariantMap source = workStatus();
        const QString sourceState = source.value(QStringLiteral("state")).toString();
        QString state;
        if (sourceState == QStringLiteral("thinking") || sourceState == QStringLiteral("waiting")
            || sourceState == QStringLiteral("success") || sourceState == QStringLiteral("error")) {
            state = sourceState;
        } else if (sourceState == QStringLiteral("working") || sourceState == QStringLiteral("compacting")) {
            state = QStringLiteral("working");
        }
        QJsonObject snapshot{
            {QStringLiteral("state"), state.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(state)},
            {QStringLiteral("task"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("ts"), static_cast<qint64>(source.value(QStringLiteral("sequence")).toULongLong())}
        };
        m_bridge.respondJson(request, 200, snapshot);
        return;
    }
    if (method == QStringLiteral("GET") && route == QStringLiteral("/balance")) {
        const QVariantMap source = m_balance->snapshot();
        const bool available = source.value(QStringLiteral("available")).toBool();
        const QVariantMap balances = source.value(QStringLiteral("balances")).toMap();
        if (available && !balances.isEmpty()) {
            const QString currency = balances.constBegin().key();
            const QString total = balances.constBegin().value().toString();
            m_bridge.respondJson(request, 200, QJsonObject{
                {QStringLiteral("ok"), true},
                {QStringLiteral("provider"), QStringLiteral("deepseek-official")},
                {QStringLiteral("kind"), QStringLiteral("deepseek")},
                {QStringLiteral("data"), QJsonObject{
                    {QStringLiteral("currency"), currency}, {QStringLiteral("total"), total}
                }}
            });
        } else {
            const QString sourceReason = source.value(QStringLiteral("reason")).toString();
            const QString reason = sourceReason == QStringLiteral("provider_unsupported")
                ? QStringLiteral("unsupported")
                : sourceReason == QStringLiteral("credentials_missing")
                    || sourceReason == QStringLiteral("credentials_invalid")
                    || sourceReason == QStringLiteral("credentials_rejected")
                    ? QStringLiteral("credential-missing") : QStringLiteral("fetch-error");
            m_bridge.respondJson(request, 200, QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("provider"), QStringLiteral("deepseek-official")},
                {QStringLiteral("reason"), reason},
                {QStringLiteral("message"), source.value(QStringLiteral("status")).toString()}
            });
        }
        return;
    }
    if (method == QStringLiteral("GET") && route == QStringLiteral("/balance/trigger")) {
        // 上游该接口是命令触发计数的只读轮询；Qt 侧没有独立命令计数，保持 0 可避免每秒重复刷新余额。
        m_bridge.respondJson(request, 200, QJsonObject{{QStringLiteral("count"), 0}});
        return;
    }
    if (method == QStringLiteral("GET") && route == QStringLiteral("/broadcast")) {
        m_bridge.respondJson(request, 200, QJsonObject{{QStringLiteral("ok"), true},
            {QStringLiteral("text"), QString()}, {QStringLiteral("ts"), 0}});
        return;
    }
    if (method == QStringLiteral("GET") && route == QStringLiteral("/notify")) {
        m_bridge.respondJson(request, 200, QJsonObject{
            {QStringLiteral("ok"), true}, {QStringLiteral("seq"), 0},
            {QStringLiteral("frames"), QJsonArray{}}
        });
        return;
    }
    if ((route.startsWith(QStringLiteral("/thumb/")) || route.startsWith(QStringLiteral("/font/"))
         || route.startsWith(QStringLiteral("/pic/"))) && method == QStringLiteral("GET")) {
        QString contentType;
        const QString filePath = resolveAssetPath(path, contentType);
        if (!filePath.isEmpty()) {
            m_bridge.respondFile(request, 200, contentType, filePath);
            return;
        }
        m_bridge.respondError(request, 404, QStringLiteral("asset_missing"), QStringLiteral("asset unavailable"));
        return;
    }
    if (route == QStringLiteral("/whisper") || route == QStringLiteral("/whisper/trigger")
        || route == QStringLiteral("/chat")) {
        m_bridge.respondError(request, 404, QStringLiteral("unsupported"), QStringLiteral("chat is disabled"));
        return;
    }
    m_bridge.respondError(request, 404, QStringLiteral("unknown_route"), QStringLiteral("route unavailable"));
    qCInfo(petLifecycleLog) << "[PetBridge] 未知路由已拒绝；route=" << route;
}

/** 读取上游聚合配置并覆盖 Qt 侧的桌宠展示设置。 */
QJsonObject PetController::buildConfig() const
{
    const QString configPath = QDir(runtimePath()).filePath(QStringLiteral("assets/config.json"));
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(petAssetsLog) << "[PetAssets] 配置文件缺失；path=" << configPath;
        return {};
    }
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        qCWarning(petAssetsLog) << "[PetAssets] 配置文件无效；error=" << parseError.errorString();
        return {};
    }
    QJsonObject result = document.object();
    QJsonObject main = result.value(QStringLiteral("main")).toObject();
    QJsonArray pets = main.value(QStringLiteral("pets")).toArray();
    if (pets.isEmpty()) {
        qCWarning(petAssetsLog) << "[PetAssets] 配置缺少 main.pets";
        return {};
    }
    QJsonObject pet = pets.first().toObject();
    pet.insert(QStringLiteral("display"), QStringLiteral("desktop"));
    pet.insert(QStringLiteral("size"), size());
    pet.insert(QStringLiteral("balanceEnabled"), followBalance());
    pet.insert(QStringLiteral("workStatusEnabled"), followWorkStatus());
    QJsonObject position = pet.value(QStringLiteral("position")).toObject();
    position.insert(QStringLiteral("corner"), QStringLiteral("top-left"));
    position.insert(QStringLiteral("marginX"), qMax(0, positionX()));
    position.insert(QStringLiteral("marginY"), qMax(0, positionY()));
    pet.insert(QStringLiteral("position"), position);
    pets = QJsonArray{pet};
    main.insert(QStringLiteral("pets"), pets);
    result.insert(QStringLiteral("main"), main);
    return result;
}

/** 解析上游 thumb/font/pic 路由并拒绝目录穿越或符号链接越界。 */
QString PetController::resolveAssetPath(const QString &requestPath, QString &contentType) const
{
    QString routeRoot;
    QString subdirectory;
    if (requestPath.startsWith(kRoutePrefix + QStringLiteral("/thumb/"))) {
        routeRoot = kRoutePrefix + QStringLiteral("/thumb/");
        subdirectory = QStringLiteral("webm");
    } else if (requestPath.startsWith(kRoutePrefix + QStringLiteral("/font/"))) {
        routeRoot = kRoutePrefix + QStringLiteral("/font/");
        subdirectory = QStringLiteral("fonts");
    } else if (requestPath.startsWith(kRoutePrefix + QStringLiteral("/pic/"))) {
        routeRoot = kRoutePrefix + QStringLiteral("/pic/");
        subdirectory = QStringLiteral("pic");
    } else {
        return {};
    }
    QString relative = QUrl::fromPercentEncoding(requestPath.mid(routeRoot.size()).toUtf8());
    // thumb 请求的第一段是素材归属根（通常为 main），包内动画文件本身位于 assets/webm。
    if (subdirectory == QStringLiteral("webm")) {
        const int slash = relative.indexOf(u'/');
        if (slash < 1 || slash == relative.size() - 1)
            return {};
        relative = relative.mid(slash + 1);
    } else if (subdirectory == QStringLiteral("pic") && relative.startsWith(QStringLiteral("memes/"))) {
        subdirectory = QStringLiteral("memes");
        relative = relative.mid(QStringLiteral("memes/").size());
    }
    if (relative.isEmpty() || relative.contains(u'\\') || relative.startsWith(u'/')
        || relative.contains(QStringLiteral("..")) || relative.contains(u':')) {
        qCWarning(petAssetsLog) << "[PetAssets] 资源路径被拒绝；reason=unsafe-relative-path";
        return {};
    }
    contentType = contentTypeForPath(relative);
    if (contentType.isEmpty()) {
        qCWarning(petAssetsLog) << "[PetAssets] 资源后缀被拒绝；path=" << relative;
        return {};
    }
    const QFileInfo rootInfo(QDir(runtimePath()).filePath(QStringLiteral("assets/%1").arg(subdirectory)));
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    const QFileInfo fileInfo(QDir(canonicalRoot).filePath(relative));
    const QString canonicalFile = fileInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonicalFile.isEmpty() || !fileInfo.isFile()
        || !isInside(canonicalRoot, canonicalFile)) {
        qCWarning(petAssetsLog) << "[PetAssets] 资源缺失或越界；relative=" << relative;
        return {};
    }
    return canonicalFile;
}

/** 处理主动关闭、异常退出和有限自动重启。 */
void PetController::handleProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_bridge.detach();
    m_token.fill('\0');
    m_token.clear();
    m_configRequestId.clear();
    const bool abnormal = !m_stopping
        && (status == QProcess::CrashExit || exitCode != 0);
    const bool restart = m_restartRequested;
    m_restartRequested = false;
    m_ready = false;
    emit runningChanged();
    emit readyChanged();
    if (restart && enabled()) {
        setStatus(tr("正在重启桌宠"));
        QTimer::singleShot(0, this, &PetController::start);
        return;
    }
    if (!abnormal || !enabled()) {
        setStatus(enabled() ? tr("桌宠已关闭") : tr("桌宠未启用"));
        qCInfo(petLifecycleLog) << "[PetLifecycle] helper 已关闭；exitCode=" << exitCode
                                << "abnormal=" << abnormal;
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (!m_restartTimes.isEmpty() && now - m_restartTimes.front() > kRestartWindowMs)
        m_restartTimes.removeFirst();
    if (m_restartTimes.size() >= kMaximumRestarts) {
        setStatus(tr("桌宠已停止"), tr("桌宠连续异常退出，已暂停自动重启。"));
        qCWarning(petLifecycleLog) << "[PetLifecycle] 达到自动重启上限；count=" << m_restartTimes.size();
        return;
    }
    m_restartTimes.append(now);
    const int delay = m_restartTimes.size() == 1 ? 1000 : m_restartTimes.size() == 2 ? 3000 : 10000;
    setStatus(tr("桌宠异常退出，准备重启"), tr("将在 %1 秒后重试。").arg(delay / 1000));
    m_restartTimer.start(delay);
    qCWarning(petLifecycleLog) << "[PetLifecycle] 安排有限重启；attempt=" << m_restartTimes.size()
                               << "delayMs=" << delay;
}

/** 更新生命周期文本，区分关闭、缺失、协议错误和正常就绪。 */
void PetController::setStatus(const QString &status, const QString &error)
{
    if (m_statusText == status && m_errorText == error)
        return;
    m_statusText = status;
    m_errorText = error;
    emit statusChanged();
}

/** 生成不可预测且不写入日志的进程代次令牌。 */
QByteArray PetController::createToken() const
{
    return QUuid::createUuid().toRfc4122().toBase64(QByteArray::Base64UrlEncoding);
}
