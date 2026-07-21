#include <jni.h>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "../../common/src/main/cpp/gl.h"

namespace {

std::mutex gl_mutex;
bool gl_ready = false;

std::string jstring_to_string(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string result(chars ? chars : "");
    if (chars) env->ReleaseStringUTFChars(value, chars);
    return result;
}

std::string ffmpeg_error(int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error, buffer, sizeof(buffer));
    return buffer;
}

void throw_java(JNIEnv* env, const std::string& message) {
    jclass exception = env->FindClass("java/lang/RuntimeException");
    if (exception) env->ThrowNew(exception, message.c_str());
}

class Video {
public:
    Video(const std::string& path, GLuint texture) : texture(texture) {
        int result = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
        if (result < 0) fail("open media: " + ffmpeg_error(result));
        result = avformat_find_stream_info(format, nullptr);
        if (result < 0) fail("read media info: " + ffmpeg_error(result));
        stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (stream < 0) fail("video stream not found");

        const AVCodecParameters* parameters = format->streams[stream]->codecpar;
        const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
        if (!decoder) fail("video codec not found");
        codec = avcodec_alloc_context3(decoder);
        if (!codec) fail("unable to allocate video codec");
        result = avcodec_parameters_to_context(codec, parameters);
        if (result < 0) fail("copy video codec parameters: " + ffmpeg_error(result));
        result = avcodec_open2(codec, decoder, nullptr);
        if (result < 0) fail("open video codec: " + ffmpeg_error(result));
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        rgba = av_frame_alloc();
        if (!packet || !frame || !rgba) fail("allocate video frames");

        AVRational rate = format->streams[stream]->avg_frame_rate;
        frame_rate = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 0.0;
        if (frame_rate <= 0.0) frame_rate = 30.0;
    }

    ~Video() {
        if (sws) sws_freeContext(sws);
        av_frame_free(&rgba);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }

    double frame_rate_value() const { return frame_rate; }

    bool decode() {
        for (;;) {
            int result = av_read_frame(format, packet);
            if (result == AVERROR_EOF) {
                avcodec_send_packet(codec, nullptr);
            } else if (result < 0) {
                fail("read video packet: " + ffmpeg_error(result));
            } else {
                if (packet->stream_index != stream) {
                    av_packet_unref(packet);
                    continue;
                }
                result = avcodec_send_packet(codec, packet);
                av_packet_unref(packet);
                if (result < 0 && result != AVERROR(EAGAIN))
                    fail("send video packet: " + ffmpeg_error(result));
            }

            while (true) {
                result = avcodec_receive_frame(codec, frame);
                if (result == AVERROR(EAGAIN)) break;
                if (result == AVERROR_EOF) return false;
                if (result < 0) fail("decode video frame: " + ffmpeg_error(result));
                upload(frame);
                av_frame_unref(frame);
                return true;
            }
            if (result == AVERROR_EOF) return false;
        }
    }

private:
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgba = nullptr;
    SwsContext* sws = nullptr;
    int stream = -1;
    GLuint texture = 0;
    int texture_width = 0;
    int texture_height = 0;
    double frame_rate = 30.0;

