#pragma once

#include "CommonDef.h"
#include <QWidget>


class PlayerWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PlayerWidget(QWidget *parent = nullptr);

    void paintEvent(QPaintEvent *event) override;

public Q_SLOTS:
    void onFrameChanged(VideoFrame);

private:
    struct Impl;
    Impl *mImpl{};
};
