#include "AppController.h"

#include "agent/AgentSessionController.h"
#include "chat/ChatModel.h"
#include "config/AppSettings.h"
#include "pi/PiProcess.h"
#include "pi/PiRpcClient.h"
#include "session/SessionModel.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>
#include <QUrl>

Q_LOGGING_CATEGORY(appControllerLog, "pidesktop.app")

/**
 * 按 Process → RPC → Controller → Model 的依赖顺序组装应用。
 */
AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_settings(new AppSettings(this))
    , m_chatModel(new ChatModel(this))
    , m_sessionModel(new SessionModel(this))
    , m_projects(new ProjectModel(m_sessionModel, this))
    , m_process(new PiProcess(this))
    , m_rpcClient(new PiRpcClient(m_process, this))
    , m_agent(new AgentSessionController(m_process, m_rpcClient, m_chatModel, this))
{
    connect(m_agent, &AgentSessionController::sessionsChanged,
            m_sessionModel, &SessionModel::refresh);
    connect(m_process, &PiProcess::processExited, this, [this] {
        if (m_restartPending) {
            m_restartPending = false;
            QTimer::singleShot(0, this, &AppController::startPi);
        }
    });

    m_workspaceTimer.setSingleShot(true);
    m_workspaceTimer.setInterval(45000);
    connect(&m_workspaceTimer, &QTimer::timeout, this, [this] {
        m_workspaceError = tr("工作目录切换超时，请检查 Pi 连接后重试。");
        m_workspaceSwitching = false;
        m_restartPending = false;
        m_process->stop();
        emit workspaceNavigationChanged();
        qCWarning(appControllerLog) << "[ProjectWorkspace] 切换超时；cwd=" << m_settings->workspacePath();
    });
    connect(m_agent, &AgentSessionController::connectedChanged, this, [this] {
        if (!m_workspaceSwitching || !m_agent->connected())
            return;
        m_workspaceTimer.stop();
        m_workspaceSwitching = false;
        m_workspaceError.clear();
        m_startupSession = QFileInfo(m_agent->sessionFile()).isFile() ? m_agent->sessionFile() : QString();
        m_process->setSessionFile(m_startupSession);
        emit workspaceNavigationChanged();
        m_sessionModel->refresh();
        qCInfo(appControllerLog) << "[ProjectWorkspace] 切换成功；cwd=" << m_settings->workspacePath()
                                << "session=" << m_agent->sessionFile();
    });
    connect(m_process, &PiProcess::processError, this, [this](const QString &message) {
        if (!m_workspaceSwitching)
            return;
        m_workspaceTimer.stop();
        m_workspaceSwitching = false;
        m_workspaceError = message;
        emit workspaceNavigationChanged();
        qCWarning(appControllerLog) << "[ProjectWorkspace] 切换异常:" << message;
    });
    // 跟随当前会话更新崩溃恢复入口，新会话不会恢复到上一个历史文件。
    connect(m_agent, &AgentSessionController::sessionChanged, this, [this] {
        if (m_workspaceSwitching)
            return;
        // Pi 空会话可能尚未落盘，不能把预留文件名作为下次恢复入口。
        m_startupSession = QFileInfo(m_agent->sessionFile()).isFile() ? m_agent->sessionFile() : QString();
        m_process->setSessionFile(m_startupSession);
    });

    m_sessionModel->setSessionRoot(
        QDir(m_settings->piProfilePath()).filePath(QStringLiteral("sessions")));
    m_sessionModel->refresh();
    QTimer::singleShot(0, this, &AppController::startPi);
}

/**
 * 主动停止进程，避免应用退出时留下 npm/node 子进程。
 */
AppController::~AppController()
{
    m_process->stop();
}

/**
 * 返回由 AppController 持有的聊天模型。
 */
ChatModel *AppController::chatModel() const
{
    return m_chatModel;
}

/**
 * 返回由 AppController 持有的 Session 模型。
 */
SessionModel *AppController::sessionModel() const
{
    return m_sessionModel;
}

/**
 * 返回当前 Session 业务控制器。
 */
AgentSessionController *AppController::agent() const
{
    return m_agent;
}

