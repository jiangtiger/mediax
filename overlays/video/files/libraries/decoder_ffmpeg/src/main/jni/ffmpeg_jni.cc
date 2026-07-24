/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdlib.h>
#include <thread>

extern "C" {
#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
#ifdef _STDINT_H
#undef _STDINT_H
#endif
#include <stdint.h>
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#define LOG_TAG "ffmpeg_jni"
#define LOGE(...) \
  ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))
#define LOGD(...) \
  ((void)__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__))

#define LIBRARY_FUNC(RETURN_TYPE, NAME, ...)                               \
  extern "C" {                                                             \
  JNIEXPORT RETURN_TYPE                                                    \
  Java_androidx_media3_decoder_ffmpeg_FfmpegLibrary_##NAME(JNIEnv* env,    \
                                                           jobject thiz,   \
                                                           ##__VA_ARGS__); \
  }                                                                        \
  JNIEXPORT RETURN_TYPE                                                    \
  Java_androidx_media3_decoder_ffmpeg_FfmpegLibrary_##NAME(                \
      JNIEnv* env, jobject thiz, ##__VA_ARGS__)

#define AUDIO_DECODER_FUNC(RETURN_TYPE, NAME, ...)               \
  extern "C" {                                                   \
  JNIEXPORT RETURN_TYPE                                          \
  Java_androidx_media3_decoder_ffmpeg_FfmpegAudioDecoder_##NAME( \
      JNIEnv* env, jobject thiz, ##__VA_ARGS__);                 \
  }                                                              \
  JNIEXPORT RETURN_TYPE                                          \
  Java_androidx_media3_decoder_ffmpeg_FfmpegAudioDecoder_##NAME( \
      JNIEnv* env, jobject thiz, ##__VA_ARGS__)

#define VIDEO_DECODER_FUNC(RETURN_TYPE, NAME, ...)                       \
  extern "C" {                                                           \
  JNIEXPORT RETURN_TYPE                                                  \
  Java_androidx_media3_decoder_ffmpeg_ExperimentalFfmpegVideoDecoder_##NAME( \
      JNIEnv* env, jobject thiz, ##__VA_ARGS__);                         \
  }                                                                      \
  JNIEXPORT RETURN_TYPE                                                  \
  Java_androidx_media3_decoder_ffmpeg_ExperimentalFfmpegVideoDecoder_##NAME( \
      JNIEnv* env, jobject thiz, ##__VA_ARGS__)

#define ERROR_STRING_BUFFER_LENGTH 256

// ============================================================================
// Audio decoder constants and forward declarations (kept intact)
// ============================================================================

// Output format corresponding to AudioFormat.ENCODING_PCM_16BIT.
static const AVSampleFormat OUTPUT_FORMAT_PCM_16BIT = AV_SAMPLE_FMT_S16;
// Output format corresponding to AudioFormat.ENCODING_PCM_FLOAT.
static const AVSampleFormat OUTPUT_FORMAT_PCM_FLOAT = AV_SAMPLE_FMT_FLT;

// LINT.IfChange
static const int AUDIO_DECODER_ERROR_INVALID_DATA = -1;
static const int AUDIO_DECODER_ERROR_OTHER = -2;
// LINT.ThenChange(../java/androidx/media3/decoder/ffmpeg/FfmpegAudioDecoder.java)

static jmethodID growOutputBufferMethod;

// ============================================================================
// Video decoder constants and struct
// ============================================================================

// LINT.IfChange
static const int VIDEO_DECODER_ERROR_INVALID_DATA = -1;
static const int VIDEO_DECODER_ERROR_OTHER = -2;
// LINT.ThenChange(../java/androidx/media3/decoder/ffmpeg/ExperimentalFfmpegVideoDecoder.java)

struct JniContext {
  AVCodecContext* codecContext;
  SwsContext* swsContext;
  AVFrame* currentFrame;
};

// ============================================================================
// Audio decoder forward declarations (kept intact)
// ============================================================================

/**
 * Returns the AVCodec with the specified name, or NULL if it is not available.
 */
const AVCodec* getCodecByName(JNIEnv* env, jstring codecName);

/**
 * Allocates and opens a new AVCodecContext for the specified codec, passing the
 * provided extraData as initialization data for the decoder if it is non-NULL.
 * Returns the created context.
 */
AVCodecContext* createContext(JNIEnv* env, const AVCodec* codec,
                              jbyteArray extraData, jboolean outputFloat,
                              jint rawSampleRate, jint rawChannelCount);

