#pragma once


#include <vector>
#include <QWidget>
#include <QStyle>
using VideoFrame = struct AVFrame *;
using AudioFrame = std::vector<const char *>;

namespace g {
inline void updateStyle(QWidget *widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}
}
