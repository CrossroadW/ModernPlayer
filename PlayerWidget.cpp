#include "PlayerWidget.h"
#include <spdlog/spdlog.h>
#include "PlayerController.h"
#include <QPainter>
#include "libyuv.h"

extern "C" {
#include <libavutil/frame.h>
}


struct PlayerWidget::Impl {
    std::vector<uint8_t> g_rgbaData;
    int g_width = 0;
    int g_height = 0;
    int g_stride = 0;

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

PlayerWidget::PlayerWidget(QWidget *parent): QWidget(parent),
                                             mImpl(new Impl{}) {}


void PlayerWidget::onFrameChanged(VideoFrame frame) {
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


void PlayerWidget::paintEvent(QPaintEvent *event) {
    if (mImpl->g_rgbaData.empty()) {
        return;
    }

    QPainter painter(this);

    const QRect viewRect = rect();

    const QRect dstRect = Impl::scaleKeepAspectRatio(
        viewRect, mImpl->g_width, mImpl->g_height);
    const QImage rgbImage = QImage(
        mImpl->g_rgbaData.data(),
        mImpl->g_width,
        mImpl->g_height,
        mImpl->g_stride,
        QImage::Format_ARGB32
        ).scaled(dstRect.size(), Qt::KeepAspectRatioByExpanding,
                 Qt::FastTransformation);

    painter.drawImage(dstRect, rgbImage);
}
