#include "PiProcess.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

Q_LOGGING_CATEGORY(piProcessLog, "pidesktop.process")

/**
 * 连接 QProcess 的异步信号，并保持所有进程操作位于 UI 线程。
 */
PiProcess::PiProcess(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    /** 隐藏 Node 或 CMD 的控制台，同时保留 QProcess 的标准输入输出管道。 */
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
        arguments->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        arguments->startupInfo->wShowWindow = SW_HIDE;
    });
    qCInfo(piProcessLog) << "[PiProcess] 后台无窗口启动已启用；CREATE_NO_WINDOW=true; RPC管道保留";
#endif
    connect(&m_process, &QProcess::started, this, [this] {
        qCInfo(piProcessLog) << "[PiProcess] Pi RPC 进程启动成功，工作目录:" << m_workingDirectory;
        emit runningChanged();
        emit started();
        // 连续稳定运行一段时间后再恢复崩溃重试额度，避免短周期崩溃形成无限重启。
        QTimer::singleShot(30000, this, [this] {
            if (running()) {
                m_restartAttempts = 0;
            }
        });
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        emit outputReceived(m_process.readAllStandardOutput());
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        const QByteArray chunk = m_process.readAllStandardError();
        m_stderrBuffer.append(chunk);
        if (m_stderrBuffer.size() > 64 * 1024) {
            m_stderrBuffer = m_stderrBuffer.right(64 * 1024);
        }
        // 标准错误可能连续输出，只向上层转发，不在高频回调内逐块写日志。
        emit errorReceived(chunk);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_stopping && error == QProcess::Crashed) {
            // Windows 的 terminate 会报告 Crashed，主动停止不应被展示为运行故障。
            emit runningChanged();
            return;
        }
        const QString message = m_process.errorString();
        qCWarning(piProcessLog) << "[PiProcess] 进程错误，类型:" << error << "详情:" << message;
        emit processError(message);
        emit runningChanged();
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PiProcess::handleFinished);
}

/**
 * 析构阶段同步等待短暂退出，必要时只结束本对象直接拥有的 QProcess。
 */
PiProcess::~PiProcess()
{
    m_stopping = true;
    if (m_process.state() == QProcess::NotRunning) {
        return;
    }

    m_process.blockSignals(true);
    m_process.closeWriteChannel();
    m_process.terminate();
    if (!m_process.waitForFinished(1200)) {
        m_process.kill();
        m_process.waitForFinished(1200);
    }
}

/**
 * 判断 QProcess 是否已经进入运行状态。
 */
bool PiProcess::running() const
{
    return m_process.state() == QProcess::Running;
}

/**
 * 判断进程是否尚未完全退出，供重启流程处理启动中状态。
 */
bool PiProcess::active() const
{
    return m_process.state() != QProcess::NotRunning;
}

/** 返回主动停止标记，避免 Windows terminate 的非零退出码被误报为运行故障。 */
bool PiProcess::stopping() const
{
    return m_stopping;
}

/**
 * 保存用户配置的 Pi 命令路径。
 */
void PiProcess::setExecutable(const QString &executable)
{
    m_executable = executable.trimmed().isEmpty() ? QStringLiteral("pi") : executable.trimmed();
}

/**
 * 保存子进程工作目录；启动时拒绝无效目录，避免误操作其他工作区。
 */
void PiProcess::setWorkingDirectory(const QString &workingDirectory)
{
    m_workingDirectory = workingDirectory;
}

/**
 * 保存独立 Profile 路径，启动时通过环境变量传递给 Pi。
 */
void PiProcess::setConfigDirectory(const QString &configDirectory)
{
    m_configDirectory = QDir::cleanPath(configDirectory);
}

/** 保存启动会话，确保跨目录切换在初始化工具前恢复正确上下文。 */
void PiProcess::setSessionFile(const QString &sessionFile)
{
    m_sessionFile = sessionFile;
}

void PiProcess::setProxyUrl(const QString &proxyUrl)
{
    m_proxyUrl = proxyUrl.trimmed();
}

/**
 * 查找显式路径、PATH 可执行文件以及 Windows npm cmd 包装脚本。
 */
