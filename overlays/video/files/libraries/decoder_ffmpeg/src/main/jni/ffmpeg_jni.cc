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
// Video decoder JNI functions (NEW - added for MPEG2 video support)
// ============================================================================

/**
 * Transforms ffmpeg AVERROR into a negative VIDEO_DECODER_ERROR constant value.
 */
static int transformVideoError(int errorNumber) {
  return errorNumber == AVERROR_INVALIDDATA ? VIDEO_DECODER_ERROR_INVALID_DATA
                                            : VIDEO_DECODER_ERROR_OTHER;
}

/**
 * Initializes a new video decoder context.
 *
 * @param codecName  FFmpeg codec name (e.g. "mpeg2video")
 * @param extraData  Codec-specific initialization data (may be NULL)
 * @param threads    Number of decoding threads to use
 * @param width      Video width hint (0 if unknown)
 * @param height     Video height hint (0 if unknown)
 * @return           Opaque native context pointer, or 0 on failure
 */
VIDEO_DECODER_FUNC(jlong, ffmpegVideoInitialize, jstring codecName,
                   jbyteArray extraData, jint threads, jint width, jint height) {
  const AVCodec* codec = getCodecByName(env, codecName);
  if (!codec) {
    LOGE("Video codec '%s' not found.", 
         codecName ? env->GetStringUTFChars(codecName, NULL) : "null");
    return 0L;
  }

  AVCodecContext* context = avcodec_alloc_context3(codec);
  if (!context) {
    LOGE("Failed to allocate video codec context.");
    return 0L;
  }

  context->thread_count = threads;
  if (width > 0) {
    context->width = width;
  }
  if (height > 0) {
    context->height = height;
  }

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
 *
 * @param context  Native context pointer
 * @param data     Direct ByteBuffer containing compressed data
 * @param size     Number of bytes in the buffer
 * @param pts      Presentation timestamp in microseconds
 * @return         0 on success, negative error code on failure
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
      // Decoder output needs to be drained first. This is not a fatal error.
      LOGD("avcodec_send_packet returned EAGAIN.");
      return result;
    }
    if (result == AVERROR_EOF) {
      LOGD("avcodec_send_packet returned EOF.");
      return 0;
    }
    logError("avcodec_send_packet (video)", result);
    return transformVideoError(result);
  }

  return 0;
}

/**
 * Attempts to receive a decoded video frame from the decoder.
 *
 * @param context  Native context pointer
 * @return         0 if a frame was received, AVERROR(EAGAIN) if no output
 *                 available, negative error code on failure
 */
