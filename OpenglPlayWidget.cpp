#include "OpenglPlayWidget.h"

#include <libyuv/convert_argb.h>

extern "C" {
#include <libavutil/frame.h>
}


struct OpenglPlayWidget::Impl {
    std::vector<uint8_t> g_rgbaData;
    int g_width = 0;
    int g_height = 0;
    int g_stride = 0;
    unsigned int textureId = 0;

    QRect
    static scaleKeepAspectRatio(const QRect &outer, int inner_w, int inner_h) {
        // 无效输入检查
        if (inner_w <= 0 || inner_h <= 0)
            return {0, 0, 0, 0};

        const int outer_w = outer.width();
        const int outer_h = outer.height();

        // 整数运算优化（避免浮点除法）
        const bool width_limited = (outer_w * inner_h) < (outer_h * inner_w);
        const int scaled_w =
            width_limited ? outer_w : (inner_w * outer_h / inner_h);
        const int scaled_h =
            width_limited ? (inner_h * outer_w / inner_w) : outer_h;

        // 一次性构造结果矩形（比分别设置x/y/width/height更快）
        return {
            outer.left() + (outer_w - scaled_w) / 2, // 自动居中x
            outer.top() + (outer_h - scaled_h) / 2, // 自动居中y
            scaled_w,
            scaled_h
        };
    }
};

OpenglPlayWidget::OpenglPlayWidget(QWidget *parent): QOpenGLWidget(parent),
    mImpl(new Impl{}) {}

QSize OpenglPlayWidget::sizeHint() const {
    return QOpenGLWidget::sizeHint();
}

void OpenglPlayWidget::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_TEXTURE_2D);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    // 设置清除颜色为黑色
    // 创建纹理
    glGenTextures(1, &mImpl->textureId);
    glBindTexture(GL_TEXTURE_2D, mImpl->textureId);

    // 设置纹理参数
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // 线性滤波
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // 线性放大
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 解绑纹理
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenglPlayWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void OpenglPlayWidget::paintGL() {
    if (mImpl->g_rgbaData.empty())
        return;

    glClear(GL_COLOR_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, mImpl->textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA,
                 mImpl->g_width, mImpl->g_height,
                 0, GL_BGRA, GL_UNSIGNED_BYTE,
                 mImpl->g_rgbaData.data());

    // 设置纹理参数（仅需一次，建议放 initializeGL 中）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 直接绘制纹理四边形
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f); // 左下
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, -1.0f);  // 右下
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);   // 右上
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);  // 左上
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenglPlayWidget::onFrameChanged(VideoFrame frame) {
    const uint8_t *src_y = frame->data[0];
    const uint8_t *src_u = frame->data[1];
    const uint8_t *src_v = frame->data[2];

    int src_stride_y = frame->linesize[0];
    int src_stride_u = frame->linesize[1];
    int src_stride_v = frame->linesize[2];
    // spdlog::warn("onFrameChanged: VideoFrame2");
    mImpl->g_width = frame->width;
    mImpl->g_height = frame->height;

    mImpl->g_rgbaData = std::vector<uint8_t>(
        mImpl->g_width * mImpl->g_height * 4);
    mImpl->g_stride = mImpl->g_width * 4;

    // 调用转换
    libyuv::I420ToARGB(
        src_y, src_stride_y,
        src_u, src_stride_u,
        src_v, src_stride_v,
        mImpl->g_rgbaData.data(), mImpl->g_stride,
        mImpl->g_width, mImpl->g_height
        );
    this->update();
}