    [[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

    void upload(const AVFrame* source) {
        sws = sws_getCachedContext(sws, source->width, source->height,
            static_cast<AVPixelFormat>(source->format), source->width, source->height,
            AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) fail("create FFmpeg scaler");
        rgba->format = AV_PIX_FMT_RGBA;
        rgba->width = source->width;
        rgba->height = source->height;
        if (av_frame_get_buffer(rgba, 1) < 0) fail("allocate RGBA frame");
        sws_scale(sws, source->data, source->linesize, 0, source->height,
                  rgba->data, rgba->linesize);

        glBindTexture(GL_TEXTURE_2D, texture);
        if (texture_width != source->width || texture_height != source->height) {
            texture_width = source->width;
            texture_height = source->height;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture_width, texture_height,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->linesize[0] / 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture_width, texture_height,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba->data[0]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        av_frame_unref(rgba);
    }
};

class Audio {
public:
    explicit Audio(const std::string& path) {
        int result = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
        if (result < 0) fail("open audio: " + ffmpeg_error(result));
        result = avformat_find_stream_info(format, nullptr);
        if (result < 0) fail("read audio info: " + ffmpeg_error(result));
        stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (stream < 0) fail("audio stream not found");
        const AVCodecParameters* parameters = format->streams[stream]->codecpar;
        const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
        if (!decoder) fail("audio codec not found");
        codec = avcodec_alloc_context3(decoder);
        if (!codec) fail("allocate audio codec");
        if (avcodec_parameters_to_context(codec, parameters) < 0) fail("copy audio codec parameters");
        if (avcodec_open2(codec, decoder, nullptr) < 0) fail("open audio codec");
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!packet || !frame) fail("allocate audio frames");
    }

    ~Audio() {
        swr_free(&resampler);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }

    std::vector<uint8_t> decode(bool mono) {
        AVChannelLayout output_layout{};
        output_channels = mono ? 1 : 2;
        av_channel_layout_default(&output_layout, output_channels);
        int result = swr_alloc_set_opts2(&resampler, &output_layout, AV_SAMPLE_FMT_S16, 44100,
            &codec->ch_layout, codec->sample_fmt, codec->sample_rate, 0, nullptr);
        if (result < 0 || !resampler || swr_init(resampler) < 0) fail("initialize audio resampler");

        std::vector<uint8_t> output;
        while ((result = av_read_frame(format, packet)) >= 0) {
            if (packet->stream_index == stream) append_packet(output, packet);
            av_packet_unref(packet);
        }
        avcodec_send_packet(codec, nullptr);
        append_packet(output, nullptr);
        av_channel_layout_uninit(&output_layout);
        return output;
    }

private:
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* resampler = nullptr;
    int stream = -1;
    int output_channels = 2;

    [[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

    void append_packet(std::vector<uint8_t>& output, AVPacket* input) {
        int result = input ? avcodec_send_packet(codec, input) : 0;
        if (result < 0 && result != AVERROR(EAGAIN)) fail("send audio packet: " + ffmpeg_error(result));
        while ((result = avcodec_receive_frame(codec, frame)) >= 0) {
            int samples = av_rescale_rnd(swr_get_delay(resampler, codec->sample_rate) + frame->nb_samples,
                                         44100, codec->sample_rate, AV_ROUND_UP);
            int channels = output_channels;
            std::vector<uint8_t> buffer(static_cast<size_t>(samples) * channels * 2);
            uint8_t* destination[] = {buffer.data()};
            int converted = swr_convert(resampler, destination, samples,
                                        const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
            if (converted > 0) output.insert(output.end(), buffer.begin(),
                buffer.begin() + static_cast<size_t>(converted) * channels * 2);
            av_frame_unref(frame);
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF && result < 0)
            fail("decode audio frame: " + ffmpeg_error(result));
    }
};

void JNICALL media_init(JNIEnv* env, jclass, jlong proc) {
    std::lock_guard lock(gl_mutex);
    if (gl_ready) return;
    if (!proc || !gladLoadGL(reinterpret_cast<GLADloadfunc>(proc))) {
        throw_java(env, "MediaPlayer: unable to load OpenGL functions");
        return;
    }
    gl_ready = true;
}

void JNICALL video_decode(JNIEnv* env, jobject object) {
    jclass cls = env->GetObjectClass(object);
    jfieldID field = env->GetFieldID(cls, "ptr", "J");
    auto* video = reinterpret_cast<Video*>(env->GetLongField(object, field));
    try { video->decode(); } catch (const std::exception& e) { throw_java(env, e.what()); }
}

jlong JNICALL video_open(JNIEnv* env, jobject object, jstring path, jint texture, jint) {
    try {
        auto* video = new Video(jstring_to_string(env, path), static_cast<GLuint>(texture));
        jclass cls = env->GetObjectClass(object);
        env->SetDoubleField(object, env->GetFieldID(cls, "frameRate", "D"), video->frame_rate_value());
        return reinterpret_cast<jlong>(video);
    } catch (const std::exception& e) { throw_java(env, e.what()); return 0; }
}

void JNICALL video_release(JNIEnv*, jclass, jlong pointer) { delete reinterpret_cast<Video*>(pointer); }

jlong JNICALL audio_open(JNIEnv* env, jobject, jstring path) {
    try { return reinterpret_cast<jlong>(new Audio(jstring_to_string(env, path))); }
    catch (const std::exception& e) { throw_java(env, e.what()); return 0; }
}

jobject JNICALL audio_decode(JNIEnv* env, jobject object, jboolean mono) {
    try {
        jclass cls = env->GetObjectClass(object);
        auto* audio = reinterpret_cast<Audio*>(env->GetLongField(object, env->GetFieldID(cls, "ptr", "J")));
        auto data = audio->decode(mono == JNI_TRUE);
        void* memory = malloc(data.size());
        if (!memory) throw std::bad_alloc();
        std::copy(data.begin(), data.end(), static_cast<uint8_t*>(memory));
        jobject buffer = env->NewDirectByteBuffer(memory, static_cast<jlong>(data.size()));
        jclass audio_class = env->FindClass("net/hacker/mediaplayer/Audio");
        jmethodID constructor = env->GetMethodID(audio_class, "<init>", "(Ljava/nio/ByteBuffer;I)V");
        return env->NewObject(audio_class, constructor, buffer, mono ? 1 : 2);
    } catch (const std::exception& e) { throw_java(env, e.what()); return nullptr; }
}

void JNICALL audio_release(JNIEnv*, jclass, jlong pointer) { delete reinterpret_cast<Audio*>(pointer); }

} // namespace

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8) != JNI_OK) return JNI_ERR;
    JNINativeMethod media_methods[] = {{const_cast<char*>("init"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(media_init)}};
    JNINativeMethod video_methods[] = {
        {const_cast<char*>("open"), const_cast<char*>("(Ljava/lang/String;II)J"), reinterpret_cast<void*>(video_open)},
        {const_cast<char*>("decode"), const_cast<char*>("()V"), reinterpret_cast<void*>(video_decode)},
        {const_cast<char*>("release"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(video_release)}};
    JNINativeMethod audio_methods[] = {
        {const_cast<char*>("open"), const_cast<char*>("(Ljava/lang/String;)J"), reinterpret_cast<void*>(audio_open)},
        {const_cast<char*>("decode"), const_cast<char*>("(Z)Lnet/hacker/mediaplayer/Audio;"), reinterpret_cast<void*>(audio_decode)},
        {const_cast<char*>("release"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(audio_release)}};
    jclass media = env->FindClass("net/hacker/mediaplayer/MediaPlayer");
    jclass video = env->FindClass("net/hacker/mediaplayer/VideoDecoder");
    jclass audio = env->FindClass("net/hacker/mediaplayer/AudioDecoder");
    if (!media || !video || !audio || env->RegisterNatives(media, media_methods, 1) < 0 ||
        env->RegisterNatives(video, video_methods, 3) < 0 || env->RegisterNatives(audio, audio_methods, 3) < 0)
        return JNI_ERR;
    av_log_set_level(AV_LOG_QUIET);
    return JNI_VERSION_1_8;
}
