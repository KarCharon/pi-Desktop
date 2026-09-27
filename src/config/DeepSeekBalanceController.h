#pragma once

#include <QObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <functional>

class QNetworkReply;

/** 独立直连官方余额接口；隔离账户、限制重试并产生精确扣减通知。 */
class DeepSeekBalanceController final : public QObject
{
    Q_OBJECT
    /** 当前模型是否允许余额查询。 */
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    /** 当前查询阶段，失败不影响聊天。 */
    Q_PROPERTY(State state READ state NOTIFY changed)
    /** 已脱敏且包含币种图标的展示文本。 */
    Q_PROPERTY(QString displayText READ displayText NOTIFY changed)
    /** 查询原因或成功更新时间。 */
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    /** 仅 Ready 状态下代表有效的账户可用性。 */
    Q_PROPERTY(bool isAvailable READ isAvailable NOTIFY changed)
    /** 本地脱敏诊断日志路径，不包含 Profile 或认证信息。 */
    Q_PROPERTY(QString diagnosticLogPath READ diagnosticLogPath CONSTANT)
public:
    /** 查询状态，禁用时不读取凭据或联网。 */
    enum State {
        Disabled, ///< 非 DeepSeek 或连接尚未确认。
        Loading, ///< 首次请求或有限重试中。
        Ready, ///< 有效余额已取得。
        Error ///< 查询失败，详细原因已脱敏。
    };
    Q_ENUM(State)
    /** 可注入请求工厂，测试不得访问真实网络。 */
    using RequestFactory = std::function<QNetworkReply *(const QNetworkRequest &)>;
    /** 创建直连管理器；测试 factory 默认不落盘，显式目录可用于日志测试。 */
    explicit DeepSeekBalanceController(QObject *parent = nullptr, RequestFactory factory = {},
                                      bool diagnosticsEnabled = true, const QString &diagnosticDirectory = {});
    /** 退出时取消请求，避免回调访问已析构成员。 */
    ~DeepSeekBalanceController() override;
    /** 返回功能启用状态。 */
    bool enabled() const { return m_enabled; }
    /** 返回查询阶段。 */
    State state() const { return m_state; }
    /** 返回主栏文字。 */
    QString displayText() const { return m_text; }
    /** 返回脱敏提示。 */
    QString statusText() const { return m_status; }
    /** 返回上次有效账户可用性；调用者必须同时判断 Ready。 */
    bool isAvailable() const { return m_available; }
    /** 原子更新 Profile 与启用状态；身份变化清除旧余额和动画。 */
    void setContext(const QString &profile, bool enabled);
    /** 合并自动和手动刷新，冷却期间不绕过限制。 */
    Q_INVOKABLE void refresh();
    /** 仅诊断记录模型完成事件，不触发查询；source 仅接受内部事件白名单。 */
    void modelResponseCompleted(bool deepseek, const QString &source);
    /** 返回专用诊断日志位置，禁用日志时为空。 */
    QString diagnosticLogPath() const { return m_logPath; }
    /** QML 上报动画阶段白名单与有界数量，不接受任意文本或密钥。 */
    Q_INVOKABLE void traceAnimation(const QString &stage, int count);
    /** 严格解析有界响应，金额保留原始十进制精度。 */
    static bool parseBalance(const QByteArray &json, QMap<QString, QString> &balances, bool &available);
    /** 精确计算正差额；相等或增加返回空，不使用浮点数。 */
    static QString decrease(const QString &oldValue, const QString &newValue);
    /** 将官方币种转换为显示符号，未知币种保留代码。 */
    static QString currencySymbol(const QString &currency);
signals:
    /** 只读展示属性发生变化。 */
    void changed();
    /** currency 为币种，amount 为正差额，不表示单次请求精确费用。 */
    void balanceDecreased(const QString &currency, const QString &amount);
    /** 清空已有浮字，防止跨账户串用。 */
    void contextReset();
private:
    friend class DeepSeekBalanceTest; ///< 测试可检查周期并模拟到期，不等待真实五分钟。
    /** 写入仅含调用点白名单字段的 JSONL；达到 1MiB 轮转，只保留一份备份。 */
    void trace(const QString &event, QJsonObject fields = {});
    /** 记录来源并复用原有刷新调度，不改变查询和冷却策略。 */
    void requestRefresh(const QString &source);
    /** 开始新任务并读取当前 Profile 密钥，不回退环境变量。 */
    void begin();
    /** 使用任务内密钥发起单次 1000ms 请求。 */
    void attempt();
    /** 完成当前请求并校验代次、响应及重试预算。 */
    void finish(QNetworkReply *reply, quint64 generation);
    /** 结束任务，释放密钥并安排至多一次合并刷新。 */
    void complete();
    /** 更新统一失败文案及脱敏原因。 */
    void fail(const QString &reason);
    /** 取消旧任务及所有定时器，断开旧 reply 回调。 */
    void cancel();
    QString m_logPath; ///< 每个应用实例共享的本地日志文件，进程锁保护轮转。
    QString m_runId; ///< 非敏感运行标识，区分重启及同进程内的控制器。
    QElapsedTimer m_logClock; ///< 日志相对时间，不受系统时钟调整影响。
    QElapsedTimer m_attemptClock; ///< 当前单次请求的实测耗时。
    quint64 m_taskId = 0; ///< 当前刷新任务编号，不包含会话或 Profile 标识。
    quint64 m_eventId = 0; ///< 本实例诊断事件编号，便于关联同步信号。
    bool m_logWarningIssued = false; ///< 日志写入失败时仅警告一次，避免刷屏。
    QNetworkAccessManager m_network; ///< 独立直连管理器，不改变 Pi 代理。
    RequestFactory m_factory; ///< 可控测试请求入口，生产为空。
    QPointer<QNetworkReply> m_reply; ///< 当前唯一在途请求。
    QTimer m_poll; ///< 启用期间每五分钟刷新；上下文失效时停止。
    QTimer m_schedule; ///< 合并事件与冷却等待。
    QTimer m_retry; ///< 同一任务内有限重试。
    QTimer m_deadline; ///< 单次请求总截止时间。
    QString m_profile; ///< 当前 Profile，不包含密钥。
    QByteArray m_key; ///< 仅任务存活期间持有的认证密钥。
    QByteArray m_identity; ///< 仅内存比较的密钥摘要。
    QByteArray m_body; ///< 上限 64KiB 的响应数据。
    QMap<QString, QString> m_balances; ///< 当前身份的上次有效余额。
    QString m_text; ///< 主栏文本。
    QString m_status; ///< 脱敏状态说明。
    State m_state = Disabled; ///< 查询阶段。
    bool m_enabled = false; ///< 当前是否确认 DeepSeek。
    bool m_available = false; ///< 上次有效可用性。
    bool m_active = false; ///< 请求或重试任务是否存在。
    bool m_pending = false; ///< 至多一次尾随刷新。
    bool m_timeout = false; ///< 当前请求是否超过总时限。
    bool m_oversized = false; ///< 响应是否超出上限。
    int m_retries = 0; ///< 当前任务已执行的重试数，上限三次。
    quint64 m_generation = 0; ///< 防止旧响应写回新上下文。
    qint64 m_notBefore = 0; ///< 下一次允许请求的时间戳，保留 429 冷却。
};
