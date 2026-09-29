#pragma once

#include "PetBridge.h"
#include "PetStateAdapter.h"

#include <QElapsedTimer>
#include <QList>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class AgentSessionController;
class AppSettings;
class DeepSeekBalanceController;

/**
 * 管理可选 Electron 桌宠的运行包校验、进程生命周期和最小宿主路由。
 */
class PetController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
    Q_PROPERTY(int size READ size WRITE setSize NOTIFY settingsChanged)
    Q_PROPERTY(int positionX READ positionX WRITE setPositionX NOTIFY settingsChanged)
    Q_PROPERTY(int positionY READ positionY WRITE setPositionY NOTIFY settingsChanged)
    Q_PROPERTY(bool followWorkStatus READ followWorkStatus WRITE setFollowWorkStatus NOTIFY settingsChanged)
    Q_PROPERTY(bool followBalance READ followBalance WRITE setFollowBalance NOTIFY settingsChanged)
    Q_PROPERTY(QString runtimePath READ runtimePath NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap workStatus READ workStatus NOTIFY workStatusChanged)
    Q_PROPERTY(QVariantMap balanceSnapshot READ balanceSnapshot NOTIFY balanceChanged)

public:
    /** 创建桌宠控制器并绑定 Agent、余额和持久化设置。 */
    PetController(AgentSessionController *agent, DeepSeekBalanceController *balance,
                  AppSettings *settings, QObject *parent = nullptr);

    /** 关闭桌宠并清理由本对象启动的 helper。 */
    ~PetController() override;

    /** 返回用户是否允许启动桌宠。 */
    bool enabled() const;
    /** 保存桌宠启用意愿并按需启动或关闭。 */
    void setEnabled(bool enabled);
    /** 返回 helper 是否存活。 */
    bool running() const;
    /** 返回 helper 是否已经完成至少一次配置桥接。 */
    bool ready() const;
    /** 返回当前生命周期状态。 */
    QString statusText() const;
    /** 返回最近一次不可用或运行故障。 */
    QString errorText() const;
    /** 返回桌宠大小。 */
    int size() const;
    /** 保存桌宠大小并限制在可用范围。 */
    void setSize(int size);
    /** 返回桌宠初始横坐标。 */
    int positionX() const;
    /** 保存桌宠初始横坐标。 */
    void setPositionX(int value);
    /** 返回桌宠初始纵坐标。 */
    int positionY() const;
    /** 保存桌宠初始纵坐标。 */
    void setPositionY(int value);
    /** 返回是否把 Agent 状态同步给桌宠。 */
    bool followWorkStatus() const;
    /** 保存工作状态联动开关。 */
    void setFollowWorkStatus(bool enabled);
    /** 返回是否把余额快照同步给桌宠。 */
    bool followBalance() const;
    /** 保存余额联动开关。 */
    void setFollowBalance(bool enabled);
    /** 返回桌宠运行包目录。 */
    QString runtimePath() const;
    /** 返回状态适配后的工作快照。 */
    QVariantMap workStatus() const;
    /** 返回余额控制器的结构化快照。 */
    QVariantMap balanceSnapshot() const;

    /** 根据当前设置重新检查并启动桌宠。 */
    Q_INVOKABLE void start();
    /** 主动关闭桌宠且不触发自动重启。 */
    Q_INVOKABLE void stop();
    /** 重新启动桌宠，供设置保存或故障恢复使用。 */
    Q_INVOKABLE void restart();

signals:
    /** 启用意愿变化。 */
    void enabledChanged();
    /** helper 运行状态变化。 */
    void runningChanged();
    /** helper 首次完成配置桥接。 */
    void readyChanged();
    /** 生命周期或错误文本变化。 */
    void statusChanged();
    /** 设置值变化。 */
    void settingsChanged();
    /** Agent 工作状态变化。 */
    void workStatusChanged();
    /** 余额快照变化。 */
    void balanceChanged();

private:
    /** 解析 helper 运行包中的程序和参数。 */
    bool resolveRuntime(QString &program, QStringList &arguments, QString &reason) const;
    /** 启动 helper 并注入最小环境变量。 */
    bool launchProcess();
    /** 处理 helper 发来的兼容路由请求。 */
    void routeRequest(const QJsonObject &request);
    /** 构造上游 renderer 所需的聚合配置基线。 */
    QJsonObject buildConfig() const;
    /** 校验资源路径并返回批准目录内的真实文件。 */
    QString resolveAssetPath(const QString &requestPath, QString &contentType) const;
    /** 处理 helper 进程退出并按有界策略安排重启。 */
    void handleProcessFinished(int exitCode, QProcess::ExitStatus status);
    /** 更新状态文本并记录稳定的生命周期原因。 */
    void setStatus(const QString &status, const QString &error = {});
    /** 生成每次启动独立的认证令牌。 */
    QByteArray createToken() const;

    AgentSessionController *m_agent; ///< 当前 Agent，生命周期由 AppController 管理。
    DeepSeekBalanceController *m_balance; ///< 余额事实源，不在桌宠进程中保存凭据。
    AppSettings *m_settings; ///< 桌宠设置事实源。
    PetStateAdapter m_stateAdapter; ///< Agent 状态到桌宠契约的适配器。
    PetBridge m_bridge; ///< JSONL 与 HTTP 回调桥接器。
    QProcess m_process; ///< 当前桌宠 helper，严格由本控制器拥有。
    QTimer m_restartTimer; ///< 异常退出后的有限退避计时器。
    QElapsedTimer m_restartClock; ///< 五分钟重启窗口计时器。
    QList<qint64> m_restartTimes; ///< 最近五分钟的异常重启时间点。
    QByteArray m_token; ///< 当前 helper 代次令牌，仅驻留内存。
    QString m_statusText; ///< 当前生命周期状态。
    QString m_errorText; ///< 最近一次可展示的故障原因。
    QString m_configRequestId; ///< 当前代次配置请求的桥接编号。
    QString m_lastHelperDiagnostic; ///< 最近一次 helper 诊断，避免重复刷屏并保留关键故障上下文。
    quint64 m_generation = 0; ///< helper 进程代次。
    bool m_ready = false; ///< 是否已完成配置路由。
    bool m_stopping = false; ///< 是否由用户或析构主动停止。
    bool m_restartRequested = false; ///< 是否等待当前 helper 退出后重启。
};
