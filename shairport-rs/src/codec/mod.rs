/// Unified audio decoder interface.
///
/// Supports ALAC via the integrated Hammerton decoder.
/// When the `aac` feature is enabled, supports AAC via symphonia.
/// When the `ffmpeg` feature is enabled, also supports AAC and resampling.
use crate::decoder::AlacDecoder;

/// Supported audio formats.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AudioFormat {
    Alac44100S16Stereo,
    Alac48000S24Stereo,
    Aac44100F24Stereo,
    Aac48000F24Stereo,
    Aac48000F24_5_1,
    Aac48000F24_7_1,
}

impl AudioFormat {
    pub fn sample_rate(&self) -> u32 {
        match self {
            Self::Alac44100S16Stereo | Self::Aac44100F24Stereo => 44100,
            _ => 48000,
        }
    }

    pub fn channels(&self) -> u16 {
        match self {
            Self::Aac48000F24_5_1 => 6,
            Self::Aac48000F24_7_1 => 8,
            _ => 2,
        }
    }

    pub fn frames_per_packet(&self) -> usize {
        match self {
            Self::Alac44100S16Stereo | Self::Alac48000S24Stereo => 352,
            _ => 1024,
        }
    }

    /// Detect format from AP2 SSRC value.
    pub fn from_ssrc(ssrc: u32) -> Option<Self> {
        match ssrc {
            0x0000_FACE => Some(Self::Alac44100S16Stereo),
            0x1500_0000 => Some(Self::Alac48000S24Stereo),
            0x1600_0000 => Some(Self::Aac44100F24Stereo),
            0x1700_0000 => Some(Self::Aac48000F24Stereo),
            0x2700_0000 => Some(Self::Aac48000F24_5_1),
            0x2800_0000 => Some(Self::Aac48000F24_7_1),
            _ => None,
        }
    }

    /// Detect format from the AP2 `audioFormat` setup bitmask.
    pub fn from_ap2_audio_format(audio_format: u64) -> Option<Self> {
        match audio_format {
            0x0004_0000 => Some(Self::Alac44100S16Stereo),
            0x0020_0000 => Some(Self::Alac48000S24Stereo),
            0x0040_0000 => Some(Self::Aac44100F24Stereo),
            0x0080_0000 => Some(Self::Aac48000F24Stereo),
            _ => None,
        }
    }

    pub fn description(&self) -> &'static str {
        match self {
            Self::Alac44100S16Stereo => "ALAC/44100/S16_LE/2",
            Self::Alac48000S24Stereo => "ALAC/48000/S24_LE/2",
            Self::Aac44100F24Stereo => "AAC/44100/F24/2",
            Self::Aac48000F24Stereo => "AAC/48000/F24/2",
            Self::Aac48000F24_5_1 => "AAC/48000/F24/5.1",
            Self::Aac48000F24_7_1 => "AAC/48000/F24/7.1",
        }
    }
}

/// Result of decoding one audio frame.
pub struct DecodedFrame {
    pub samples: Vec<f32>,
    pub sample_rate: u32,
    pub channels: u16,
}

/// Audio decoder that dispatches to the right codec.
pub enum AudioDecoder {
    Alac(AlacDecoder),
    #[cfg(feature = "ffmpeg")]
    Ffmpeg(Box<FfmpegDecoder>),
    #[cfg(feature = "aac")]
    SymphoniaAac(SymphoniaAacDecoder),
    Unsupported,
}