QString PiProcess::resolveExecutable() const
{
    const QFileInfo explicitFile(m_executable);
    if (explicitFile.exists() && explicitFile.isFile()) {
        return explicitFile.absoluteFilePath();
    }

    const QString found = QStandardPaths::findExecutable(m_executable);
    if (!found.isEmpty()) {
        return found;
    }

#ifdef Q_OS_WIN
    const QString pathValue = qEnvironmentVariable("PATH");
    const QStringList pathEntries = pathValue.split(u';', Qt::SkipEmptyParts);
    for (const QString &pathEntry : pathEntries) {
        const QString candidate = QDir(pathEntry).filePath(m_executable + QStringLiteral(".cmd"));
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
#endif

    return {};
}

/**
 * Windows 批处理脚本通过 cmd.exe 启动，其余文件由 QProcess 直接执行。
 */
void PiProcess::configureProcessCommand(const QString &resolvedExecutable)
{
    QStringList arguments{QStringLiteral("--mode"), QStringLiteral("rpc")};
    if (!m_sessionFile.isEmpty())
        arguments << QStringLiteral("--session") << m_sessionFile;
    const QString suffix = QFileInfo(resolvedExecutable).suffix().toLower();
#ifdef Q_OS_WIN
    // QProcess 会复用启动配置；从 CMD 切换回直接执行时不能残留原生命令行。
    m_process.setNativeArguments({});
    if (suffix == QStringLiteral("cmd") || suffix == QStringLiteral("bat")) {
        const QDir npmDirectory = QFileInfo(resolvedExecutable).absoluteDir();
        QString cliPath;
        const QStringList cliCandidates{
            QStringLiteral("node_modules/@earendil-works/pi-coding-agent/dist/cli.js"),
            QStringLiteral("node_modules/@earendil-works/pi-coding-agent/dist/bundle/cli.js")};
        for (const QString &candidate : cliCandidates) {
            const QString path = npmDirectory.filePath(candidate);
            if (QFileInfo(path).isFile()) {
                cliPath = path;
                break;
            }
        }
        QString nodePath = npmDirectory.filePath(QStringLiteral("node.exe"));
        if (!QFileInfo::exists(nodePath)) {
            nodePath = QStandardPaths::findExecutable(QStringLiteral("node"));
        }
        if (!nodePath.isEmpty() && !cliPath.isEmpty()) {
            // 直接启动 Node，确保 QProcess 能可靠终止整个 Pi 进程而非只终止 cmd 包装层。
            m_process.setProgram(nodePath);
            m_process.setArguments(QStringList{cliPath} + arguments);
            return;
        }

        QString command = QStringLiteral("\"")
                          + QDir::toNativeSeparators(resolvedExecutable)
                          + QStringLiteral("\" --mode rpc");
        if (!m_sessionFile.isEmpty())
            command += QStringLiteral(" --session \"") + QDir::toNativeSeparators(m_sessionFile)
                       + QStringLiteral("\"");
        m_process.setProgram(qEnvironmentVariable("COMSPEC", QStringLiteral("cmd.exe")));
        // CMD 不遵循 CRT 的反斜杠转义规则，不能把含引号的命令交给
        // setArguments 自动转义。/S /C 会剥去整条命令最外层的一对引号。
        m_process.setArguments({});
        m_process.setNativeArguments(QStringLiteral("/D /S /C \"") + command + QStringLiteral("\""));
        qCWarning(piProcessLog) << "[PiProcess] 未找到 npm Pi CLI，回退到 cmd 包装层";
        return;
    }
#endif
    m_process.setProgram(resolvedExecutable);
    m_process.setArguments(arguments);
}

/**
 * 校验配置后异步启动 Pi RPC；启动失败原因会通过 processError 返回。
 */
bool PiProcess::start()
{
    if (m_process.state() != QProcess::NotRunning) {
        qCInfo(piProcessLog) << "[PiProcess] 跳过启动：进程正在启动或已经运行";
        return true;
    }

    if (m_configDirectory.isEmpty() || !QFileInfo(m_configDirectory).isDir()) {
        const QString message = tr("Pi Desktop Profile 不存在：%1").arg(m_configDirectory);
        qCWarning(piProcessLog) << "[PiProcess] 配置缺失:" << message;
        emit processError(message);
        return false;
    }

    if (!QFileInfo(m_workingDirectory).isDir()) {
        const QString message = tr("工作目录不存在：%1").arg(m_workingDirectory);
        qCWarning(piProcessLog) << "[ProjectWorkspace] 拒绝启动，不能回退到其他目录:" << message;
        emit processError(message);
        return false;
    }
    if (!m_sessionFile.isEmpty() && !QFileInfo(m_sessionFile).isFile()) {
        const QString message = tr("会话文件不存在：%1").arg(m_sessionFile);
        qCWarning(piProcessLog) << "[ProjectWorkspace] 恢复会话失败:" << message;
        emit processError(message);
        return false;
    }

    const QString resolvedExecutable = resolveExecutable();
    if (resolvedExecutable.isEmpty()) {
        const QString message = tr("找不到 Pi 命令“%1”，请在设置中配置可执行文件。")
                                    .arg(m_executable);
        qCWarning(piProcessLog) << "[PiProcess] 配置无效:" << message;
        emit processError(message);
        return false;
    }

    m_stopping = false;
    m_stderrBuffer.clear();
    configureProcessCommand(resolvedExecutable);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (!m_proxyUrl.isEmpty()) {
        // 同时覆盖大小写，避免 Undici 优先读取继承的 lowercase 旧值。
        // 不修改父进程环境，也不记录可能包含认证信息的代理 URL。
        for (const QString &key : {QStringLiteral("HTTP_PROXY"), QStringLiteral("HTTPS_PROXY"),
                                   QStringLiteral("ALL_PROXY")}) {
            environment.insert(key, m_proxyUrl);
            environment.insert(key.toLower(), m_proxyUrl);
        }
    }
    environment.insert(QStringLiteral("PI_CODING_AGENT_DIR"), m_configDirectory);
    environment.insert(QStringLiteral("PI_CODING_AGENT_SESSION_DIR"),
                       QDir(m_configDirectory).filePath(QStringLiteral("sessions")));
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(m_workingDirectory);
    qCInfo(piProcessLog) << "[ProjectWorkspace] 启动上下文；cwd=" << m_workingDirectory
                        << "session=" << m_sessionFile;

    qCInfo(piProcessLog) << "[PiProcess] 正在启动 Pi RPC，程序:" << resolvedExecutable
                         << "Profile:" << m_configDirectory;
    m_process.start();
    emit runningChanged();
    return true;
}

/**
 * 先温和终止进程，超时后由析构流程保证系统回收。
 */
void PiProcess::stop()
{
    m_stopping = true;
    if (!active()) {
        qCInfo(piProcessLog) << "[PiProcess] 跳过停止：进程未运行";
        return;
    }

    qCInfo(piProcessLog) << "[PiProcess] 正在停止 Pi RPC";
    m_process.closeWriteChannel();
    m_process.terminate();
    QTimer::singleShot(1500, this, [this] {
        if (m_stopping && m_process.state() != QProcess::NotRunning) {
            qCWarning(piProcessLog) << "[PiProcess] 温和停止超时，强制结束 Pi RPC";
            m_process.kill();
        }
    });
}

/**
 * 仅在进程可写时发送数据，避免静默丢失 RPC 命令。
 */
bool PiProcess::write(const QByteArray &data)
{
    if (m_process.state() != QProcess::Running || !m_process.isWritable()) {
        qCWarning(piProcessLog) << "[PiProcess] 写入失败：Pi RPC 未运行或不可写";
        return false;
    }
    return m_process.write(data) == data.size();
}

/**
 * 输出一次退出汇总；非主动异常退出最多自动重启三次。
 */
void PiProcess::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const QString stderrSummary = QString::fromUtf8(m_stderrBuffer.right(1200)).trimmed();
    qCInfo(piProcessLog) << "[PiProcess] 进程退出，退出码:" << exitCode
                         << "状态:" << exitStatus
                         << "stderr 摘要:" << stderrSummary;
    emit runningChanged();
    emit processExited(exitCode);

    if (!m_stopping && exitStatus == QProcess::CrashExit && m_restartAttempts < 3) {
        ++m_restartAttempts;
        qCWarning(piProcessLog) << "[PiProcess] 检测到崩溃，准备自动重启，次数:" << m_restartAttempts;
        QTimer::singleShot(1500, this, [this] { start(); });
    }
}
