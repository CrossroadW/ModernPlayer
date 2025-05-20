#include "PlayerController.h"
#include <spdlog/spdlog.h>
#include "FFmpegWrapper.h"
#include "PlayerWidget.h"
#include <boost/lockfree/spsc_queue.hpp>
#include <future>
Q_DECLARE_METATYPE(VideoFrame);

#define PREFIX  "[PlayerController]"
using namespace std::literals;

namespace {
AVFormatContext *g_format_context;
AVCodecContext *videoCodecContext;
AVCodecContext *audioCodecContext;
int g_videoStream;
int g_audioStream;
std::chrono::time_point<std::chrono::system_clock> g_start_time;
std::chrono::milliseconds g_total_video_time;

std::mutex g_mtx_pause;
std::condition_variable g_cv_pause;
std::atomic<std::chrono::milliseconds> g_pause_time;

std::atomic_bool g_is_paused = false;
std::atomic_bool g_is_seeking = false;
std::atomic_bool g_is_speeding = false;
std::atomic_int g_seek_pos_ms = 0;
int64_t g_audio_pts_begin;
int64_t g_video_pts_begin;

boost::lockfree::spsc_queue<AVPacket *, boost::lockfree::capacity<128>>
g_buffer_video;
boost::lockfree::spsc_queue<AVPacket *, boost::lockfree::capacity<1024>>
g_buffer_audio;
FFmpeg::SwrResample *g_swr{};
AVRational g_audio_pts_base;
std::atomic<std::chrono::time_point<std::chrono::system_clock>>
g_last_pause_point;


void doSeek(int64_t seek_pos_ms) {
    AVStream *audio_stream = g_format_context->streams[g_audioStream];
    double time_base = av_q2d(audio_stream->time_base) * 1000;
    int64_t target_pts = seek_pos_ms / time_base;
    int seek_flags = AVSEEK_FLAG_FRAME;
    if (av_seek_frame(g_format_context, g_audioStream, target_pts, seek_flags) <
        0) {
        spdlog::error(PREFIX ".doSeek seek failed");
    }
}

void startReadPacket(std::stop_token token, PlayerController *controller) {
    AVPacket *packet{};
    while (!token.stop_requested()) {
        if (auto err = FFmpeg::readPaket(g_format_context, packet)) {
            if (err.errorCode == AVERROR_EOF) {
                spdlog::warn("EOF detected, restarting...");

                return;
            }

            spdlog::error("readPaket error");
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            continue;
        }
        if (token.stop_requested()) {
            spdlog::info("stop decode thread");
            break;
        }
        if (packet->stream_index !=
            g_audioStream && packet->stream_index != g_videoStream) {
            spdlog::info("skip packet");
            continue;
        }
        bool isVideo = packet->stream_index == g_videoStream;
        bool isAudio = packet->stream_index == g_audioStream;
        // spdlog::info("push packet");

        while (true) {
            if (token.stop_requested()) {
                spdlog::info("stop decode thread");
                break;
            }
            // spdlog::info("g_is_seeking:{}", g_is_seeking.load());
            if (g_is_seeking.load()) {
                spdlog::info("trigger seeking");
                g_buffer_video.consume_all([](auto) {});
                g_buffer_audio.consume_all([](auto) {});

                using namespace std::chrono;

                int64_t current_ms = duration_cast<milliseconds>(
                    (system_clock::now() - g_pause_time.load() - g_start_time)

                    ).count();

                doSeek(g_seek_pos_ms);

                auto now = system_clock::now();

                spdlog::info("seekoffset :{}", g_seek_pos_ms - current_ms);
                {
                    milliseconds expected = g_pause_time.load();
                    milliseconds desired;
                    do {
                        desired = expected + milliseconds(
                                      current_ms - g_seek_pos_ms.load());
                    } while (!g_pause_time.compare_exchange_weak(
                        expected, desired));
                }
                auto delta = std::chrono::duration_cast<milliseconds>(
                    now - g_last_pause_point.load());
                milliseconds current = g_pause_time.load();
                while (!g_pause_time.
                    compare_exchange_weak(current, current + delta)) {}
                g_is_paused = false;
                g_is_seeking = false;
                g_cv_pause.notify_all();
                spdlog::warn("seeking success");
                break;
            }
            if (isVideo && !g_buffer_video.push(packet)) {
                std::this_thread::sleep_for(std::chrono::microseconds(3));
                continue;
            } else if (isAudio && !g_buffer_audio.push(packet)) {
                std::this_thread::sleep_for(std::chrono::microseconds(3));
                spdlog::info("audio buffer full");
                continue;
            }
            break;
        }
    }
}

uint64_t calDuration() {
    static const AVFormatContext *lastFormatContext = nullptr;
    static int lastVideoStream = -1;
    static double cachedFrameIntervalMs = 0.0;

    if (g_format_context == nullptr || g_videoStream < 0) {
        return 0;
    }

    // 如果格式或视频流编号变化，重新计算
    if (g_format_context != lastFormatContext || g_videoStream !=
        lastVideoStream) {
        AVStream *stream = g_format_context->streams[g_videoStream];
        AVRational avgRate = stream->avg_frame_rate;

        if (avgRate.num != 0 && avgRate.den != 0) {
            cachedFrameIntervalMs = av_q2d(av_inv_q(avgRate)) * 1000.0;
        } else {
            cachedFrameIntervalMs = 0; // 无效帧率
        }

        // 更新缓存状态
        lastFormatContext = g_format_context;
        lastVideoStream = g_videoStream;
    }

    return cachedFrameIntervalMs;
}

void startVideoDecode2(std::stop_token token, PlayerController *controller) {
    while (!token.stop_requested()) {
        AVPacket *packet{};
        if (g_is_seeking) {
            spdlog::info(PREFIX "video decode is seeking");
            avcodec_flush_buffers(videoCodecContext);
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            continue;
        }
        if (!g_buffer_video.pop(packet)) {
            spdlog::info(PREFIX "video buffer empty");
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            continue;
        }
        std::vector<AVFrame *> frames;
        if (FFmpeg::sendPacket2(videoCodecContext, packet, frames).
            hasErr()) {
            spdlog::error(PREFIX "sendPacket2 error");
            continue;
        }

        while (!token.stop_requested() && !frames.empty() && !g_is_seeking.
               load()) {
            AVFrame *frame = frames.back();
            frames.pop_back();

            uint64_t pts = frame->pts;

            uint64_t currentPosMillis = av_q2d(
                                            g_format_context->streams[
                                                g_videoStream]->
                                            time_base)
                                        * pts * 1000;

            using namespace std::chrono;
            // if (g_is_speeding.load()) {
            //     auto duration = milliseconds(calDuration());
            //     auto old_val = g_pause_time.load();
            //     while (!g_pause_time.compare_exchange_weak(
            //         old_val, old_val - duration)) {}
            // }
            auto deadline = g_start_time + milliseconds(
                                currentPosMillis) - milliseconds(
                                g_video_pts_begin);
            spdlog::warn("begin waiting && waiting");
            while (!g_is_seeking && (
                       system_clock::now() - g_pause_time.
                       load())
                   <
                   deadline && !token.stop_requested()) {
                // spdlog::warn(PREFIX "video wait");

                std::this_thread::sleep_for(1ms);
            }
            spdlog::warn("end waiting && invokeMethod ");
            QMetaObject::invokeMethod(controller, "VideoFrameReady",
                                      Qt::DirectConnection,
                                      Q_ARG(VideoFrame, frame));
            {
                std::unique_lock<std::mutex> lock(g_mtx_pause);
                while (g_is_paused && !token.stop_requested() && !
                       g_is_seeking) {
                    spdlog::info(PREFIX "video pause");
                    g_cv_pause.wait(lock);
                }
            }
            if (g_is_seeking) {
                spdlog::info("video break");
                break;
            }
            if (token.stop_requested()) {
                spdlog::info(PREFIX "video break return");
                return;
            }
        }
        av_packet_free(&packet);
    }
}

void startAudioDecode(std::stop_token token, PlayerController *controller) {
    while (!token.stop_requested()) {
        AVPacket *packet{};
        if (g_is_seeking) {
            avcodec_flush_buffers(audioCodecContext);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (!g_buffer_audio.pop(packet)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        // spdlog::info("sendAudioPacket frame");
        std::vector<AVFrame *> frames;
        if (FFmpeg::sendPacket2(audioCodecContext, packet, frames).
            hasErr()) {
            spdlog::error("sendPacket2 error");
            continue;
        }
        while (!token.stop_requested() && !frames.empty()) {
            AVFrame *frame = frames.back();
            frames.pop_back();

            uint64_t pts = frame->pts;

            uint64_t currentPosMillis = av_q2d(
                                            g_format_context->streams[
                                                g_audioStream]->
                                            time_base)
                                        * pts * 1000;

            using namespace std::chrono;

            auto deadline = g_start_time + milliseconds(
                                currentPosMillis) - milliseconds(
                                g_audio_pts_begin);

            while (!g_is_seeking &&
                   (system_clock::now() - g_pause_time.load())
                   <
                   deadline && !token.stop_requested()) {
                std::this_thread::sleep_for(1ms); // 精细等待
            }

            if (FFmpeg::decodeAudio(g_swr, frame, audioCodecContext,
                                    g_is_speeding ? 2.0 : 1.0).
                hasErr()) {
                spdlog::error("decodeAudio error");

                av_frame_free(&frame);
                continue;
            }
            {
                std::unique_lock<std::mutex> lock(g_mtx_pause);
                while (g_is_paused && !token.stop_requested() && !
                       g_is_seeking) {
                    g_cv_pause.wait(lock);
                }
            }

            if (g_is_seeking) {
                spdlog::info("audio break");
                av_frame_free(&frame);
                break;
            }
            av_frame_free(&frame);
        }
        av_packet_free(&packet);
    }
}
}

PlayerController::PlayerController(const PlayerWidget *rendererBridge) {
    qRegisterMetaType<PlayerState>("PlayerState");
    qRegisterMetaType<VideoFrame>("VideoFrame");
    connect(this, &PlayerController::VideoFrameReady,
            rendererBridge,
            qOverload<VideoFrame>(&PlayerWidget::onFrameChanged),
            Qt::DirectConnection);
}

PlayerController::~PlayerController() {
    Close();
}

void PlayerController::Open(const std::string &url) {
    if (mState == PlayerState::Idle) {
        mState = PlayerState::Ready;
        mUrl = url;
        spdlog::info(PREFIX "open url:{}", url);
        FFmpeg::openFile(g_format_context, url, g_audioStream, g_videoStream);
        FFmpeg::openCodec(videoCodecContext, g_videoStream, g_format_context);
        FFmpeg::openCodec(audioCodecContext, g_audioStream, g_format_context);
        spdlog::warn(PREFIX "coded_width: {}",
                     videoCodecContext->coded_width);

        AVStream *stream = g_format_context->streams[g_videoStream];
        AVRational pts_base = stream->time_base;
        int64_t video_ms = stream->duration * av_q2d(pts_base) * 1000;
        g_total_video_time = std::chrono::milliseconds(video_ms);
        spdlog::info(PREFIX "file total len: {}.{}s", video_ms / 1000 / 60,
                     video_ms / 1000 % 60);
        g_audio_pts_begin = g_format_context->streams[g_audioStream]->
            start_time;
        g_video_pts_begin = g_format_context->streams[g_videoStream]->
            start_time;
        spdlog::info(PREFIX "audio pts begin:{}", g_audio_pts_begin);
        spdlog::info(PREFIX "video pts begin:{}", g_video_pts_begin);
        emit StateChanged(mState);
    } else {
        spdlog::warn(PREFIX "player is not idle");
    }
}


void PlayerController::Play() {
    if (mState == PlayerState::Playing) {
        mState = PlayerState::Paused;
        g_last_pause_point = std::chrono::system_clock::now();
        g_is_paused = true;
        emit StateChanged(mState);
        return;
    }
    if (mState == PlayerState::Paused) {
        mState = PlayerState::Playing;
        spdlog::info(PREFIX "start decode thread");

        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - g_last_pause_point.load());

        std::chrono::milliseconds current = g_pause_time.load();
        while (!g_pause_time.compare_exchange_weak(current, current + delta)) {}
        g_is_paused = false;
        g_cv_pause.notify_all();
        emit StateChanged(mState);
        return;
    }
    if (mState == PlayerState::Ready) {
        mState = PlayerState::Playing;
        spdlog::info("start decode thread");

        g_start_time = std::chrono::system_clock::now();
        mReadTask = std::jthread(startReadPacket, this);
        mVideoTask = std::jthread(startVideoDecode2, this);
        mAudioTask = std::jthread(startAudioDecode, this);

        emit StateChanged(mState);
    }
}


void PlayerController::Close() {
    if (mState == PlayerState::Playing || mState == PlayerState::Paused ||
        mState == PlayerState::Ready) {
        mState = PlayerState::Idle;
        if (mReadTask.joinable()) {
            mReadTask.request_stop();
            mReadTask.join();
        }
        g_cv_pause.notify_all();
        if (mVideoTask.joinable()) {
            mVideoTask.request_stop();
            mVideoTask.join();
        }
        if (mAudioTask.joinable()) {
            mAudioTask.request_stop();
            mAudioTask.join();
        }
        if (g_swr) {
            delete g_swr;
            g_swr = nullptr;
        }
        if (videoCodecContext) {
            avcodec_close(videoCodecContext);
            videoCodecContext = nullptr;
        }
        if (audioCodecContext) {
            avcodec_close(audioCodecContext);
            audioCodecContext = nullptr;
        }
        if (g_format_context) {
            avformat_close_input(&g_format_context);
            g_format_context = nullptr;
        }
        g_buffer_video.consume_all([](AVPacket *packet
            ) {
                av_packet_free(&packet);
            });
        g_buffer_audio.consume_all([](AVPacket *packet) {
            av_packet_free(&packet);
        });
        g_pause_time = 0ms;
        g_total_video_time = 0ms;
        g_is_paused = false;
        g_is_seeking = false;
        g_seek_pos_ms = 0;
        g_start_time = std::chrono::time_point<std::chrono::system_clock>();
        g_last_pause_point = std::chrono::system_clock::now();
        g_video_pts_begin = 0;
        g_audio_pts_begin = 0;
        emit StateChanged(mState);
    } else {
        spdlog::warn(
            PREFIX
            "PlayerController::Close player is not playing or paused or ready");
    }
}

void PlayerController::Speed(bool checked) const {
    spdlog::warn(PREFIX "speed not implemented");
    if (mState != PlayerState::Playing) {
        spdlog::warn(PREFIX "player is not playing");
        return;
    }
    if (checked) {
        spdlog::info(PREFIX "speed up");
        g_is_speeding = true;
        g_cv_pause.notify_all();
    } else {
        g_is_speeding = false;
        g_cv_pause.notify_all();
    }
}

void PlayerController::SeekTo(int64_t seek_pos) {
    if (mState == PlayerState::Playing) {
        mState = PlayerState::Seeking;
        g_last_pause_point = std::chrono::system_clock::now();
        g_is_paused = false;
        g_cv_pause.notify_all();
        g_is_seeking = true;
        spdlog::info(PREFIX "seek to {}", seek_pos);
        g_seek_pos_ms = seek_pos;
        mState = PlayerState::Playing;
        emit StateChanged(mState);
        return;
    }

    if (mState == PlayerState::Paused) {
        mState = PlayerState::Seeking;
        g_last_pause_point = std::chrono::system_clock::now();
        g_is_paused = false;
        g_cv_pause.notify_all();
        g_is_seeking = true;
        spdlog::info(PREFIX "seek to {}", seek_pos);
        g_seek_pos_ms = seek_pos;
        mState = PlayerState::Playing;
        emit StateChanged(mState);
        return;
    }
    spdlog::error(" player is not playing or paused");
}


std::pair<int64_t, int64_t> PlayerController::CurrentPosition() const {
    using namespace std::chrono;

    int64_t current_ms = duration_cast<milliseconds>(
        (system_clock::now() - g_pause_time.load() - g_start_time)
        ).count();

    int64_t total_ms = g_total_video_time.count();
    return {current_ms, total_ms};
}