impl AudioDecoder {
    pub fn new_alac(
        sample_size: u32,
        channels: u16,
        magic_cookie: &[u8],
    ) -> Result<Self, &'static str> {
        AlacDecoder::new(sample_size, channels, magic_cookie).map(Self::Alac)
    }

    #[cfg(feature = "aac")]
    pub fn new_aac(format: AudioFormat) -> Self {
        match SymphoniaAacDecoder::new(format) {
            Ok(dec) => Self::SymphoniaAac(dec),
            Err(_) => Self::Unsupported,
        }
    }

    #[cfg(not(any(feature = "aac", feature = "ffmpeg")))]
    pub fn new_aac(_format: AudioFormat) -> Self {
        Self::Unsupported
    }

    #[cfg(feature = "ffmpeg")]
    pub fn new_aac_ffmpeg(format: AudioFormat) -> Self {
        Self::Ffmpeg(Box::new(FfmpegDecoder::new(format)))
    }

    pub fn decode(
        &mut self,
        input: &[u8],
        format: AudioFormat,
    ) -> Result<DecodedFrame, &'static str> {
        match self {
            Self::Alac(dec) => {
                let raw = dec.decode_frame(input)?;
                let float_samples: Vec<f32> =
                    raw.iter().map(|&s| (s as f32) / 2147483648.0).collect();
                Ok(DecodedFrame {
                    samples: float_samples,
                    sample_rate: format.sample_rate(),
                    channels: format.channels(),
                })
            }
            #[cfg(feature = "ffmpeg")]
            Self::Ffmpeg(dec) => dec.decode(input, format),
            #[cfg(feature = "aac")]
            Self::SymphoniaAac(dec) => dec.decode(input),
            Self::Unsupported => Err("no decoder available for this format"),
        }
    }
}

/// FFmpeg-based decoder (available with `ffmpeg` feature).
#[cfg(feature = "ffmpeg")]
pub struct FfmpegDecoder {
    _codec_ctx: *mut std::ffi::c_void,
}

#[cfg(feature = "ffmpeg")]
impl FfmpegDecoder {
    pub fn new(_format: AudioFormat) -> Self {
        // TODO: Initialize FFmpeg codec context
        Self {
            _codec_ctx: std::ptr::null_mut(),
        }
    }

    pub fn decode(
        &mut self,
        _input: &[u8],
        _format: AudioFormat,
    ) -> Result<DecodedFrame, &'static str> {
        Err("FFmpeg decoder not yet implemented")
    }
}

/// Pure-Rust AAC decoder using symphonia (available with `aac` feature).
#[cfg(feature = "aac")]
pub struct SymphoniaAacDecoder {
    sample_rate: u32,
    channels: u16,
}

#[cfg(feature = "aac")]
impl SymphoniaAacDecoder {
    pub fn new(format: AudioFormat) -> Result<Self, &'static str> {
        Ok(Self {
            sample_rate: format.sample_rate(),
            channels: format.channels(),
        })
    }

    /// Decode a raw AAC elementary stream frame.
    /// Decodes in-place: input is consumed and output is collected.
    pub fn decode(&self, input: &[u8]) -> Result<DecodedFrame, &'static str> {
        decode_aac_frame(input, self.sample_rate, self.channels)
    }
}