VIDEO_DECODER_FUNC(jint, ffmpegVideoReceiveFrame, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext) {
    LOGE("Invalid video decoder context.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  // Unref previous frame if any
  av_frame_unref(ctx->currentFrame);

  int result = avcodec_receive_frame(ctx->codecContext, ctx->currentFrame);
  if (result < 0) {
    if (result == AVERROR(EAGAIN)) {
      return result;
    }
    if (result == AVERROR_EOF) {
      LOGD("Video decoder reached EOF.");
      return result;
    }
    logError("avcodec_receive_frame (video)", result);
    return transformVideoError(result);
  }

  return 0;
}

/**
 * Renders the current decoded frame to an Android Surface using swscale +
 * manual YUV plane copying (no libyuv dependency).
 *
 * @param context  Native context pointer
 * @param surface  Android Surface object
 * @return         0 on success, negative error code on failure
 */
VIDEO_DECODER_FUNC(jint, ffmpegVideoRenderFrame, jlong context,
                   jobject surface) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext) {
    LOGE("Invalid video decoder context.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  if (!surface) {
    LOGE("Surface is null.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (!window) {
    LOGE("Failed to get ANativeWindow from Surface.");
    return VIDEO_DECODER_ERROR_OTHER;
  }

  int width = ctx->codecContext->width;
  int height = ctx->codecContext->height;

  if (width <= 0 || height <= 0) {
    LOGE("Invalid frame dimensions: %dx%d", width, height);
    ANativeWindow_release(window);
    return VIDEO_DECODER_ERROR_OTHER;
  }

  // Initialize swscale context for YUV420P output with proper stride alignment
  if (!ctx->swsContext) {
    ctx->swsContext = sws_getContext(
        width, height, ctx->codecContext->pix_fmt,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL);
    if (!ctx->swsContext) {
      LOGE("Failed to create swscale context.");
      ANativeWindow_release(window);
      return VIDEO_DECODER_ERROR_OTHER;
    }
  }

  // Allocate aligned intermediate buffers for scaled YUV420P output
  AVFrame* scaleFrame = av_frame_alloc();
  if (!scaleFrame) {
    LOGE("Failed to allocate scale frame.");
    ANativeWindow_release(window);
    return VIDEO_DECODER_ERROR_OTHER;
  }

  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
  uint8_t* scaleBuffer = (uint8_t*)av_malloc(numBytes);
  if (!scaleBuffer) {
    LOGE("Failed to allocate scale buffer.");
    av_frame_free(&scaleFrame);
    ANativeWindow_release(window);
    return VIDEO_DECODER_ERROR_OTHER;
  }

  av_image_fill_arrays(scaleFrame->data, scaleFrame->linesize, scaleBuffer,
                       AV_PIX_FMT_YUV420P, width, height, 1);

  // Scale the decoded frame to YUV420P with proper stride alignment
  int swsResult = sws_scale(ctx->swsContext,
                            (const uint8_t* const*)ctx->currentFrame->data,
                            ctx->currentFrame->linesize,
                            0, height,
                            scaleFrame->data, scaleFrame->linesize);
  if (swsResult < 0) {
    LOGE("sws_scale failed: %d", swsResult);
    av_free(scaleBuffer);
    av_frame_free(&scaleFrame);
    ANativeWindow_release(window);
    return VIDEO_DECODER_ERROR_OTHER;
  }

  // Set buffer geometry to YV12 (YUV420P with U/V planes swapped)
  // YV12 = WINDOW_FORMAT_YV12 (0x32315659)
  int format = 0x32315659; // YV12
  ANativeWindow_setBuffersGeometry(window, width, height, format);

  ANativeWindow_Buffer winBuf;
  int lockResult = ANativeWindow_lock(window, &winBuf, NULL);
  if (lockResult != 0) {
    LOGE("ANativeWindow_lock failed: %d", lockResult);
    av_free(scaleBuffer);
    av_frame_free(&scaleFrame);
    ANativeWindow_release(window);
    return VIDEO_DECODER_ERROR_OTHER;
  }

  // Manual YUV plane copying (I420Copy-style, no libyuv dependency)
  // ANativeWindow YV12 layout:
  //   - Y plane at offset 0, stride = winBuf.stride pixels
  //   - V plane at offset (height * stride) pixels, stride = stride/2 pixels
  //   - U plane at offset (height * stride + height/2 * stride/2) pixels
  uint8_t* dst = (uint8_t*)winBuf.bits;
  int dstYStride = winBuf.stride;         // stride in pixels for Y
  int dstVStride = dstYStride / 2;        // stride in pixels for V
  int dstUStride = dstYStride / 2;        // stride in pixels for U
  int uvHeight = height / 2;
  int uvWidth = width / 2;

  // Y plane: row-by-row copy with stride alignment
  for (int y = 0; y < height; y++) {
    memcpy(dst + y * dstYStride,
           scaleFrame->data[0] + y * scaleFrame->linesize[0],
           width);
  }

  // V plane (YV12: V comes before U, source is scaleFrame->data[2])
  uint8_t* dstV = dst + dstYStride * height;
  for (int y = 0; y < uvHeight; y++) {
    memcpy(dstV + y * dstVStride,
           scaleFrame->data[2] + y * scaleFrame->linesize[2],
           uvWidth);
  }

  // U plane (YV12: U comes after V, source is scaleFrame->data[1])
  uint8_t* dstU = dstV + dstVStride * uvHeight;
  for (int y = 0; y < uvHeight; y++) {
    memcpy(dstU + y * dstUStride,
           scaleFrame->data[1] + y * scaleFrame->linesize[1],
           uvWidth);
  }

  ANativeWindow_unlockAndPost(window);
  av_free(scaleBuffer);
  av_frame_free(&scaleFrame);

  ANativeWindow_release(window);

  return 0;
}

/**
 * Returns the width of the current decoded frame.
 */
VIDEO_DECODER_FUNC(jint, ffmpegVideoGetWidth, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext) {
    return -1;
  }
  return ctx->codecContext->width;
}

/**
 * Returns the height of the current decoded frame.
 */
VIDEO_DECODER_FUNC(jint, ffmpegVideoGetHeight, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext) {
    return -1;
  }
  return ctx->codecContext->height;
}

/**
 * Returns the PTS of the current decoded frame in microseconds.
 */
VIDEO_DECODER_FUNC(jlong, ffmpegVideoGetPts, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext || !ctx->currentFrame->buf[0]) {
    return -1;
  }
  return ctx->currentFrame->pts;
}