/**
 * 返回应用设置对象。
 */
AppSettings *AppController::settings() const
{
    return m_settings;
}

/**
 * 接受普通路径或 file URL，保存后重启连接使配置立即生效。
 */
bool AppController::applySettings(const QString &piExecutable, const QString &workspacePath,
                                  const QString &profilePath, const QString &proxyUrl)
{
    QString localProfile = profilePath.trimmed();
    const QUrl profileUrl(localProfile);
    if (profileUrl.isLocalFile())
        localProfile = profileUrl.toLocalFile();
    localProfile = QDir::fromNativeSeparators(localProfile);
    const QFileInfo profileInfo(localProfile);
    m_settingsError.clear();
    if (m_agent->busy() || m_restartPending || m_workspaceSwitching)
        m_settingsError = tr("Pi 正在工作或重启，请先停止任务并等待结束后再保存设置。");
    else if (!AppSettings::isValidProxyUrl(proxyUrl))
        m_settingsError = tr("代理地址须为 http://主机:端口 或 https://主机:端口（不支持 SOCKS）；留空沿用环境。");
    else if (localProfile.isEmpty())
        m_settingsError = tr("请选择 Pi Profile 目录。");
    else if (!QDir::isAbsolutePath(localProfile) || !profileInfo.isDir() || !profileInfo.isWritable())
        m_settingsError = tr("Profile 必须是已存在且可写的绝对目录，不能选择 settings.json 文件。");
    emit settingsErrorChanged();
    if (!m_settingsError.isEmpty()) {
        qCWarning(appControllerLog) << "[ProfileSettings] 保存被拒绝:" << m_settingsError
                                   << "路径:" << localProfile;
        return false;
    }
    QString localExecutable = piExecutable;
    const QUrl executableUrl(piExecutable);
    if (executableUrl.isLocalFile()) {
        localExecutable = executableUrl.toLocalFile();
    }

    QString localWorkspace = workspacePath;
    const QUrl workspaceUrl(workspacePath);
    if (workspaceUrl.isLocalFile()) {
        localWorkspace = workspaceUrl.toLocalFile();
    }
    m_settings->setPiExecutable(localExecutable);
    m_settings->setWorkspacePath(localWorkspace);
    m_settings->setPiProfilePath(localProfile);
    m_settings->setProxyUrl(proxyUrl);
    m_sessionModel->setSessionRoot(QDir(m_settings->piProfilePath()).filePath(QStringLiteral("sessions")));
    m_sessionModel->refresh();
    qCInfo(appControllerLog) << "[ProfileSettings] 已保存，正在切换连接及会话目录；profile="
                            << m_settings->piProfilePath();
    qCInfo(appControllerLog) << "[AppController] 设置已保存，Pi:" << m_settings->piExecutable()
                             << "工作目录:" << m_settings->workspacePath();
    m_startupSession.clear();
    restartPi();
    return true;
}

/** 返回侧栏项目配置模型。 */
ProjectModel *AppController::projects() const { return m_projects; }

/** 返回目录切换锁，防止重复点击导致交叉重启。 */
bool AppController::workspaceSwitching() const { return m_workspaceSwitching; }

/** 返回可在会话栏展示的导航错误。 */
QString AppController::workspaceError() const { return m_workspaceError; }

/** 新增工作文件夹与导航切换共用一个入口，保证添加成功后实际工具目录同步更新。 */
bool AppController::addProjectFolder(const QString &projectId, const QString &path)
{
    m_workspaceError.clear();
    if (m_agent->busy() || m_workspaceSwitching || m_restartPending) {
        m_workspaceError = tr("正在执行任务或切换目录，请稍后再添加工作文件夹。");
        emit workspaceNavigationChanged();
        qCWarning(appControllerLog) << "[ProjectWorkspace] 添加并切换被拒绝；busy=" << m_agent->busy()
                                   << "switching=" << m_workspaceSwitching << "restartPending=" << m_restartPending;
        return false;
    }
    if (!m_projects->addFolder(projectId, path)) {
        m_workspaceError = m_projects->error();
        emit workspaceNavigationChanged();
        qCWarning(appControllerLog) << "[ProjectWorkspace] 添加失败，未切换目录；project=" << projectId
                                   << "path=" << path << "reason=" << m_workspaceError;
        return false;
    }
    const bool started = openWorkspace(path);
    if (started)
        qCInfo(appControllerLog) << "[ProjectWorkspace] 工作文件夹已添加，已请求自动切换；project=" << projectId
                                << "cwd=" << m_settings->workspacePath();
    else
        qCWarning(appControllerLog) << "[ProjectWorkspace] 工作文件夹已添加但自动切换失败；path=" << path
                                   << "reason=" << m_workspaceError;
    return started;
}

