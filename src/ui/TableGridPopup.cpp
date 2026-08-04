#include "TableGridPopup.h"
#include <QPainter>
#include <QMouseEvent>

TableGridPopup::TableGridPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_hoverRow(-1), m_hoverCol(-1), m_dragging(false)
{
    int w = kCols * kCellSize + kPadding * 2;
    int h = kRows * kCellSize + kPadding * 2 + 28;
    setFixedSize(w, h);
    setMouseTracking(true);
    // 白色背景
    setStyleSheet("background: #ffffff;");
}

QRect TableGridPopup::cellRect(int row, int col) const
{
    return QRect(kPadding + col * kCellSize,
                 kPadding + row * kCellSize,
                 kCellSize, kCellSize);
}

void TableGridPopup::cellFromPos(const QPoint &pos, int &row, int &col) const
{
    col = (pos.x() - kPadding) / kCellSize;
    row = (pos.y() - kPadding) / kCellSize;
    if (pos.x() < kPadding) col = -1;
    if (pos.y() < kPadding) row = -1;
    col = qBound(-1, col, kCols - 1);
    row = qBound(-1, row, kRows - 1);
}

void TableGridPopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 背景
    p.fillRect(rect(), QColor(255, 255, 255));

    QColor borderColor(190, 190, 190);
    QColor fillColor(0x00, 0x78, 0xd4, 70);
    QColor fillBorder(0x00, 0x78, 0xd4);

    // 绘制格子
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            QRect rc = cellRect(r, c).adjusted(1, 1, -1, -1);
            bool hovered = (r <= m_hoverRow && c <= m_hoverCol
                            && m_hoverRow >= 0 && m_hoverCol >= 0);

            if (hovered) {
                p.fillRect(rc, fillColor);
                p.setPen(QPen(fillBorder, 1.5));
            } else {
                p.fillRect(rc, QColor(248, 248, 248));
                p.setPen(QPen(borderColor, 1));
            }
            p.drawRect(rc);
        }
    }

    // 底部：m × n
    p.setPen(QColor(50, 50, 50));
    QFont f = p.font();
    f.setPixelSize(13);
    p.setFont(f);
    if (m_hoverRow >= 0 && m_hoverCol >= 0) {
        QString text = QString("%1 × %2")
            .arg(m_hoverRow + 1).arg(m_hoverCol + 1);
        p.drawText(QRect(0, kRows * kCellSize + kPadding + 4,
                         width(), 20), Qt::AlignHCenter | Qt::AlignTop, text);
    }
}

void TableGridPopup::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        int row, col;
        cellFromPos(event->pos(), row, col);
        if (row >= 0 && col >= 0) {
            m_hoverRow = row;
            m_hoverCol = col;
            update();
        }
    }
}

void TableGridPopup::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) return;
    int row, col;
    cellFromPos(event->pos(), row, col);
    if (row != m_hoverRow || col != m_hoverCol) {
        m_hoverRow = row;
        m_hoverCol = col;
        update();
    }
}

void TableGridPopup::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (m_hoverRow >= 0 && m_hoverCol >= 0) {
            emit gridSelected(m_hoverRow + 1, m_hoverCol + 1);
            close();
        }
    }
}

void TableGridPopup::leaveEvent(QEvent *)
{
    if (!m_dragging) {
        m_hoverRow = -1;
        m_hoverCol = -1;
        update();
    }
}
