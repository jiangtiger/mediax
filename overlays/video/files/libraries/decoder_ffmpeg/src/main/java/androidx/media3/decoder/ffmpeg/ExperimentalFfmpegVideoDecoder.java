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

import static com.google.common.base.Preconditions.checkNotNull;

import android.view.Surface;
import androidx.annotation.Nullable;
import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.decoder.DecoderInputBuffer;
import androidx.media3.decoder.SimpleDecoder;
import androidx.media3.decoder.VideoDecoderOutputBuffer;
import java.nio.ByteBuffer;

/** FFmpeg video decoder. */
@UnstableApi
/* package */ final class ExperimentalFfmpegVideoDecoder
    extends SimpleDecoder<DecoderInputBuffer, VideoDecoderOutputBuffer, FfmpegDecoderException> {

  private static final int VIDEO_DECODER_ERROR_INVALID_DATA = -1;
  private static final int VIDEO_DECODER_ERROR_OTHER = -2;

  private static final int AVERROR_EAGAIN = -11;

  private final String codecName;
  @Nullable private final byte[] extraData;
  private final int threads;
  private final int degree;
  private final int width;
  private final int height;

  private long nativeContext;

  @C.VideoOutputMode private volatile int outputMode;

  public ExperimentalFfmpegVideoDecoder(
      String codecName,
      @Nullable byte[] extraData,
      int threads,
      int degree,
      int width,
      int height,
      int numInputBuffers,
      int numOutputBuffers,
      int initialInputBufferSize)
      throws FfmpegDecoderException {
    super(
        new DecoderInputBuffer[numInputBuffers],
        new VideoDecoderOutputBuffer[numOutputBuffers]);
    if (!FfmpegLibrary.isAvailable()) {
      throw new FfmpegDecoderException("Failed to load decoder native libraries.");
    }
    this.codecName = checkNotNull(codecName);
    this.extraData = extraData;
    this.threads = threads;
    this.degree = degree;
    this.width = width;
    this.height = height;
    this.outputMode = C.VIDEO_OUTPUT_MODE_SURFACE_YUV;

    nativeContext =
        ffmpegVideoInitialize(codecName, extraData, threads, width, height);
    if (nativeContext == 0) {
      throw new FfmpegDecoderException("Video decoder initialization failed.");
    }
    setInitialInputBufferSize(initialInputBufferSize);
  }

  @Override
  public String getName() {
    return "ffmpeg" + FfmpegLibrary.getVersion() + "-" + codecName;
  }

  @Override
  protected DecoderInputBuffer createInputBuffer() {
    return new DecoderInputBuffer(
        DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_DIRECT,
        FfmpegLibrary.getInputBufferPaddingSize());
  }

  @Override
  protected VideoDecoderOutputBuffer createOutputBuffer() {
    return new VideoDecoderOutputBuffer(this::releaseOutputBuffer);
  }

  @Override
  protected FfmpegDecoderException createUnexpectedDecodeException(Throwable error) {
    return new FfmpegDecoderException("Unexpected decode error", error);
  }

  @Override
  @Nullable
  protected FfmpegDecoderException decode(
      DecoderInputBuffer inputBuffer, VideoDecoderOutputBuffer outputBuffer, boolean reset) {
    if (reset) {
      nativeContext = ffmpegVideoReset(nativeContext, extraData);
      if (nativeContext == 0) {
        return new FfmpegDecoderException("Error resetting video decoder (see logcat).");
      }
    }

    ByteBuffer inputData = checkNotNull(inputBuffer.data);
    int inputSize = inputData.limit();
    long pts = inputBuffer.timeUs;

    // Send packet to decoder
    int result =
        ffmpegVideoSendPacket(nativeContext, inputData, inputSize, pts);
    if (result == VIDEO_DECODER_ERROR_OTHER) {
      return new FfmpegDecoderException("Error sending packet to video decoder.");
    } else if (result == VIDEO_DECODER_ERROR_INVALID_DATA) {
      outputBuffer.shouldBeSkipped = true;
      return null;
    } else if (result == AVERROR_EAGAIN) {
      // Decoder needs to be drained before more input can be accepted.
      // Not an error - just skip this input for now.
      outputBuffer.shouldBeSkipped = true;
      return null;
    }

    // Try to receive a decoded frame
    result = ffmpegVideoReceiveFrame(nativeContext);
    if (result == AVERROR_EAGAIN) {
      // No frame ready yet
      outputBuffer.shouldBeSkipped = true;
      return null;
    } else if (result < 0) {
      outputBuffer.shouldBeSkipped = true;
      return null;
    }

    // We have a decoded frame
    int decodedWidth = ffmpegVideoGetWidth(nativeContext);
    int decodedHeight = ffmpegVideoGetHeight(nativeContext);
    long framePts = ffmpegVideoGetPts(nativeContext);

    if (outputMode == C.VIDEO_OUTPUT_MODE_SURFACE_YUV) {
      // Surface mode: store decoder context pointer for later rendering
      outputBuffer.mode = C.VIDEO_OUTPUT_MODE_SURFACE_YUV;
      outputBuffer.decoderPrivate = nativeContext;
    } else {
      // YUV buffer mode: copy frame data to output buffer
      // Calculate buffer sizes
      int ySize = decodedWidth * decodedHeight;
      int uvSize = (decodedWidth / 2) * (decodedHeight / 2);
      int totalSize = ySize + 2 * uvSize;

      ByteBuffer outputData = outputBuffer.init(framePts >= 0 ? framePts : pts, totalSize, null);
      outputData.position(0);
      outputData.limit(totalSize);

      ByteBuffer yPlane = outputBuffer.data.slice();
      yPlane.limit(ySize);

      ByteBuffer uPlane = outputBuffer.data.slice();
      uPlane.position(ySize);
      uPlane.limit(uPlane.position() + uvSize);

      ByteBuffer vPlane = outputBuffer.data.slice();
      vPlane.position(ySize + uvSize);
      vPlane.limit(vPlane.position() + uvSize);

      int copyResult = ffmpegVideoCopyFrameData(nativeContext, yPlane, uPlane, vPlane);
      if (copyResult < 0) {
        outputBuffer.shouldBeSkipped = true;
        return null;
      }

      outputBuffer.width = decodedWidth;
      outputBuffer.height = decodedHeight;
      outputBuffer.mode = C.VIDEO_OUTPUT_MODE_YUV;
      outputBuffer.yuvStrides = new int[3];
      outputBuffer.yuvStrides[0] = decodedWidth;
      outputBuffer.yuvStrides[1] = decodedWidth / 2;
      outputBuffer.yuvStrides[2] = decodedWidth / 2;
      outputBuffer.colorspace = VideoDecoderOutputBuffer.COLORSPACE_BT709;

      // Set up YUV planes in the output buffer
      outputBuffer.yuvPlanes = new ByteBuffer[3];
      outputBuffer.yuvPlanes[0] = yPlane;
      outputBuffer.yuvPlanes[1] = uPlane;
      outputBuffer.yuvPlanes[2] = vPlane;
      outputBuffer.yStride = decodedWidth;
      outputBuffer.uvStride = decodedWidth / 2;
    }

    outputBuffer.timeUs = framePts >= 0 ? framePts : pts;
    return null;
  }

  @Override
  public void release() {
    super.release();
    ffmpegVideoRelease(nativeContext);
    nativeContext = 0;
  }

  /** Sets the output mode. */
  public void setOutputMode(@C.VideoOutputMode int outputMode) {
    this.outputMode = outputMode;
  }

  /** Returns whether the decoder can be reused for the given format. */
  public boolean canReuseDecoder(String codecName) {
    return this.codecName.equals(codecName);
  }

  // Native methods
  // ffmpegVideoRenderFrame is package-private so ExperimentalFfmpegVideoRenderer can call it.

  private native long ffmpegVideoInitialize(
      String codecName,
      @Nullable byte[] extraData,
      int threads,
      int width,
      int height);

  private native int ffmpegVideoSendPacket(
      long context,
      ByteBuffer data,
      int size,
      long pts);

  private native int ffmpegVideoReceiveFrame(long context);

  /* package */ static native int ffmpegVideoRenderFrame(long context, Surface surface);

  private native int ffmpegVideoGetWidth(long context);

  private native int ffmpegVideoGetHeight(long context);

  private native long ffmpegVideoGetPts(long context);

  private native int ffmpegVideoCopyFrameData(
      long context,
      ByteBuffer yBuffer,
      ByteBuffer uBuffer,
      ByteBuffer vBuffer);

  private native long ffmpegVideoReset(long context, @Nullable byte[] extraData);

  private native void ffmpegVideoRelease(long context);
}
