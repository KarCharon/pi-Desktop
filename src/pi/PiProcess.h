#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

/**
 * 管理 Pi RPC 子进程及其标准输入输出，不解析业务协议。
 */
class PiProcess final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    /**
     * 创建 Pi 进程管理器。
     */
    explicit PiProcess(QObject *parent = nullptr);

    /**
     * 关闭并回收此对象拥有的 Pi 子进程。
     */
    ~PiProcess() override;

    /**
     * 返回子进程当前是否处于运行状态。
     */
    [[nodiscard]] bool running() const;

    /**
     * 返回子进程是否处于启动中或运行中。
     */
    [[nodiscard]] bool active() const;

    /** 返回是否正在主动停止，用于区分配置重启和非预期退出。 */
    [[nodiscard]] bool stopping() const;

    /**
     * 配置 Pi 可执行文件或命令名称。
     */
    void setExecutable(const QString &executable);

    /**
     * 配置 Pi 的工作目录。
     */
    void setWorkingDirectory(const QString &workingDirectory);

    /**
     * 配置 Pi 专用的配置目录。
     */
    void setConfigDirectory(const QString &configDirectory);

    /** 配置启动时恢复的会话，空路径表示在工作目录中新建会话。 */
    void setSessionFile(const QString &sessionFile);

    /** 设置后台代理；空值保留系统环境，非空覆盖 HTTP(S)/ALL_PROXY。 */
    void setProxyUrl(const QString &proxyUrl);

    /**
     * 启动 `pi --mode rpc`。
     */
    bool start();

    /**
     * 请求停止 Pi 子进程。
     */
    void stop();

    /**
     * 向 Pi 标准输入写入原始字节。
     */
    bool write(const QByteArray &data);

signals:
    /** 子进程运行状态发生变化。 */
    void runningChanged();
    /** 收到 Pi 标准输出。 */
    void outputReceived(const QByteArray &data);
    /** 收到 Pi 标准错误。 */
    void errorReceived(const QByteArray &data);
    /** 子进程启动成功。 */
    void started();
    /** 子进程退出。 */
    void processExited(int exitCode);
    /** 子进程发生运行错误。 */
    void processError(const QString &message);

private:
    /**
     * 解析 PATH 中的 Pi 命令，Windows 下同时支持 npm 的 cmd 包装脚本。
     */
    [[nodiscard]] QString resolveExecutable() const;

    /**
     * 根据可执行文件类型配置实际启动命令。
     */
    void configureProcessCommand(const QString &resolvedExecutable);

    /**
     * 汇总处理进程退出，并在崩溃时安排重启。
     */
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);

    QProcess m_process;
    QString m_executable = QStringLiteral("pi");
    QString m_workingDirectory;
    QString m_configDirectory;
    QString m_sessionFile;
    QString m_proxyUrl;
    QByteArray m_stderrBuffer;
    bool m_stopping = false;
    int m_restartAttempts = 0;
};