#[cfg(feature = "aac")]
fn decode_aac_frame(
    input: &[u8],
    sample_rate: u32,
    channels: u16,
) -> Result<DecodedFrame, &'static str> {
    use symphonia::core::audio::AudioBufferRef;
    use symphonia::core::codecs::DecoderOptions;
    use symphonia::core::io::MediaSourceStream;
    use symphonia::core::meta::MetadataOptions;
    use symphonia::core::probe::Hint;

    if input.is_empty() {
        return Err("empty AAC frame");
    }

    // Wrap raw AAC elementary stream in an ADTS header
    let aac_frame_len = input.len() + 7;
    if aac_frame_len > 0x1FFF {
        return Err("AAC frame too large for ADTS");
    }
    let freq_idx = match sample_rate {
        96000 => 0,
        88200 => 1,
        64000 => 2,
        48000 => 3,
        44100 => 4,
        32000 => 5,
        24000 => 6,
        22050 => 7,
        16000 => 8,
        12000 => 9,
        11025 => 10,
        8000 => 11,
        7350 => 12,
        _ => 3,
    };
    let ch_conf = match channels {
        1 => 1,
        2 => 2,
        6 => 6,
        8 => 7,
        _ => 2,
    };

    let mut adts = vec![0u8; aac_frame_len];
    adts[0] = 0xFF;
    adts[1] = 0xF1;
    adts[2] = (1 << 6) | (freq_idx << 2) | ((ch_conf >> 2) & 0x01);
    adts[3] = ((ch_conf & 0x03) << 6) | (((aac_frame_len >> 11) as u8) & 0x03);
    adts[4] = ((aac_frame_len >> 3) as u8) & 0xFF;
    adts[5] = (((aac_frame_len & 0x07) << 5) as u8) | 0x1F;
    adts[6] = 0xFC;
    adts[7..].copy_from_slice(input);

    let source = MediaSourceStream::new(Box::new(std::io::Cursor::new(adts)), Default::default());
    let mut hint = Hint::new();
    hint.with_extension("aac");

    let format_opts = symphonia::core::formats::FormatOptions::default();
    let metadata_opts = MetadataOptions::default();
    let probe_result = symphonia::default::get_probe()
        .format(&hint, source, &format_opts, &metadata_opts)
        .map_err(|_| "symphonia: failed to probe AAC format")?;

    let track = probe_result
        .format
        .tracks()
        .first()
        .ok_or("symphonia: no tracks")?
        .clone();

    let decode_opts = DecoderOptions::default();
    let mut decoder = symphonia::default::get_codecs()
        .make(&track.codec_params, &decode_opts)
        .map_err(|_| "symphonia: failed to create AAC decoder")?;

    let mut format = probe_result.format;
    let packet = match format.next_packet() {
        Ok(p) => p,
        Err(_) => return Err("symphonia: failed to read AAC packet"),
    };

    let decoded = decoder
        .decode(&packet)
        .map_err(|_| "symphonia: AAC decode failed")?;

    // Convert to interleaved f32
    let samples = match decoded {
        AudioBufferRef::F32(buf) => {
            let planes = buf.planes();
            let frames = planes.planes()[0].len();
            let num_planes = planes.planes().len();
            let mut out = Vec::with_capacity(frames * num_planes);
            for f in 0..frames {
                for ch in 0..num_planes {
                    out.push(planes.planes()[ch][f]);
                }
            }
            out
        }
        AudioBufferRef::S16(buf) => {
            let planes = buf.planes();
            let frames = planes.planes()[0].len();
            let num_planes = planes.planes().len();
            let mut out = Vec::with_capacity(frames * num_planes);
            for f in 0..frames {
                for ch in 0..num_planes {
                    out.push(planes.planes()[ch][f] as f32 / 32768.0);
                }
            }
            out
        }
        _ => return Err("symphonia: unsupported sample format"),
    };

    Ok(DecodedFrame {
        samples,
        sample_rate,
        channels,
    })
}

/// Convert decoded multi-channel float samples to stereo (simple mixdown).
pub fn mixdown_to_stereo(samples: &[f32], input_channels: u16) -> Vec<f32> {
    if input_channels <= 2 {
        return samples.to_vec();
    }
    let frames = samples.len() / input_channels as usize;
    let mut stereo = Vec::with_capacity(frames * 2);
    for frame in 0..frames {
        let offset = frame * input_channels as usize;
        // Simple mix: FL/FR for stereo, mix center into both, mix LFE, spread surrounds
        let fl = samples[offset];
        let fr = samples.get(offset + 1).copied().unwrap_or(0.0);
        let center = samples.get(offset + 2).copied().unwrap_or(0.0);
        let lfe = samples.get(offset + 3).copied().unwrap_or(0.0);
        let bl = if input_channels >= 6 {
            samples.get(offset + 4).copied().unwrap_or(0.0)
        } else {
            0.0
        };
        let br = if input_channels >= 6 {
            samples.get(offset + 5).copied().unwrap_or(0.0)
        } else {
            0.0
        };

        let l = fl + center * 0.5 + lfe * 0.3 + bl * 0.5;
        let r = fr + center * 0.5 + lfe * 0.3 + br * 0.5;
        stereo.push(l);
        stereo.push(r);
    }
    stereo
}

