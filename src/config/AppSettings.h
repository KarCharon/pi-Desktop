#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>

/**
 * 使用 QSettings 持久化 Pi Desktop 的本地连接配置。
 */
class AppSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString piExecutable READ piExecutable WRITE setPiExecutable NOTIFY piExecutableChanged)
    Q_PROPERTY(QString workspacePath READ workspacePath WRITE setWorkspacePath NOTIFY workspacePathChanged)
    Q_PROPERTY(QString piProfilePath READ piProfilePath WRITE setPiProfilePath NOTIFY piProfilePathChanged)
    Q_PROPERTY(QString proxyUrl READ proxyUrl WRITE setProxyUrl NOTIFY proxyUrlChanged)
    Q_PROPERTY(QStringList profileDirectories READ profileDirectories NOTIFY discoveryChanged)
    Q_PROPERTY(QStringList executablePaths READ executablePaths NOTIFY discoveryChanged)
    Q_PROPERTY(QString discoverySummary READ discoverySummary NOTIFY discoveryChanged)
    Q_PROPERTY(QString discoveryError READ discoveryError NOTIFY discoveryErrorChanged)

public:
    /**
     * 创建配置对象并加载默认值。
     */
    explicit AppSettings(QObject *parent = nullptr);

    /**
     * 返回 Pi 命令或可执行文件路径。
     */
    [[nodiscard]] QString piExecutable() const;

    /**
     * 保存 Pi 命令或可执行文件路径。
     */
    void setPiExecutable(const QString &value);

    /**
     * 返回 Pi 子进程工作目录。
     */
    [[nodiscard]] QString workspacePath() const;

    /**
     * 保存 Pi 子进程工作目录。
     */
    void setWorkspacePath(const QString &value);

    /**
     * 返回已保存的 Pi Profile 路径，未配置时使用 Desktop 目录。
     */
    [[nodiscard]] QString piProfilePath() const;

    /** 保存规范化后的 Profile 绝对路径。 */
    void setPiProfilePath(const QString &value);

    /** 空代理地址继承环境；仅用于桌面端启动的后台 Pi。 */
    QString proxyUrl() const;
    void setProxyUrl(const QString &value);
    static bool isValidProxyUrl(const QString &value);

    /** 返回自动发现的 Profile 目录列表。 */
    QStringList profileDirectories() const;
    /** 返回自动发现的 Pi 程序路径列表。 */
    QStringList executablePaths() const;
    /** 返回本次扫描结果摘要，供设置界面显示。 */
    QString discoverySummary() const;
    /** 重新扫描标准目录、环境变量和 PATH，不修改当前配置。 */
    Q_INVOKABLE void refreshDiscovery();
    /** 将候选从下拉列表永久隐藏，不删除磁盘数据或修改当前连接。 */
    Q_INVOKABLE bool removeDiscoveryEntry(const QString &kind, const QString &path);
    /** 返回候选删除失败原因。 */
    QString discoveryError() const;
    /** 根据显式根路径发现 Profile，便于在临时目录中测试。 */
    static QStringList discoverProfiles(const QString &home, const QString &environmentDirectory,
                                        const QString &currentProfile, int *skipped = nullptr);

signals:
    void proxyUrlChanged();
    /** 自动发现结果已更新。 */
    void discoveryChanged();
    /** 候选删除错误状态已更新。 */
    void discoveryErrorChanged();
    /** Profile 路径配置发生变化。 */
    void piProfilePathChanged();
    /** Pi 命令配置发生变化。 */
    void piExecutableChanged();
    /** 工作目录配置发生变化。 */
    void workspacePathChanged();

private:
    QSettings m_settings;
    QStringList m_profileDirectories;
    QStringList m_executablePaths;
    QString m_discoverySummary;
    QString m_discoveryError;
};