/**
 * Copies the current decoded frame's YUV data into direct ByteBuffers using
 * manual plane copying (no libyuv).
 *
 * @param context  Native context pointer
 * @param yBuffer  Direct ByteBuffer for Y plane data
 * @param uBuffer  Direct ByteBuffer for U plane data
 * @param vBuffer  Direct ByteBuffer for V plane data
 * @return         0 on success, negative on error
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

  // Use swscale for stride-aligned YUV420P output (same pattern as render)
  if (!ctx->swsContext) {
    ctx->swsContext = sws_getContext(
        width, height, ctx->codecContext->pix_fmt,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL);
    if (!ctx->swsContext) {
      LOGE("Failed to create swscale context for frame copy.");
      return VIDEO_DECODER_ERROR_OTHER;
    }
  }

  AVFrame* scaleFrame = av_frame_alloc();
  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
  uint8_t* scaleBuffer = (uint8_t*)av_malloc(numBytes);
  av_image_fill_arrays(scaleFrame->data, scaleFrame->linesize, scaleBuffer,
                       AV_PIX_FMT_YUV420P, width, height, 1);

  sws_scale(ctx->swsContext,
            (const uint8_t* const*)ctx->currentFrame->data,
            ctx->currentFrame->linesize,
            0, height,
            scaleFrame->data, scaleFrame->linesize);

  // Manual plane copying to Java ByteBuffers
  uint8_t* yDst = (uint8_t*)env->GetDirectBufferAddress(yBuffer);
  if (yDst) {
    int ySize = width * height;
    if (scaleFrame->linesize[0] == width) {
      memcpy(yDst, scaleFrame->data[0], ySize);
    } else {
      for (int y = 0; y < height; y++) {
        memcpy(yDst + y * width,
               scaleFrame->data[0] + y * scaleFrame->linesize[0],
               width);
      }
    }
  }

  int uvWidth = width / 2;
  int uvHeight = height / 2;

  uint8_t* uDst = (uint8_t*)env->GetDirectBufferAddress(uBuffer);
  if (uDst) {
    int uSize = uvWidth * uvHeight;
    if (scaleFrame->linesize[1] == uvWidth) {
      memcpy(uDst, scaleFrame->data[1], uSize);
    } else {
      for (int y = 0; y < uvHeight; y++) {
        memcpy(uDst + y * uvWidth,
               scaleFrame->data[1] + y * scaleFrame->linesize[1],
               uvWidth);
      }
    }
  }

  uint8_t* vDst = (uint8_t*)env->GetDirectBufferAddress(vBuffer);
  if (vDst) {
    int vSize = uvWidth * uvHeight;
    if (scaleFrame->linesize[2] == uvWidth) {
      memcpy(vDst, scaleFrame->data[2], vSize);
    } else {
      for (int y = 0; y < uvHeight; y++) {
        memcpy(vDst + y * uvWidth,
               scaleFrame->data[2] + y * scaleFrame->linesize[2],
               uvWidth);
      }
    }
  }

  av_free(scaleBuffer);
  av_frame_free(&scaleFrame);
  return 0;
}

/**
 * Resets the video decoder (flush internal buffers).
 *
 * @param context   Native context pointer
 * @param extraData Optional new extradata (may be NULL to keep current)
 * @return          Native context pointer (same or recreated), 0 on failure
 */
VIDEO_DECODER_FUNC(jlong, ffmpegVideoReset, jlong context,
                   jbyteArray extraData) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx || !ctx->codecContext) {
    LOGE("Tried to reset without a video decoder context.");
    return 0L;
  }

  AVCodecID codecId = ctx->codecContext->codec_id;
  AVCodecContext* codecCtx = ctx->codecContext;

  // Flush decoder buffers (this keeps the codec context alive)
  avcodec_flush_buffers(codecCtx);

  // If new extradata is provided, re-open the codec
  if (extraData) {
    // Release current context
    if (ctx->swsContext) {
      sws_freeContext(ctx->swsContext);
      ctx->swsContext = NULL;
    }
    av_frame_unref(ctx->currentFrame);
    avcodec_free_context(&codecCtx);
    ctx->codecContext = NULL;

    const AVCodec* codec = avcodec_find_decoder(codecId);
    if (!codec) {
      LOGE("Unexpected error finding video codec %d.", codecId);
      return 0L;
    }

    AVCodecContext* newContext = avcodec_alloc_context3(codec);
    if (!newContext) {
      LOGE("Failed to allocate new video codec context.");
      return 0L;
    }

    jsize size = env->GetArrayLength(extraData);
    newContext->extradata_size = size;
    newContext->extradata =
        (uint8_t*)av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!newContext->extradata) {
      LOGE("Failed to allocate extradata on reset.");
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

/**
 * Releases the video decoder and all associated resources.
 */
VIDEO_DECODER_FUNC(void, ffmpegVideoRelease, jlong context) {
  JniContext* ctx = (JniContext*)context;
  if (!ctx) {
    return;
  }
  if (ctx->swsContext) {
    sws_freeContext(ctx->swsContext);
    ctx->swsContext = NULL;
  }
  if (ctx->currentFrame) {
    av_frame_free(&ctx->currentFrame);
  }
  if (ctx->codecContext) {
    avcodec_free_context(&ctx->codecContext);
  }
  delete ctx;
}
