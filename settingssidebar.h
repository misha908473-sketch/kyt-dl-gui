#pragma once
#include <QtWidgets>

namespace SettingsNavigation {
constexpr int PageRole = Qt::UserRole;
constexpr int GroupRole = Qt::UserRole + 1;
constexpr int SearchRole = Qt::UserRole + 2;

class Delegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        return QSize(210, option.fontMetrics.height() + (index.data(GroupRole).toBool() ? 18 : 20));
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save(); painter->setRenderHint(QPainter::Antialiasing);
        const QRect box = option.rect.adjusted(5, 2, -5, -2);
        if (index.data(GroupRole).toBool()) {
            QFont font = option.font; font.setBold(true); painter->setFont(font);
            QColor groupText = option.palette.color(QPalette::Active, QPalette::Text); groupText.setAlphaF(0.7); painter->setPen(groupText);
            const QString text = index.data(Qt::DisplayRole).toString();
            painter->drawText(box.adjusted(3, 4, 0, 0), Qt::AlignVCenter | Qt::AlignLeading, text);
            const int width = QFontMetrics(font).horizontalAdvance(text);
            painter->setPen(option.palette.color(QPalette::Mid));
            if (option.direction == Qt::RightToLeft) painter->drawLine(box.left(), box.center().y() + 2, box.right() - width - 12, box.center().y() + 2);
            else painter->drawLine(box.left() + width + 12, box.center().y() + 2, box.right(), box.center().y() + 2);
        } else {
            const bool selected = option.state & QStyle::State_Selected;
            if (selected) painter->setBrush(option.palette.highlight());
            else if (option.state & QStyle::State_MouseOver) painter->setBrush(option.palette.alternateBase());
            else painter->setBrush(Qt::NoBrush);
            painter->setPen(Qt::NoPen); painter->drawRoundedRect(box, 5, 5);
            const bool rtl = option.direction == Qt::RightToLeft;
            const int iconSize = 22;
            const QRect iconRect(rtl ? box.right() - iconSize - 8 : box.left() + 8, box.center().y() - iconSize / 2, iconSize, iconSize);
            qvariant_cast<QIcon>(index.data(Qt::DecorationRole)).paint(painter, iconRect);
            painter->setPen(selected ? option.palette.highlightedText().color() : option.palette.text().color());
            painter->setFont(option.font);
            painter->drawText(box.adjusted(rtl ? 22 : 38, 0, rtl ? -38 : -22, 0), Qt::AlignVCenter | Qt::AlignLeading, index.data().toString());
            QStyleOption arrow; arrow.rect = QRect(rtl ? box.left() + 7 : box.right() - 15, box.center().y() - 5, 8, 10);
            arrow.palette = option.palette; arrow.state = QStyle::State_Enabled;
            if (selected) arrow.palette.setColor(QPalette::WindowText, option.palette.highlightedText().color());
            QApplication::style()->drawPrimitive(rtl ? QStyle::PE_IndicatorArrowLeft : QStyle::PE_IndicatorArrowRight, &arrow, painter);
            if (option.state & QStyle::State_HasFocus) {
                QStyleOptionFocusRect focus; focus.rect = box.adjusted(1, 1, -1, -1); focus.palette = option.palette; focus.state = option.state;
                QApplication::style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, painter);
            }
        }
        painter->restore();
    }
};
inline QListWidgetItem *addGroup(QListWidget *list, const QString &title) {
    auto *item = new QListWidgetItem(title, list); item->setFlags(Qt::NoItemFlags);
    item->setData(GroupRole, true); item->setData(PageRole, -1); return item;
}
inline void addPage(QListWidget *list, int page, const QString &title, const QString &icon, const QString &keywords, QStyle::StandardPixmap fallback) {
    auto *item = new QListWidgetItem(QIcon::fromTheme(icon, list->style()->standardIcon(fallback)), title, list);
    item->setData(PageRole, page); item->setData(SearchRole, title + " " + keywords);
}
}
