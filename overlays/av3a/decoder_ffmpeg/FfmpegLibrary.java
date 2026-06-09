/*
 * mediax overlay: AV3A (Audio Vivid / AVS3-P3) MIME → libarcdav3a 映射。
 * 应用至 media/libraries/decoder_ffmpeg/.../FfmpegLibrary.java（在 upstream getCodecName 中追加 case）。
 */
// 在 getCodecName(String mimeType) 的 switch 中、default 之前追加：
//
//     case "audio/av3a":
//       return "libarcdav3a";
//
// 说明：官方 MimeTypes 1.8.x 尚无 AUDIO_AV3A 常量，故使用字面量，与 vstv Av3aMimeTypes.AUDIO_AV3A 一致。
