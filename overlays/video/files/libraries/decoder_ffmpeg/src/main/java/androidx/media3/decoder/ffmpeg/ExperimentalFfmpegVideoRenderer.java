/*
 * Copyright (C) 2020 The Android Open Source Project
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
package androidx.media3.decoder.ffmpeg;

import static androidx.media3.exoplayer.DecoderReuseEvaluation.DISCARD_REASON_MIME_TYPE_CHANGED;
import static androidx.media3.exoplayer.DecoderReuseEvaluation.REUSE_RESULT_NO;
import static androidx.media3.exoplayer.DecoderReuseEvaluation.REUSE_RESULT_YES_WITHOUT_RECONFIGURATION;

import android.os.Handler;
import android.view.Surface;
import androidx.annotation.Nullable;
import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.util.Assertions;
import androidx.media3.common.util.TraceUtil;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.decoder.CryptoConfig;
import androidx.media3.decoder.Decoder;
import androidx.media3.decoder.DecoderInputBuffer;
import androidx.media3.decoder.VideoDecoderOutputBuffer;
import androidx.media3.exoplayer.DecoderReuseEvaluation;
import androidx.media3.exoplayer.RendererCapabilities;
import androidx.media3.exoplayer.video.DecoderVideoRenderer;
import androidx.media3.exoplayer.video.VideoRendererEventListener;
import java.util.Objects;

/**
 * Decodes and renders video using FFmpeg via software decoding.
 *
 * <p>Supports video formats including MPEG2, H.264, HEVC, VP9, and AV1 via the
 * FFmpeg native decoder.
 */
@UnstableApi
public final class ExperimentalFfmpegVideoRenderer extends DecoderVideoRenderer {

  private static final String TAG = "FfmpegVideoRenderer";

  /** The number of input and output buffers. */
  private static final int NUM_BUFFERS = 4;

  /** The default input buffer size for video. */
  private static final int DEFAULT_INPUT_BUFFER_SIZE = 1024 * 768;

  @Nullable private ExperimentalFfmpegVideoDecoder decoder;

  /**
   * Creates a new instance.
   *
   * @param allowedJoiningTimeMs The maximum duration in milliseconds for which this video renderer
   *     can attempt to seamlessly join an ongoing playback.
   * @param eventHandler A handler to use when delivering events to {@code eventListener}. May be
   *     null if delivery of events is not required.
   * @param eventListener A listener of events. May be null if delivery of events is not required.
   * @param maxDroppedFramesToNotify The maximum number of frames that can be dropped between
   *     invocations of {@link VideoRendererEventListener#onDroppedFrames(int, long)}.
   */
  public ExperimentalFfmpegVideoRenderer(
      long allowedJoiningTimeMs,
      @Nullable Handler eventHandler,
      @Nullable VideoRendererEventListener eventListener,
      int maxDroppedFramesToNotify) {
    super(allowedJoiningTimeMs, eventHandler, eventListener, maxDroppedFramesToNotify);
  }

  @Override
  public String getName() {
    return TAG;
  }

  @Override
  public final @RendererCapabilities.Capabilities int supportsFormat(Format format) {
    @Nullable String mimeType = format.sampleMimeType;
    if (!FfmpegLibrary.isAvailable() || mimeType == null || !MimeTypes.isVideo(mimeType)) {
      return RendererCapabilities.create(C.FORMAT_UNSUPPORTED_TYPE);
    }
    // Only handle MPEG2 video via FFmpeg software decoder.
    // Let MediaCodec handle H264/HEVC/VP9/AV1 — devices have hardware decoders
    // for those, and the FFmpeg video renderer's native implementation is only
    // validated for MPEG2.
    if (!MimeTypes.VIDEO_MPEG2.equals(mimeType)) {
      return RendererCapabilities.create(C.FORMAT_UNSUPPORTED_TYPE);
    }
    if (!FfmpegLibrary.supportsFormat(mimeType)) {
      return RendererCapabilities.create(C.FORMAT_UNSUPPORTED_SUBTYPE);
    }
    if (format.cryptoType != C.CRYPTO_TYPE_NONE) {
      return RendererCapabilities.create(C.FORMAT_UNSUPPORTED_DRM);
    }
    return RendererCapabilities.create(
        C.FORMAT_HANDLED,
        RendererCapabilities.ADAPTIVE_NOT_SEAMLESS,
        RendererCapabilities.TUNNELING_NOT_SUPPORTED);
  }

  @Override
  protected Decoder<DecoderInputBuffer, VideoDecoderOutputBuffer, FfmpegDecoderException>
      createDecoder(Format format, @Nullable CryptoConfig cryptoConfig)
          throws FfmpegDecoderException {
    TraceUtil.beginSection("createFfmpegVideoDecoder");
    @Nullable String mimeType = Assertions.checkNotNull(format.sampleMimeType);
    @Nullable String codecName = FfmpegLibrary.getCodecName(mimeType);
    if (codecName == null) {
      throw new FfmpegDecoderException("Unsupported video MIME type: " + mimeType);
    }

    @Nullable byte[] extraData = null;
    if (format.initializationData != null && !format.initializationData.isEmpty()) {
      extraData = format.initializationData.get(0);
    }

    int threads = Runtime.getRuntime().availableProcessors();
    int rotationDegrees = format.rotationDegrees;
    int width = format.width != Format.NO_VALUE ? format.width : 0;
    int height = format.height != Format.NO_VALUE ? format.height : 0;
    int initialInputBufferSize =
        format.maxInputSize != Format.NO_VALUE ? format.maxInputSize : DEFAULT_INPUT_BUFFER_SIZE;

    decoder =
        new ExperimentalFfmpegVideoDecoder(
            codecName,
            extraData,
            threads,
            rotationDegrees,
            width,
            height,
            NUM_BUFFERS,
            NUM_BUFFERS,
            initialInputBufferSize);
    TraceUtil.endSection();
    return decoder;
  }

  @Override
  protected void renderOutputBufferToSurface(
      VideoDecoderOutputBuffer outputBuffer, Surface surface) throws FfmpegDecoderException {
    TraceUtil.beginSection("renderFfmpegVideoToSurface");
    if (decoder == null) {
      throw new FfmpegDecoderException(
          "Failed to render output buffer to surface: decoder is not initialized.");
    }
    decoder.renderToSurface(outputBuffer, surface);
    outputBuffer.release();
    TraceUtil.endSection();
  }

  @Override
  protected void setDecoderOutputMode(@C.VideoOutputMode int outputMode) {
    if (decoder != null) {
      decoder.setOutputMode(outputMode);
    }
  }

  @Override
  protected DecoderReuseEvaluation canReuseDecoder(
      String decoderName, Format oldFormat, Format newFormat) {
    boolean sameMimeType = Objects.equals(oldFormat.sampleMimeType, newFormat.sampleMimeType);
    if (sameMimeType && decoder != null) {
      @Nullable String codecName = FfmpegLibrary.getCodecName(oldFormat.sampleMimeType);
      if (codecName != null && decoder.canReuseDecoder(codecName)) {
        return new DecoderReuseEvaluation(
            decoderName,
            oldFormat,
            newFormat,
            REUSE_RESULT_YES_WITHOUT_RECONFIGURATION,
            /* discardReasons= */ 0);
      }
    }
    return new DecoderReuseEvaluation(
        decoderName,
        oldFormat,
        newFormat,
        REUSE_RESULT_NO,
        DISCARD_REASON_MIME_TYPE_CHANGED);
  }
}
