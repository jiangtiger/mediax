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
    int sendResult =
        ffmpegVideoSendPacket(nativeContext, inputData, inputSize, pts);
    if (sendResult == VIDEO_DECODER_ERROR_OTHER) {
      return new FfmpegDecoderException("Error sending packet to video decoder.");
    } else if (sendResult == VIDEO_DECODER_ERROR_INVALID_DATA) {
      outputBuffer.shouldBeSkipped = true;
      return null;
    }
    // If sendResult == AVERROR_EAGAIN, the decoder's internal buffer is full.
    // We still try to receive a frame below to drain the decoder.
    // If sendResult == 0, the packet was accepted; we try to receive a frame.

    // Try to receive a decoded frame
    int recvResult = ffmpegVideoReceiveFrame(nativeContext);
    if (recvResult == AVERROR_EAGAIN) {
      // No frame ready yet — skip this output
      outputBuffer.shouldBeSkipped = true;
      return null;
    } else if (recvResult < 0) {
      outputBuffer.shouldBeSkipped = true;
      return null;
    }

    // We have a decoded frame — get per-frame handle (av_frame_ref)
    long frameHandle = ffmpegVideoGetFrameHandle(nativeContext);
    if (frameHandle == 0) {
      outputBuffer.shouldBeSkipped = true;
      return null;
    }

    // Get frame metadata
    int decodedWidth = ffmpegVideoGetWidth(nativeContext);
    int decodedHeight = ffmpegVideoGetHeight(nativeContext);
    long framePts = ffmpegVideoGetPts(nativeContext);
    long outputPts = framePts >= 0 ? framePts : pts;

    if (outputMode == C.VIDEO_OUTPUT_MODE_SURFACE_YUV) {
      // Surface mode: store per-frame handle for later rendering.
      // Each output buffer gets its own AVFrame reference, so frames are
      // not overwritten by subsequent decode() calls.
      outputBuffer.mode = C.VIDEO_OUTPUT_MODE_SURFACE_YUV;
      outputBuffer.decoderPrivate = frameHandle;
      outputBuffer.width = decodedWidth;
      outputBuffer.height = decodedHeight;
      outputBuffer.timeUs = outputPts;
      outputBuffer.format = inputBuffer.format;
    } else {
      // YUV buffer mode: copy frame data to output buffer
      int ySize = decodedWidth * decodedHeight;
      int uvSize = (decodedWidth / 2) * (decodedHeight / 2);
      int totalSize = ySize + 2 * uvSize;

      outputBuffer.init(outputPts, 0, null);

      ByteBuffer outputData = ByteBuffer.allocateDirect(totalSize);
      outputBuffer.data = outputData;

      ByteBuffer yPlane = outputData.slice();
      yPlane.limit(ySize);

      ByteBuffer uPlane = outputData.slice();
      uPlane.position(ySize);
      uPlane.limit(uPlane.position() + uvSize);

      ByteBuffer vPlane = outputData.slice();
      vPlane.position(ySize + uvSize);
      vPlane.limit(vPlane.position() + uvSize);

      int copyResult = ffmpegVideoCopyFrameData(nativeContext, yPlane, uPlane, vPlane);
      if (copyResult < 0) {
        outputBuffer.shouldBeSkipped = true;
        ffmpegVideoReleaseFrame(frameHandle);
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

      outputBuffer.yuvPlanes = new ByteBuffer[3];
      outputBuffer.yuvPlanes[0] = yPlane;
      outputBuffer.yuvPlanes[1] = uPlane;
      outputBuffer.yuvPlanes[2] = vPlane;
      outputBuffer.yStride = decodedWidth;
      outputBuffer.uvStride = decodedWidth / 2;

      // Release the per-frame handle since we copied the data
      ffmpegVideoReleaseFrame(frameHandle);
    }

    outputBuffer.timeUs = outputPts;
    return null;
  }

  /** Renders the outputBuffer to the surface. Used with SURFACE_YUV mode only. */
  public void renderToSurface(VideoDecoderOutputBuffer outputBuffer, Surface surface)
      throws FfmpegDecoderException {
    long frameHandle = outputBuffer.decoderPrivate;
    if (frameHandle == 0) {
      throw new FfmpegDecoderException("No frame handle in output buffer for surface rendering.");
    }
    int result = ffmpegVideoRenderFrame(nativeContext, frameHandle, surface);
    if (result < 0) {
      throw new FfmpegDecoderException("Failed to render video frame to surface.");
    }
  }

  @Override
  protected void releaseOutputBuffer(VideoDecoderOutputBuffer outputBuffer) {
    // Release per-frame AVFrame reference if in SURFACE_YUV mode
    if (outputBuffer.decoderPrivate != 0) {
      ffmpegVideoReleaseFrame(outputBuffer.decoderPrivate);
      outputBuffer.decoderPrivate = 0;
    }
    super.releaseOutputBuffer(outputBuffer);
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

  /** Creates an AVFrame reference for the current decoded frame. */
  private native long ffmpegVideoGetFrameHandle(long context);

  /** Renders a per-frame AVFrame to a Surface. */
  /* package */ static native int ffmpegVideoRenderFrame(
      long context, long frameHandle, Surface surface);

  /** Releases a per-frame AVFrame reference. */
  /* package */ static native void ffmpegVideoReleaseFrame(long frameHandle);

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