/// Sample rate conversion (linear interpolation, simplistic).
pub fn resample(input: &[f32], input_rate: u32, output_rate: u32, channels: u16) -> Vec<f32> {
    if input_rate == output_rate || input_rate == 0 || output_rate == 0 {
        return input.to_vec();
    }
    let ratio = output_rate as f64 / input_rate as f64;
    let input_frames = input.len() / channels as usize;
    let output_frames = (input_frames as f64 * ratio) as usize;
    let mut output = vec![0.0f32; output_frames * channels as usize];

    for out_frame in 0..output_frames {
        let in_frame_f = out_frame as f64 / ratio;
        let in_frame = in_frame_f as usize;
        let frac = in_frame_f - in_frame as f64;
        let next_in = (in_frame + 1).min(input_frames.saturating_sub(1));

        for ch in 0..channels as usize {
            let in_idx = in_frame * channels as usize + ch;
            let next_idx = next_in * channels as usize + ch;
            let out_idx = out_frame * channels as usize + ch;
            let a = input.get(in_idx).copied().unwrap_or(0.0);
            let b = input.get(next_idx).copied().unwrap_or(0.0);
            output[out_idx] = a + (b - a) * frac as f32;
        }
    }
    output
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn audio_format_from_ssrc() {
        assert_eq!(
            AudioFormat::from_ssrc(0x0000_FACE),
            Some(AudioFormat::Alac44100S16Stereo)
        );
        assert_eq!(
            AudioFormat::from_ssrc(0x1500_0000),
            Some(AudioFormat::Alac48000S24Stereo)
        );
        assert_eq!(
            AudioFormat::from_ssrc(0x1600_0000),
            Some(AudioFormat::Aac44100F24Stereo)
        );
        assert_eq!(
            AudioFormat::from_ssrc(0x1700_0000),
            Some(AudioFormat::Aac48000F24Stereo)
        );
        assert_eq!(AudioFormat::from_ssrc(0x99999999), None);
    }

    #[test]
    fn audio_format_from_ap2_setup_format() {
        assert_eq!(
            AudioFormat::from_ap2_audio_format(0x0080_0000),
            Some(AudioFormat::Aac48000F24Stereo)
        );
        assert_eq!(AudioFormat::from_ap2_audio_format(0x0000_0001), None);
    }

    #[test]
    fn mixdown_stereo_passthrough() {
        let input = vec![0.5, -0.5, 0.3, -0.3];
        let output = mixdown_to_stereo(&input, 2);
        assert_eq!(output, input);
    }

    #[test]
    fn mixdown_5_1_to_stereo() {
        // FL, FR, C, LFE, BL, BR
        let input = vec![1.0, -1.0, 0.5, 0.3, 0.2, -0.2];
        let output = mixdown_to_stereo(&input, 6);
        assert_eq!(output.len(), 2);
        // L = 1.0 + 0.5*0.5 + 0.3*0.3 + 0.2*0.5 = 1.0 + 0.25 + 0.09 + 0.10 = 1.44
        assert!((output[0] - 1.44).abs() < 0.01);
    }

    #[test]
    fn resample_same_rate_passthrough() {
        let input = vec![0.5, -0.5, 0.3, -0.3];
        let output = resample(&input, 44100, 44100, 2);
        assert_eq!(output, input);
    }

    #[test]
    fn resample_changes_length() {
        let input = vec![0.0; 100];
        let output = resample(&input, 44100, 48000, 2);
        // 100 samples at 44100 Hz = 50 frames
        // At 48000 Hz, 50 frames = 50 * 48000/44100 ≈ 54 frames = 108 samples
        assert!(output.len() > 100);
    }
}