struct GrowOutputBufferCallback {
  uint8_t* operator()(int requiredSize) const;

  JNIEnv* env;
  jobject thiz;
  jobject decoderOutputBuffer;
};

/**
 * Decodes the packet into the output buffer, returning the number of bytes
 * written, or a negative AUDIO_DECODER_ERROR constant value in the case of an
 * error.
 */
int decodePacket(AVCodecContext* context, AVPacket* packet,
                 uint8_t* outputBuffer, int outputSize,
                 GrowOutputBufferCallback growBuffer);

/**
 * Transforms ffmpeg AVERROR into a negative AUDIO_DECODER_ERROR constant value.
 */
int transformError(int errorNumber);

/**
 * Outputs a log message describing the avcodec error number.
 */
void logError(const char* functionName, int errorNumber);

/**
 * Releases the specified context.
 */
void releaseContext(AVCodecContext* context);

// ============================================================================
// JNI_OnLoad (kept intact)
// ============================================================================

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  JNIEnv* env;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    LOGE("JNI_OnLoad: GetEnv failed");
    return -1;
  }
  jclass clazz =
      env->FindClass("androidx/media3/decoder/ffmpeg/FfmpegAudioDecoder");
  if (!clazz) {
    LOGE("JNI_OnLoad: FindClass failed");
    return -1;
  }
  growOutputBufferMethod =
      env->GetMethodID(clazz, "growOutputBuffer",
                       "(Landroidx/media3/decoder/"
                       "SimpleDecoderOutputBuffer;I)Ljava/nio/ByteBuffer;");
  if (!growOutputBufferMethod) {
    LOGE("JNI_OnLoad: GetMethodID failed");
    return -1;
  }
  return JNI_VERSION_1_6;
}

// ============================================================================
// Library functions (kept intact)
// ============================================================================

LIBRARY_FUNC(jstring, ffmpegGetVersion) {
  return env->NewStringUTF(LIBAVCODEC_IDENT);
}

LIBRARY_FUNC(jint, ffmpegGetInputBufferPaddingSize) {
  return (jint)AV_INPUT_BUFFER_PADDING_SIZE;
}

LIBRARY_FUNC(jboolean, ffmpegHasDecoder, jstring codecName) {
  return getCodecByName(env, codecName) != NULL;
}

// ============================================================================
// Audio decoder JNI functions (kept intact)
// ============================================================================

AUDIO_DECODER_FUNC(jlong, ffmpegInitialize, jstring codecName,
                   jbyteArray extraData, jboolean outputFloat,
                   jint rawSampleRate, jint rawChannelCount) {
  const AVCodec* codec = getCodecByName(env, codecName);
  if (!codec) {
    LOGE("Codec not found.");
    return 0L;
  }
  return (jlong)createContext(env, codec, extraData, outputFloat, rawSampleRate,
                              rawChannelCount);
}

AUDIO_DECODER_FUNC(jint, ffmpegDecode, jlong context, jobject inputData,
                   jint inputSize, jobject decoderOutputBuffer,
                   jobject outputData, jint outputSize) {
  if (!context) {
    LOGE("Context must be non-NULL.");
    return -1;
  }
  if (!inputData || !decoderOutputBuffer || !outputData) {
    LOGE("Input and output buffers must be non-NULL.");
    return -1;
  }
  if (inputSize < 0) {
    LOGE("Invalid input buffer size: %d.", inputSize);
    return -1;
  }
  if (outputSize < 0) {
    LOGE("Invalid output buffer length: %d", outputSize);
    return -1;
  }
  uint8_t* inputBuffer = (uint8_t*)env->GetDirectBufferAddress(inputData);
  uint8_t* outputBuffer = (uint8_t*)env->GetDirectBufferAddress(outputData);
  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    LOGE("Failed to allocate packet.");
    return -1;
  }
  packet->data = inputBuffer;
  packet->size = inputSize;
  const int ret =
      decodePacket((AVCodecContext*)context, packet, outputBuffer, outputSize,
                   GrowOutputBufferCallback{env, thiz, decoderOutputBuffer});
  av_packet_free(&packet);
  return ret;
}

uint8_t* GrowOutputBufferCallback::operator()(int requiredSize) const {
  jobject newOutputData = env->CallObjectMethod(
      thiz, growOutputBufferMethod, decoderOutputBuffer, requiredSize);
  if (env->ExceptionCheck()) {
    LOGE("growOutputBuffer() failed");
    env->ExceptionDescribe();
    return nullptr;
  }
  return static_cast<uint8_t*>(env->GetDirectBufferAddress(newOutputData));
}

