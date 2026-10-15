#ifndef MD_EDITOR_OUTLINETREE_H
#define MD_EDITOR_OUTLINETREE_H

#include <QTreeView>
#include <QStandardItemModel>
#include <QJsonArray>

class OutlineTree : public QTreeView {
    Q_OBJECT
public:
    explicit OutlineTree(QWidget *parent = nullptr);

    // 数据角色：Qt::UserRole 存标题 pos（ProseMirror 文档位置，点击跳转用），
    // RoleHeadingLevel 存标题级别（1-6），RoleIndentLevel 存显示缩进档位
    // （updateHeadings 按「级别 − 文档最高级别」算好存入，delegate 直接取用），
    // RoleHasChildren 存「下级段非空」（紧邻下一行档位更深），RoleCollapsed
    // 存折叠状态（平铺列表上的折叠 = 隐藏其后档位更深的连续段）
    enum DataRole {
        RoleHeadingLevel = Qt::UserRole + 1,
        RoleIndentLevel,
        RoleHasChildren,
        RoleCollapsed
    };

    // 显示缩进档位 = 标题级别 − 文档内最高级别（级别数字最小者）。
    // 统一基准而非按相邻标题相对升降：同级标题恒对齐；文档只有四级标题时
    // 四级项全部对齐最左；中间出现标题3时标题3 到最左、标题4 整体缩进
    // 一档（跳级空档保留，如标题2 下直接跟标题4 时标题4 缩 2 档）
    static int indentLevelForHeading(int level, int minLevel)
    { return qMax(0, level - minLevel); }

    // 语言切换后重设全部文案（Task 10 调用）
    void retranslateUi();

    // 折叠/展开 index 所在标题的下级段（点击大纲行左侧箭头触发；亦可
    // 直接调用，测试用）。折叠 = 隐藏其后档位更深的连续段；展开时尊重
    // 段内更深层已折叠的子段
    void toggleCollapse(const QModelIndex &index);

public slots:
    void updateHeadings(const QJsonArray &headings);
    // 光标位置（ProseMirror 文档 pos）变化时高亮当前所属标题项
    void highlightAtPos(int pos);

signals:
    void headingClicked(int pos);

private slots:
    void onItemClicked(const QModelIndex &index);

protected:
    // 点击箭头槽切换折叠（吞掉不触发跳转），其余区域走默认处理
    void mousePressEvent(QMouseEvent *event) override;

private:
    void applyHighlight();   // 按最近光标位置高亮当前所属标题
    // 行 row 的下级段结束行（不含）：自 row+1 起首个「档位不深于 row」的行；
    // 期间所有行档位必更深（build 时由档位序列保证）
    int subordinateEnd(int row) const;
    // 折叠/展开行 row 的下级段后，重算段内各行显隐（展开时维护生效折叠栈）
    void applySubordinateVisibility(int row, bool collapsed);
    QStandardItemModel *m_model;
    int m_currentPos = -1;   // 最近一次光标位置（ProseMirror 文档 pos，-1 表示尚无）
};

#endif // MD_EDITOR_OUTLINETREE_H
