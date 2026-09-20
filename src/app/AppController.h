#pragma once

#include "agent/AgentSessionController.h"
#include "chat/ChatModel.h"
#include "config/AppSettings.h"
#include "session/SessionModel.h"
#include "session/ProjectModel.h"

#include <QObject>
#include <QTimer>

class PiProcess;
class PiRpcClient;

/**
 * 组装应用服务，并向 QML 暴露稳定的模型与控制器入口。
 */
class AppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ChatModel *chatModel READ chatModel CONSTANT)
    Q_PROPERTY(SessionModel *sessionModel READ sessionModel CONSTANT)
    Q_PROPERTY(AgentSessionController *agent READ agent CONSTANT)
    Q_PROPERTY(AppSettings *settings READ settings CONSTANT)
    Q_PROPERTY(QString settingsError MEMBER m_settingsError NOTIFY settingsErrorChanged)
    Q_PROPERTY(ProjectModel *projects READ projects CONSTANT)
    Q_PROPERTY(bool workspaceSwitching READ workspaceSwitching NOTIFY workspaceNavigationChanged)
    Q_PROPERTY(QString workspaceError READ workspaceError NOTIFY workspaceNavigationChanged)

public:
    /**
     * 创建并连接 Pi Desktop 的全部后端组件。
     */
    explicit AppController(QObject *parent = nullptr);

    /**
     * 停止仍在运行的 Pi 子进程。
     */
    ~AppController() override;

    /** 返回聊天列表模型。 */
    [[nodiscard]] ChatModel *chatModel() const;
    /** 返回 Session 列表模型。 */
    [[nodiscard]] SessionModel *sessionModel() const;
    /** 返回当前 Agent 控制器。 */
    [[nodiscard]] AgentSessionController *agent() const;
    /** 返回应用配置。 */
    [[nodiscard]] AppSettings *settings() const;

    /**
     * 保存连接配置并重启 Pi RPC。
     */
    Q_INVOKABLE bool applySettings(const QString &piExecutable, const QString &workspacePath,
                                   const QString &profilePath, const QString &proxyUrl);

    /**
     * 手动重启 Pi RPC 连接。
     */
    Q_INVOKABLE void restartPi();

    /** 返回独立于 Profile 的项目导航配置。 */
    ProjectModel *projects() const;
    /** 返回是否正在等待工作目录切换完成。 */
    bool workspaceSwitching() const;
    /** 返回工作目录切换失败原因。 */
    QString workspaceError() const;
    /** 选择真实工作目录并可选恢复历史会话，重启保证工具目录同步。 */
    Q_INVOKABLE bool openWorkspace(const QString &path, const QString &sessionFile = {});
    /** 新增项目工作文件夹后立即切换实际 cwd；忙碌时拒绝新增以避免状态不一致。 */
    Q_INVOKABLE bool addProjectFolder(const QString &projectId, const QString &path);

signals:
    /** 配置校验结果变化，供设置对话框展示原因。 */
    void settingsErrorChanged();
    /** 工作目录切换进度或失败原因变化。 */
    void workspaceNavigationChanged();

private:
    /**
     * 应用当前配置并启动 Pi 子进程。
     */
    void startPi();

    AppSettings *m_settings;
    ChatModel *m_chatModel;
    SessionModel *m_sessionModel;
    ProjectModel *m_projects;
    PiProcess *m_process;
    PiRpcClient *m_rpcClient;
    AgentSessionController *m_agent;
    bool m_restartPending = false;
    QString m_settingsError;
    QString m_startupSession;
    QString m_workspaceError;
    bool m_workspaceSwitching = false;
    QTimer m_workspaceTimer;
};
