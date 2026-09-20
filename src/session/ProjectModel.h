#pragma once

#include <QObject>
#include <QVariantList>

class SessionModel;

/** 独立保存项目和工作文件夹，并按会话的真实工作目录生成侧栏行。 */
class ProjectModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    /** 加载应用级项目配置，订阅历史会话更新。 */
    explicit ProjectModel(SessionModel *sessions, QObject *parent = nullptr);
    /** 返回项目、文件夹和会话的扁平导航树。 */
    QVariantList rows() const;
    /** 返回最近一次操作的失败原因。 */
    QString error() const;
    /** 新建或重命名项目；空编号表示新建。 */
    Q_INVOKABLE bool saveProject(const QString &id, const QString &name);
    /** 将真实目录加入项目，名称为空时采用目录名。 */
    Q_INVOKABLE bool addFolder(const QString &projectId, const QString &path, const QString &name = {});
    /** 修改工作文件夹显示别名，不修改磁盘目录。 */
    Q_INVOKABLE bool renameFolder(const QString &id, const QString &name);
    /** 仅移除导航配置，保留全部磁盘目录和会话文件。 */
    Q_INVOKABLE bool removeEntry(const QString &id);
    /** 统一绝对路径、分隔符和 Windows 大小写，供目录匹配使用。 */
    static QString pathKey(const QString &path);
signals:
    /** 导航树内容发生变化。 */
    void rowsChanged();
    /** 操作错误发生变化。 */
    void errorChanged();
private:
    /** 原子提交配置到应用设置并重建导航行。 */
    bool persist(const QVariantList &projects);
    /** 记录并展示配置或输入失败原因。 */
    bool fail(const QString &message);
    /** 合并项目配置与会话元数据，保留未分组历史。 */
    void rebuild();
    SessionModel *m_sessions;
    QVariantList m_projects;
    QVariantList m_rows;
    QString m_error;
};
