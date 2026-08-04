#ifndef MD_EDITOR_TABLEGRIDPOPUP_H
#define MD_EDITOR_TABLEGRIDPOPUP_H

#include <QWidget>

class TableGridPopup : public QWidget {
    Q_OBJECT
public:
    explicit TableGridPopup(QWidget *parent = nullptr);

signals:
    void gridSelected(int rows, int cols);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    static constexpr int kCols = 10;
    static constexpr int kRows = 10;
    static constexpr int kCellSize = 24;
    static constexpr int kPadding = 8;

    int m_hoverRow;
    int m_hoverCol;

    QRect cellRect(int row, int col) const;
};

#endif