AUDIO_DECODER_FUNC(jint, ffmpegGetChannelCount, jlong context) {
  if (!context) {
    LOGE("Context must be non-NULL.");
    return -1;
  }
  return ((AVCodecContext*)context)->ch_layout.nb_channels;
}

AUDIO_DECODER_FUNC(jint, ffmpegGetSampleRate, jlong context) {
  if (!context) {
    LOGE("Context must be non-NULL.");
    return -1;
  }
  return ((AVCodecContext*)context)->sample_rate;
}

AUDIO_DECODER_FUNC(jlong, ffmpegReset, jlong jContext, jbyteArray extraData) {
  AVCodecContext* context = (AVCodecContext*)jContext;
  if (!context) {
    LOGE("Tried to reset without a context.");
    return 0L;
  }

  AVCodecID codecId = context->codec_id;
  if (codecId == AV_CODEC_ID_TRUEHD) {
    jboolean outputFloat =
        (jboolean)(context->request_sample_fmt == OUTPUT_FORMAT_PCM_FLOAT);
    // Release and recreate the context if the codec is TrueHD.
    // TODO: Figure out why flushing doesn't work for this codec.
    releaseContext(context);
    const AVCodec* codec = avcodec_find_decoder(codecId);
    if (!codec) {
      LOGE("Unexpected error finding codec %d.", codecId);
      return 0L;
    }
    return (jlong)createContext(env, codec, extraData, outputFloat,
                                /* rawSampleRate= */ -1,
                                /* rawChannelCount= */ -1);
  }

  avcodec_flush_buffers(context);
  return (jlong)context;
}

AUDIO_DECODER_FUNC(void, ffmpegRelease, jlong context) {
  if (context) {
    releaseContext((AVCodecContext*)context);
  }
}

// ============================================================================
// Audio decoder internal functions (kept intact)
// ============================================================================

const AVCodec* getCodecByName(JNIEnv* env, jstring codecName) {
  if (!codecName) {
    return NULL;
  }
  const char* codecNameChars = env->GetStringUTFChars(codecName, NULL);
  const AVCodec* codec = avcodec_find_decoder_by_name(codecNameChars);
  env->ReleaseStringUTFChars(codecName, codecNameChars);
  return codec;
}

AVCodecContext* createContext(JNIEnv* env, const AVCodec* codec,
                              jbyteArray extraData, jboolean outputFloat,
                              jint rawSampleRate, jint rawChannelCount) {
  AVCodecContext* context = avcodec_alloc_context3(codec);
  if (!context) {
    LOGE("Failed to allocate context.");
    return NULL;
  }
  context->request_sample_fmt =
      outputFloat ? OUTPUT_FORMAT_PCM_FLOAT : OUTPUT_FORMAT_PCM_16BIT;
  if (extraData) {
    jsize size = env->GetArrayLength(extraData);
    context->extradata_size = size;
    context->extradata =
        (uint8_t*)av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!context->extradata) {
      LOGE("Failed to allocate extradata.");
      releaseContext(context);
      return NULL;
    }
    env->GetByteArrayRegion(extraData, 0, size, (jbyte*)context->extradata);
  }
  if (context->codec_id == AV_CODEC_ID_PCM_MULAW ||
      context->codec_id == AV_CODEC_ID_PCM_ALAW) {
    context->sample_rate = rawSampleRate;
    av_channel_layout_default(&context->ch_layout, rawChannelCount);
  }
  context->err_recognition = AV_EF_IGNORE_ERR;
  int result = avcodec_open2(context, codec, NULL);
  if (result < 0) {
    logError("avcodec_open2", result);
    releaseContext(context);
    return NULL;
  }
  return context;
}

