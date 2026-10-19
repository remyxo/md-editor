#include "TableGridPopup.h"
#include "../app/ThemeManager.h"

#include <QPainter>
#include <QMouseEvent>

namespace {
// 窗口四周阴影预留边（PlatformChrome 挂的 DropShadowEffect 渲染区）
constexpr int kShadowMargin = 8;

// 令牌取色；主题未加载（如独立测试）时回退亮色值
QColor tk(const QString &name, QRgb fallback)
{
    if (ThemeManager::instance()) {
        const QColor c = ThemeManager::instance()->tokenColor(name);
        if (c.isValid())
            return c;
    }
    return QColor(fallback);
}
} // namespace

TableGridPopup::TableGridPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_hoverRow(-1), m_hoverCol(-1), m_dragging(false)
{
    int w = kCols * kCellSize + kPadding * 2;
    int h = kRows * kCellSize + kPadding * 2 + 28;
    setContentsMargins(kShadowMargin, kShadowMargin, kShadowMargin, kShadowMargin);
    setFixedSize(w + 2 * kShadowMargin, h + 2 * kShadowMargin);
    setMouseTracking(true);
    // 半透明窗口：paintEvent 只画内缩圆角面板，四周留阴影空间
    //（背景色与描边色取自主题令牌，暗色不再穿帮）
    setAttribute(Qt::WA_TranslucentBackground);
}

QRect TableGridPopup::contentRect() const
{
    return rect().marginsRemoved(contentsMargins());
}

QRect TableGridPopup::cellRect(int row, int col) const
{
    const QPoint org = contentRect().topLeft();
    return QRect(org.x() + kPadding + col * kCellSize,
                 org.y() + kPadding + row * kCellSize,
                 kCellSize, kCellSize);
}

void TableGridPopup::cellFromPos(const QPoint &pos, int &row, int &col) const
{
    const QPoint p = pos - contentRect().topLeft();
    col = (p.x() - kPadding) / kCellSize;
    row = (p.y() - kPadding) / kCellSize;
    if (p.x() < kPadding) col = -1;
    if (p.y() < kPadding) row = -1;
    col = qBound(-1, col, kCols - 1);
    row = qBound(-1, row, kRows - 1);
}

void TableGridPopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect cr = contentRect();

    // 圆角面板底 + 描边（阴影由 PlatformChrome 的 DropShadowEffect 提供）
    p.setPen(QPen(tk(QStringLiteral("strokeFlyout"), qRgb(0xe5, 0xe5, 0xe5)), 1));
    p.setBrush(tk(QStringLiteral("flyout"), qRgb(0xff, 0xff, 0xff)));
    p.drawRoundedRect(cr, 8, 8);

    QColor borderColor = tk(QStringLiteral("stroke"), qRgb(0xd1, 0xd1, 0xd1));
    QColor idleCell = tk(QStringLiteral("hoverFill"), qRgb(0xf8, 0xf8, 0xf8));
    QColor fillBorder = tk(QStringLiteral("accent"), qRgb(0x00, 0x78, 0xd4));
    QColor fillColor = fillBorder;
    fillColor.setAlpha(70);

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
                p.fillRect(rc, idleCell);
                p.setPen(QPen(borderColor, 1));
            }
            p.drawRect(rc);
        }
    }

    // 底部：m × n
    p.setPen(tk(QStringLiteral("text1"), qRgb(0x32, 0x32, 0x32)));
    QFont f = p.font();
    f.setPixelSize(13);
    p.setFont(f);
    if (m_hoverRow >= 0 && m_hoverCol >= 0) {
        QString text = QString("%1 × %2")
            .arg(m_hoverRow + 1).arg(m_hoverCol + 1);
        p.drawText(QRect(cr.left(), cr.top() + kRows * kCellSize + kPadding + 4,
                         cr.width(), 20), Qt::AlignHCenter | Qt::AlignTop, text);
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
    // 无论是否按下，鼠标移动时即时高亮
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
