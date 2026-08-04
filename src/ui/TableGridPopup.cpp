#include "TableGridPopup.h"
#include <QPainter>
#include <QMouseEvent>
#include <QApplication>

TableGridPopup::TableGridPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_hoverRow(-1), m_hoverCol(-1)
{
    int w = kCols * kCellSize + kPadding * 2;
    int h = kRows * kCellSize + kPadding * 2 + 24;
    setFixedSize(w, h);
    setMouseTracking(true);
}

QRect TableGridPopup::cellRect(int row, int col) const
{
    return QRect(kPadding + col * kCellSize,
                 kPadding + row * kCellSize,
                 kCellSize, kCellSize);
}

void TableGridPopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor borderColor(180, 180, 180);
    QColor fillColor(0x00, 0x78, 0xd4, 80);    // 半透明蓝
    QColor fillBorder(0x00, 0x78, 0xd4);

    // 绘制所有格子
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            QRect rc = cellRect(r, c).adjusted(1, 1, -1, -1);
            bool hovered = (r <= m_hoverRow && c <= m_hoverCol
                            && m_hoverRow >= 0 && m_hoverCol >= 0);

            if (hovered) {
                p.fillRect(rc, fillColor);
                p.setPen(QPen(fillBorder, 1.5));
            } else {
                p.fillRect(rc, QColor(250, 250, 250));
                p.setPen(QPen(borderColor, 1));
            }
            p.drawRect(rc);
        }
    }

    // 底部文字：m × n
    if (m_hoverRow >= 0 && m_hoverCol >= 0) {
        p.setPen(QColor(80, 80, 80));
        p.setFont(QFont("Sans Serif", 10));
        QString text = QString("%1 × %2")
            .arg(m_hoverRow + 1).arg(m_hoverCol + 1);
        p.drawText(QRect(0, kRows * kCellSize + kPadding,
                         width(), 20), Qt::AlignCenter, text);
    }
}

void TableGridPopup::mouseMoveEvent(QMouseEvent *event)
{
    QPoint pos = event->pos();
    int col = (pos.x() - kPadding) / kCellSize;
    int row = (pos.y() - kPadding) / kCellSize;
    col = qBound(0, col, kCols - 1);
    row = qBound(0, row, kRows - 1);

    if (pos.x() < kPadding) col = -1;
    if (pos.y() < kPadding) row = -1;

    if (row != m_hoverRow || col != m_hoverCol) {
        m_hoverRow = row;
        m_hoverCol = col;
        update();
    }
}

void TableGridPopup::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton
        && m_hoverRow >= 0 && m_hoverCol >= 0) {
        emit gridSelected(m_hoverRow + 1, m_hoverCol + 1);
        close();
    }
}

void TableGridPopup::leaveEvent(QEvent *)
{
    m_hoverRow = -1;
    m_hoverCol = -1;
    update();
}