int decodePacket(AVCodecContext* context, AVPacket* packet,
                 uint8_t* outputBuffer, int outputSize,
                 GrowOutputBufferCallback growBuffer) {
  int result = 0;
  // Queue input data.
  result = avcodec_send_packet(context, packet);
  if (result) {
    logError("avcodec_send_packet", result);
    return transformError(result);
  }

  // Dequeue output data until it runs out.
  int outSize = 0;
  while (true) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
      LOGE("Failed to allocate output frame.");
      return AUDIO_DECODER_ERROR_INVALID_DATA;
    }
    result = avcodec_receive_frame(context, frame);
    if (result) {
      av_frame_free(&frame);
      if (result == AVERROR(EAGAIN)) {
        break;
      }
      logError("avcodec_receive_frame", result);
      return transformError(result);
    }

    // Resample output.
    AVSampleFormat sampleFormat = context->sample_fmt;
    int channelCount = context->ch_layout.nb_channels;
    int sampleRate = context->sample_rate;
    int sampleCount = frame->nb_samples;
    int dataSize = av_samples_get_buffer_size(NULL, channelCount, sampleCount,
                                              sampleFormat, 1);
    SwrContext* resampleContext = static_cast<SwrContext*>(context->opaque);
    if (!resampleContext) {
      result =
          swr_alloc_set_opts2(&resampleContext,             // ps
                              &context->ch_layout,          // out_ch_layout
                              context->request_sample_fmt,  // out_sample_fmt
                              sampleRate,                   // out_sample_rate
                              &context->ch_layout,          // in_ch_layout
                              sampleFormat,                 // in_sample_fmt
                              sampleRate,                   // in_sample_rate
                              0,                            // log_offset
                              NULL                          // log_ctx
          );
      if (result < 0) {
        logError("swr_alloc_set_opts2", result);
        av_frame_free(&frame);
        return transformError(result);
      }
      result = swr_init(resampleContext);
      if (result < 0) {
        logError("swr_init", result);
        av_frame_free(&frame);
        return transformError(result);
      }
      context->opaque = resampleContext;
    }

    int outSampleSize = av_get_bytes_per_sample(context->request_sample_fmt);
    int outSamples = swr_get_out_samples(resampleContext, sampleCount);
    int bufferOutSize = outSampleSize * channelCount * outSamples;
    if (outSize + bufferOutSize > outputSize) {
      LOGD(
          "Output buffer size (%d) too small for output data (%d), "
          "reallocating buffer.",
          outputSize, outSize + bufferOutSize);
      outputSize = outSize + bufferOutSize;
      outputBuffer = growBuffer(outputSize);
      if (!outputBuffer) {
        LOGE("Failed to reallocate output buffer.");
        av_frame_free(&frame);
        return AUDIO_DECODER_ERROR_OTHER;
      }
    }
    result = swr_convert(resampleContext, &outputBuffer, bufferOutSize,
                         (const uint8_t**)frame->data, frame->nb_samples);
    av_frame_free(&frame);
    if (result < 0) {
      logError("swr_convert", result);
      return AUDIO_DECODER_ERROR_INVALID_DATA;
    }
    int available = swr_get_out_samples(resampleContext, 0);
    if (available != 0) {
      LOGE("Expected no samples remaining after resampling, but found %d.",
           available);
      return AUDIO_DECODER_ERROR_INVALID_DATA;
    }
    outputBuffer += bufferOutSize;
    outSize += bufferOutSize;
  }
  return outSize;
}

int transformError(int errorNumber) {
  return errorNumber == AVERROR_INVALIDDATA ? AUDIO_DECODER_ERROR_INVALID_DATA
                                            : AUDIO_DECODER_ERROR_OTHER;
}

void logError(const char* functionName, int errorNumber) {
  char* buffer = (char*)malloc(ERROR_STRING_BUFFER_LENGTH * sizeof(char));
  av_strerror(errorNumber, buffer, ERROR_STRING_BUFFER_LENGTH);
  LOGE("Error in %s: %s", functionName, buffer);
  free(buffer);
}

void releaseContext(AVCodecContext* context) {
  if (!context) {
    return;
  }
  SwrContext* swrContext;
  if ((swrContext = (SwrContext*)context->opaque)) {
    swr_free(&swrContext);
    context->opaque = NULL;
  }
  avcodec_free_context(&context);
}

// ============================================================================
// Video decoder JNI functions
// ============================================================================

/**
 * Transforms ffmpeg AVERROR into a negative VIDEO_DECODER_ERROR constant value.
 */
static int transformVideoError(int errorNumber) {
  return errorNumber == AVERROR_INVALIDDATA ? VIDEO_DECODER_ERROR_INVALID_DATA
                                            : VIDEO_DECODER_ERROR_OTHER;
}

// ---- Helper: set proper BT601 colorspace on swscale context ----
static void setSwsColorspace(SwsContext* sws) {
  if (!sws) return;
  // MPEG2 SD content uses BT.601 with limited (MPEG) range.
  const int* coeffs = sws_getCoefficients(SWS_CS_BT601);
  sws_setColorspaceDetails(sws, coeffs, /* srcRange= */ 0,
                           coeffs, /* dstRange= */ 0,
                           /* brightness= */ 0,
                           /* contrast= */ 1 << 16,
                           /* saturation= */ 1 << 16);
}

