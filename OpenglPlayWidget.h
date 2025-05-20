#pragma once

#include "CommonDef.h"
#include <QOpenGLWidget>
#include <QOpenGLFunctions>

class OpenglPlayWidget : public QOpenGLWidget , protected QOpenGLFunctions{
    Q_OBJECT

public:
    explicit OpenglPlayWidget(QWidget *parent = nullptr);

    QSize sizeHint() const override;
protected:
    void initializeGL() override;

    void resizeGL(int w, int h) override;

    void paintGL() override;

public Q_SLOTS:
    void onFrameChanged(VideoFrame);

private:
    struct Impl;
    Impl *mImpl{};
};