/** 校验目录和会话头信息，再以目标目录重新启动 Pi，避免仅更改界面路径。 */
bool AppController::openWorkspace(const QString &path, const QString &sessionFile)
{
    m_workspaceError.clear();
    const QUrl url(path);
    const QString local = QDir::fromNativeSeparators(url.isLocalFile() ? url.toLocalFile() : path.trimmed());
    if (m_agent->busy() || m_workspaceSwitching || m_restartPending)
        m_workspaceError = tr("正在执行任务或切换目录，请稍后再试。");
    else if (local.isEmpty() || !QDir::isAbsolutePath(local) || !QFileInfo(local).isDir())
        m_workspaceError = tr("工作目录不存在或无效：%1").arg(local);
    else if (!sessionFile.isEmpty()) {
        QFile file(sessionFile);
        if (!QDir::isAbsolutePath(sessionFile) || !file.open(QIODevice::ReadOnly)) {
            m_workspaceError = tr("无法读取会话文件：%1").arg(sessionFile);
        } else {
            const auto header = QJsonDocument::fromJson(file.readLine()).object();
            if (header.value("type").toString() != QStringLiteral("session")
                || ProjectModel::pathKey(header.value("cwd").toString()) != ProjectModel::pathKey(local))
                m_workspaceError = tr("会话工作目录与选择的文件夹不一致，已取消切换。");
        }
    }
    emit workspaceNavigationChanged();
    if (!m_workspaceError.isEmpty()) {
        qCWarning(appControllerLog) << "[ProjectWorkspace] 切换被拒绝:" << m_workspaceError;
        return false;
    }
    if (m_agent->connected() && ProjectModel::pathKey(local) == ProjectModel::pathKey(m_settings->workspacePath())
        && (sessionFile.isEmpty() || sessionFile == m_agent->sessionFile())) {
        qCInfo(appControllerLog) << "[ProjectWorkspace] 跳过重启：已在所选目录及会话";
        return true;
    }
    m_workspaceSwitching = true;
    m_startupSession = sessionFile;
    m_settings->setWorkspacePath(QDir::cleanPath(local));
    m_workspaceTimer.start();
    emit workspaceNavigationChanged();
    qCInfo(appControllerLog) << "[ProjectWorkspace] 开始切换；cwd=" << local << "session=" << sessionFile;
    restartPi();
    return true;
}

/**
 * 正在运行时等待退出信号后重启，否则直接启动。
 */
void AppController::restartPi()
{
    if (m_process->active()) {
        const QString session = m_startupSession;
        m_agent->prepareWorkspaceSwitch();
        m_startupSession = session;
        m_restartPending = true;
        m_process->stop();
        qCInfo(appControllerLog) << "[AppController] 已请求重启 Pi";
        return;
    }
    startPi();
}

/**
 * 将持久化配置注入纯进程层并启动 RPC。
 */
void AppController::startPi()
{
    // 在新进程完成状态和历史加载之前，不允许向旧工作目录提交消息。
    const QString session = m_startupSession;
    m_agent->prepareWorkspaceSwitch();
    m_startupSession = session;
    m_process->setSessionFile(session);
    m_process->setExecutable(m_settings->piExecutable());
    m_process->setWorkingDirectory(m_settings->workspacePath());
    m_process->setConfigDirectory(m_settings->piProfilePath());
    m_process->setProxyUrl(m_settings->proxyUrl());
    if (!m_process->start()) {
        qCWarning(appControllerLog) << "[AppController] Pi 启动失败，请检查设置";
    }
}