/**
 * Initializes a new video decoder context.
 */
VIDEO_DECODER_FUNC(jlong, ffmpegVideoInitialize, jstring codecName,
                   jbyteArray extraData, jint threads, jint width, jint height) {
  const AVCodec* codec = getCodecByName(env, codecName);
  if (!codec) {
    LOGE("Video codec not found.");
    return 0L;
  }

  AVCodecContext* context = avcodec_alloc_context3(codec);
  if (!context) {
    LOGE("Failed to allocate video codec context.");
    return 0L;
  }

  context->thread_count = threads;
  if (width > 0) context->width = width;
  if (height > 0) context->height = height;

  if (extraData) {
    jsize size = env->GetArrayLength(extraData);
    context->extradata_size = size;
    context->extradata =
        (uint8_t*)av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!context->extradata) {
      LOGE("Failed to allocate video extradata.");
      avcodec_free_context(&context);
      return 0L;
    }
    env->GetByteArrayRegion(extraData, 0, size, (jbyte*)context->extradata);
  }

  context->err_recognition = AV_EF_IGNORE_ERR;
  int result = avcodec_open2(context, codec, NULL);
  if (result < 0) {
    logError("avcodec_open2 (video)", result);
    avcodec_free_context(&context);
    return 0L;
  }

  JniContext* jniContext = new JniContext();
  jniContext->codecContext = context;
  jniContext->swsContext = NULL;
  jniContext->currentFrame = av_frame_alloc();
  if (!jniContext->currentFrame) {
    LOGE("Failed to allocate video frame.");
    avcodec_free_context(&context);
    delete jniContext;
    return 0L;
  }

  return (jlong)jniContext;
}

/**
 * Sends a compressed video packet to the decoder.
 * Returns: 0 on success, AVERROR(EAGAIN) if decoder buffer full,
 *          negative error on failure.
 */
