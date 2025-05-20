#pragma once

#include <libavutil/frame.h>
#include <soundtouch/SoundTouch.h>
#include <spdlog/spdlog.h>


struct Sound {
    static inline soundtouch::SoundTouch s1{};

    static AVFrame *makeAVFrameFromFloatPCM(const std::vector<float> &pcm,
                                            int sampleRate, int channels) {
        if (channels <= 0 || sampleRate <= 0 || pcm.empty())
            return nullptr;

        int nb_samples = pcm.size() / channels;
        if (nb_samples <= 0)
            return nullptr;

        AVFrame *out = av_frame_alloc();
        if (!out)
            return nullptr;

        out->format = AV_SAMPLE_FMT_FLTP;
        out->channel_layout = av_get_default_channel_layout(channels);
        out->sample_rate = sampleRate;
        out->nb_samples = nb_samples;

        // 为帧分配 buffer
        if (av_frame_get_buffer(out, 0) < 0) {
            av_frame_free(&out);
            return nullptr;
        }

        // 拷贝 interleaved float 数据到 planar 格式
        for (int ch = 0; ch < channels; ++ch) {
            float *dst = reinterpret_cast<float *>(out->data[ch]);
            for (int i = 0; i < nb_samples; ++i) {
                dst[i] = pcm[i * channels + ch];
            }
        }

        return out;
    }

    static
    std::vector<float> processFrame(AVFrame *frame, double tempo) {
        s1.setChannels(frame->channels);
        s1.setRate(frame->sample_rate);
        s1.setTempo(tempo);
        std::vector<float> result;
        int channels = s1.numChannels();
        // 输入样本必须是 float 格式，若不是 float，则必须先转换！
        if (frame->format != AV_SAMPLE_FMT_FLTP && frame->format !=
            AV_SAMPLE_FMT_FLT) {
            spdlog::error("Unsupported sample format");
            return result;
        }

        // 提取 planar 或 packed float PCM
        int nbSamples = frame->nb_samples;
        if (frame->format == AV_SAMPLE_FMT_FLTP) {
            // Planar：每个声道一块数据
            std::vector<float> interleaved(nbSamples * channels);
            for (int ch = 0; ch < channels; ++ch) {
                float *src = reinterpret_cast<float *>(frame->data[ch]);
                for (int i = 0; i < nbSamples; ++i) {
                    interleaved[i * channels + ch] = src[i];
                }
            }
            s1.putSamples(interleaved.data(), nbSamples);
        } else {
            // Packed float
            float *samples = reinterpret_cast<float *>(frame->data[0]);
            s1.putSamples(samples, nbSamples);
        }

        // 从 SoundTouch 取出转换后的样本
        const int BUFF_SIZE = 4096;
        std::vector<float> buffer(BUFF_SIZE);
        int received;

        do {
            received = s1.receiveSamples(
                buffer.data(), BUFF_SIZE / channels);
            result.insert(result.end(), buffer.begin(),
                          buffer.begin() + received * channels);
        } while (received != 0);

        return result;
    }
};