VIDEO_DECODER_FUNC(jint, ffmpegVideoSendPacket, jlong context, jobject data,
                   jint size, jlong pts) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext) {
    LOGE("Invalid video decoder context.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  uint8_t* inputBuffer = (uint8_t*)env->GetDirectBufferAddress(data);
  if (!inputBuffer) {
    LOGE("Failed to get direct buffer address.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    LOGE("Failed to allocate video packet.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  packet->data = inputBuffer;
  packet->size = size;
  packet->pts = pts;

  int result = avcodec_send_packet(ctx->codecContext, packet);
  av_packet_free(&packet);

  if (result < 0) {
    if (result == AVERROR(EAGAIN)) {
      // Decoder buffer full — caller should call ffmpegVideoReceiveFrame to drain.
      return result;
    }
    if (result == AVERROR_EOF) {
      return 0;
    }
    logError("avcodec_send_packet (video)", result);
    return transformVideoError(result);
  }
  return 0;
}

/**
 * Receives a decoded frame into the context's currentFrame.
 * Returns: 0 on success, AVERROR(EAGAIN) if no frame available,
 *          negative on error.
 */
VIDEO_DECODER_FUNC(jint, ffmpegVideoReceiveFrame, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext) {
    LOGE("Invalid video decoder context.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  av_frame_unref(ctx->currentFrame);
  int result = avcodec_receive_frame(ctx->codecContext, ctx->currentFrame);
  if (result < 0) {
    if (result == AVERROR(EAGAIN)) return result;
    if (result == AVERROR_EOF) return result;
    logError("avcodec_receive_frame (video)", result);
    return transformVideoError(result);
  }
  return 0;
}

/**
 * Creates a per-buffer AVFrame reference (av_frame_ref) from the current
 * decoded frame. The caller stores this in outputBuffer.decoderPrivate and
 * must free it with ffmpegVideoReleaseFrame when done.
 *
 * This is the key fix for the "only first frame" bug: each output buffer
 * gets its own AVFrame reference, so frames are not overwritten by
 * subsequent decode() calls while waiting to be rendered.
 *
 * Returns: positive jlong pointer on success, 0 on failure.
 */
VIDEO_DECODER_FUNC(jlong, ffmpegVideoGetFrameHandle, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext || !ctx->currentFrame->buf[0]) {
    return 0;
  }
  AVFrame* frameRef = av_frame_alloc();
  if (!frameRef) return 0;
  if (av_frame_ref(frameRef, ctx->currentFrame) < 0) {
    av_frame_free(&frameRef);
    return 0;
  }
  return (jlong)frameRef;
}

/**
 * Renders a per-frame AVFrame to an Android Surface.
 *
 * Key optimizations:
 * - If source is already YUV420P (typical for MPEG2), skip sws_scale
 *   entirely and copy planes directly to ANativeWindow.
 * - If conversion is needed, use sws_scale with BT.601 colorspace.
 * - Avoid per-frame allocation by reusing sws context.
 *
 * @param context     Decoder context (for sws context reuse)
 * @param frameHandle Per-frame AVFrame pointer from ffmpegVideoGetFrameHandle
 * @param surface     Android Surface object
 * @return            0 on success, negative on error
 */
VIDEO_DECODER_FUNC(jint, ffmpegVideoRenderFrame, jlong context,
                   jlong frameHandle, jobject surface) {
  JniContext* ctx = (JniContext*)context;
  AVFrame* frame = (AVFrame*)frameHandle;
  if (!ctx || !ctx->codecContext || !frame) {
    LOGE("Invalid context or frame handle in ffmpegVideoRenderFrame.");
    return VIDEO_DECODER_ERROR_OTHER;
  }
  if (!surface) {
    LOGE("Surface is null.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  // Use per-frame dimensions if available, fall back to codec context
  int width = frame->width > 0 ? frame->width : ctx->codecContext->width;
  int height = frame->height > 0 ? frame->height : ctx->codecContext->height;
  if (width <= 0 || height <= 0) {
    LOGE("Invalid frame dimensions: %dx%d", width, height);
    return VIDEO_DECODER_ERROR_OTHER;
  }

  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (!window) {
    LOGE("Failed to get ANativeWindow from Surface.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  // Determine if we need sws_scale (source is not YUV420P)
  bool needScale = (frame->format != AV_PIX_FMT_YUV420P);

  // Source planes — either directly from the frame or from a converted buffer
  const uint8_t* srcY;
  const uint8_t* srcU;
  const uint8_t* srcV;
  int srcStrideY;
  int srcStrideU;
  int srcStrideV;

  // For sws_scale path
  AVFrame* scaleFrame = NULL;
  uint8_t* scaleBuffer = NULL;

  if (needScale) {
    // Create or reuse swscale context
    if (!ctx->swsContext) {
      ctx->swsContext = sws_getContext(
          width, height, (AVPixelFormat)frame->format,
          width, height, AV_PIX_FMT_YUV420P,
          SWS_BILINEAR, NULL, NULL, NULL);
      if (!ctx->swsContext) {
        LOGE("Failed to create swscale context.");
        ANativeWindow_release(window);
        return VIDEO_DECODER_ERROR_OTHER;
      }
      setSwsColorspace(ctx->swsContext);
    }

    scaleFrame = av_frame_alloc();
    if (!scaleFrame) {
      ANativeWindow_release(window);
      return VIDEO_DECODER_ERROR_OTHER;
    }
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
    scaleBuffer = (uint8_t*)av_malloc(numBytes);
    if (!scaleBuffer) {
      av_frame_free(&scaleFrame);
      ANativeWindow_release(window);
      return VIDEO_DECODER_ERROR_OTHER;
    }
    av_image_fill_arrays(scaleFrame->data, scaleFrame->linesize, scaleBuffer,
                         AV_PIX_FMT_YUV420P, width, height, 1);

    int swsResult = sws_scale(ctx->swsContext,
                              (const uint8_t* const*)frame->data,
                              frame->linesize, 0, height,
                              scaleFrame->data, scaleFrame->linesize);
    if (swsResult < 0) {
      LOGE("sws_scale failed: %d", swsResult);
      av_free(scaleBuffer);
      av_frame_free(&scaleFrame);
      ANativeWindow_release(window);
      return VIDEO_DECODER_ERROR_OTHER;
    }
    srcY = scaleFrame->data[0];
    srcU = scaleFrame->data[1];
    srcV = scaleFrame->data[2];
    srcStrideY = scaleFrame->linesize[0];
    srcStrideU = scaleFrame->linesize[1];
    srcStrideV = scaleFrame->linesize[2];
  } else {
    // Source is already YUV420P — copy directly from the frame
    srcY = frame->data[0];
    srcU = frame->data[1];
    srcV = frame->data[2];
    srcStrideY = frame->linesize[0];
    srcStrideU = frame->linesize[1];
    srcStrideV = frame->linesize[2];
  }

  // Set buffer geometry to YV12
  ANativeWindow_setBuffersGeometry(window, width, height,
                                   0x32315659 /* WINDOW_FORMAT_YV12 */);

  ANativeWindow_Buffer winBuf;
  int lockResult = ANativeWindow_lock(window, &winBuf, NULL);
  if (lockResult != 0) {
    LOGE("ANativeWindow_lock failed: %d", lockResult);
    if (scaleBuffer) av_free(scaleBuffer);
    if (scaleFrame) av_frame_free(&scaleFrame);
    ANativeWindow_release(window);
    return VIDEO_DECODER_ERROR_OTHER;
  }

  // Copy YUV planes to ANativeWindow buffer
  uint8_t* dst = (uint8_t*)winBuf.bits;
  int dstYStride = winBuf.stride;
  int dstUVStride = dstYStride / 2;
  int uvHeight = height / 2;
  int uvWidth = width / 2;

  // Y plane
  if (srcStrideY == dstYStride) {
    memcpy(dst, srcY, (size_t)dstYStride * height);
  } else {
    for (int y = 0; y < height; y++) {
      memcpy(dst + y * dstYStride, srcY + y * srcStrideY, width);
    }
  }

  // V plane (YV12: V before U)
  uint8_t* dstV = dst + (size_t)dstYStride * height;
  if (srcStrideV == dstUVStride) {
    memcpy(dstV, srcV, (size_t)dstUVStride * uvHeight);
  } else {
    for (int y = 0; y < uvHeight; y++) {
      memcpy(dstV + y * dstUVStride, srcV + y * srcStrideV, uvWidth);
    }
  }

  // U plane (YV12: U after V)
  uint8_t* dstU = dstV + (size_t)dstUVStride * uvHeight;
  if (srcStrideU == dstUVStride) {
    memcpy(dstU, srcU, (size_t)dstUVStride * uvHeight);
  } else {
    for (int y = 0; y < uvHeight; y++) {
      memcpy(dstU + y * dstUVStride, srcU + y * srcStrideU, uvWidth);
    }
  }

  ANativeWindow_unlockAndPost(window);

  if (scaleBuffer) av_free(scaleBuffer);
  if (scaleFrame) av_frame_free(&scaleFrame);
  ANativeWindow_release(window);
  return 0;
}

/**
 * Releases a per-frame AVFrame reference.
 * Called when the output buffer is returned to the pool.
 */
VIDEO_DECODER_FUNC(void, ffmpegVideoReleaseFrame, jlong frameHandle) {
  if (frameHandle) {
    AVFrame* frame = (AVFrame*)frameHandle;
    av_frame_free(&frame);
  }
}

/** Returns the width of the current decoded frame. */
VIDEO_DECODER_FUNC(jint, ffmpegVideoGetWidth, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext) return -1;
  return ctx->codecContext->width;
}

/** Returns the height of the current decoded frame. */
VIDEO_DECODER_FUNC(jint, ffmpegVideoGetHeight, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext) return -1;
  return ctx->codecContext->height;
}

/** Returns the PTS of the current decoded frame in microseconds. */
VIDEO_DECODER_FUNC(jlong, ffmpegVideoGetPts, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext || !ctx->currentFrame->buf[0]) return -1;
  return ctx->currentFrame->pts;
}

/**
 * Copies the current decoded frame's YUV data into direct ByteBuffers.
 * Skips sws_scale when source is already YUV420P for better performance.
 */
VIDEO_DECODER_FUNC(jint, ffmpegVideoCopyFrameData, jlong context,
                   jobject yBuffer, jobject uBuffer, jobject vBuffer) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext || !ctx->currentFrame->buf[0]) {
    LOGE("No current frame to copy.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  int width = ctx->codecContext->width;
  int height = ctx->codecContext->height;
  AVFrame* frame = ctx->currentFrame;

  bool needScale = (frame->format != AV_PIX_FMT_YUV420P);

  const uint8_t* srcY;
  const uint8_t* srcU;
  const uint8_t* srcV;
  int srcStrideY, srcStrideU, srcStrideV;

  AVFrame* scaleFrame = NULL;
  uint8_t* scaleBuffer = NULL;

  if (needScale) {
    if (!ctx->swsContext) {
      ctx->swsContext = sws_getContext(
          width, height, (AVPixelFormat)frame->format,
          width, height, AV_PIX_FMT_YUV420P,
          SWS_BILINEAR, NULL, NULL, NULL);
      if (!ctx->swsContext) {
        LOGE("Failed to create swscale context for frame copy.");
        return VIDEO_DECODER_ERROR_OTHER;
      }
      setSwsColorspace(ctx->swsContext);
    }
    scaleFrame = av_frame_alloc();
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
    scaleBuffer = (uint8_t*)av_malloc(numBytes);
    av_image_fill_arrays(scaleFrame->data, scaleFrame->linesize, scaleBuffer,
                         AV_PIX_FMT_YUV420P, width, height, 1);
    sws_scale(ctx->swsContext,
              (const uint8_t* const*)frame->data, frame->linesize,
              0, height, scaleFrame->data, scaleFrame->linesize);
    srcY = scaleFrame->data[0]; srcU = scaleFrame->data[1]; srcV = scaleFrame->data[2];
    srcStrideY = scaleFrame->linesize[0]; srcStrideU = scaleFrame->linesize[1];
    srcStrideV = scaleFrame->linesize[2];
  } else {
    srcY = frame->data[0]; srcU = frame->data[1]; srcV = frame->data[2];
    srcStrideY = frame->linesize[0]; srcStrideU = frame->linesize[1];
    srcStrideV = frame->linesize[2];
  }

  int uvWidth = width / 2;
  int uvHeight = height / 2;

  uint8_t* yDst = (uint8_t*)env->GetDirectBufferAddress(yBuffer);
  if (yDst) {
    if (srcStrideY == width) {
      memcpy(yDst, srcY, (size_t)width * height);
    } else {
      for (int y = 0; y < height; y++)
        memcpy(yDst + y * width, srcY + y * srcStrideY, width);
    }
  }
  uint8_t* uDst = (uint8_t*)env->GetDirectBufferAddress(uBuffer);
  if (uDst) {
    if (srcStrideU == uvWidth) {
      memcpy(uDst, srcU, (size_t)uvWidth * uvHeight);
    } else {
      for (int y = 0; y < uvHeight; y++)
        memcpy(uDst + y * uvWidth, srcU + y * srcStrideU, uvWidth);
    }
  }
  uint8_t* vDst = (uint8_t*)env->GetDirectBufferAddress(vBuffer);
  if (vDst) {
    if (srcStrideV == uvWidth) {
      memcpy(vDst, srcV, (size_t)uvWidth * uvHeight);
    } else {
      for (int y = 0; y < uvHeight; y++)
        memcpy(vDst + y * uvWidth, srcV + y * srcStrideV, uvWidth);
    }
  }

  if (scaleBuffer) av_free(scaleBuffer);
  if (scaleFrame) av_frame_free(&scaleFrame);
  return 0;
}

/** Resets the video decoder (flush internal buffers). */
VIDEO_DECODER_FUNC(jlong, ffmpegVideoReset, jlong context, jbyteArray extraData) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext) {
    LOGE("Tried to reset without a video decoder context.");
    return 0L;
  }

  AVCodecID codecId = ctx->codecContext->codec_id;
  AVCodecContext* codecCtx = ctx->codecContext;
  avcodec_flush_buffers(codecCtx);

  if (extraData) {
    if (ctx->swsContext) { sws_freeContext(ctx->swsContext); ctx->swsContext = NULL; }
    av_frame_unref(ctx->currentFrame);
    avcodec_free_context(&codecCtx);
    ctx->codecContext = NULL;

    const AVCodec* codec = avcodec_find_decoder(codecId);
    if (!codec) { LOGE("Video codec %d not found.", codecId); return 0L; }

    AVCodecContext* newContext = avcodec_alloc_context3(codec);
    if (!newContext) { LOGE("Failed to allocate new video codec context."); return 0L; }

    jsize size = env->GetArrayLength(extraData);
    newContext->extradata_size = size;
    newContext->extradata = (uint8_t*)av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!newContext->extradata) {
      avcodec_free_context(&newContext);
      return 0L;
    }
    env->GetByteArrayRegion(extraData, 0, size, (jbyte*)newContext->extradata);
    newContext->err_recognition = AV_EF_IGNORE_ERR;

    int result = avcodec_open2(newContext, codec, NULL);
    if (result < 0) {
      logError("avcodec_open2 (video reset)", result);
      avcodec_free_context(&newContext);
      return 0L;
    }
    ctx->codecContext = newContext;
  }
  return (jlong)ctx;
}

/** Releases the video decoder and all associated resources. */
VIDEO_DECODER_FUNC(void, ffmpegVideoRelease, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx) return;
  if (ctx->swsContext) { sws_freeContext(ctx->swsContext); ctx->swsContext = NULL; }
  if (ctx->currentFrame) { av_frame_free(&ctx->currentFrame); }
  if (ctx->codecContext) { avcodec_free_context(&ctx->codecContext); }
  delete ctx;
}
