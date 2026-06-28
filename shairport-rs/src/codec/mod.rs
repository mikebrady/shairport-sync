/// Unified audio decoder interface backed by Symphonia.
use anyhow::{Context, anyhow};
use rubato::audioadapter_buffers::direct::InterleavedSlice;
use rubato::{
    Async, FixedAsync, Resampler, SincInterpolationParameters, SincInterpolationType,
    WindowFunction, calculate_cutoff,
};
use symphonia::core::{
    audio::{AudioBufferRef, Layout, SampleBuffer},
    codecs::{CODEC_TYPE_AAC, CODEC_TYPE_ALAC, CodecParameters, Decoder, DecoderOptions},
    formats::Packet,
};

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

    pub fn bits_per_sample(&self) -> u32 {
        match self {
            Self::Alac44100S16Stereo => 16,
            _ => 24,
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

    pub fn is_alac(&self) -> bool {
        matches!(self, Self::Alac44100S16Stereo | Self::Alac48000S24Stereo)
    }

    pub fn is_playable(&self) -> bool {
        !matches!(self, Self::Aac48000F24_5_1 | Self::Aac48000F24_7_1)
    }
}

/// Result of decoding one audio frame.
pub struct DecodedFrame {
    pub samples: Vec<f32>,
    pub sample_rate: u32,
    pub channels: u16,
}

/// Stateful Symphonia decoder for one AirPlay audio format.
pub struct AudioDecoder {
    decoder: Box<dyn Decoder>,
    format: AudioFormat,
    frames_per_packet: usize,
    next_ts: u64,
}

impl AudioDecoder {
    pub fn new_for_format(
        format: AudioFormat,
        magic_cookie: Option<&[u8]>,
    ) -> anyhow::Result<Self> {
        if !format.is_playable() {
            return Err(anyhow!(
                "unsupported playback format {}",
                format.description()
            ));
        }

        if format.is_alac() {
            let cookie = magic_cookie.context("ALAC magic cookie is required")?;
            return Self::new_alac(
                format.bits_per_sample(),
                format.channels(),
                format.sample_rate(),
                format.frames_per_packet(),
                cookie,
            );
        }

        Self::new_aac(format)
    }

    pub fn new_alac(
        sample_size: u32,
        channels: u16,
        sample_rate: u32,
        frames_per_packet: usize,
        magic_cookie: &[u8],
    ) -> anyhow::Result<Self> {
        if magic_cookie.len() < 24 {
            return Err(anyhow!("ALAC magic cookie too short"));
        }

        let mut params = CodecParameters::new();
        params
            .for_codec(CODEC_TYPE_ALAC)
            .with_sample_rate(sample_rate)
            .with_bits_per_sample(sample_size)
            .with_bits_per_coded_sample(sample_size)
            .with_max_frames_per_packet(frames_per_packet as u64)
            .with_extra_data(Box::from(magic_cookie));
        apply_channel_layout(&mut params, channels)?;

        let format = match (sample_rate, sample_size) {
            (48_000, 24) => AudioFormat::Alac48000S24Stereo,
            _ => AudioFormat::Alac44100S16Stereo,
        };
        Self::from_params(format, frames_per_packet, params)
    }

    pub fn new_aac(format: AudioFormat) -> anyhow::Result<Self> {
        if !matches!(
            format,
            AudioFormat::Aac44100F24Stereo | AudioFormat::Aac48000F24Stereo
        ) {
            return Err(anyhow!(
                "unsupported AAC playback format {}",
                format.description()
            ));
        }

        let mut params = CodecParameters::new();
        params
            .for_codec(CODEC_TYPE_AAC)
            .with_sample_rate(format.sample_rate())
            .with_bits_per_sample(format.bits_per_sample())
            .with_max_frames_per_packet(format.frames_per_packet() as u64);
        apply_channel_layout(&mut params, format.channels())?;

        Self::from_params(format, format.frames_per_packet(), params)
    }

    fn from_params(
        format: AudioFormat,
        frames_per_packet: usize,
        params: CodecParameters,
    ) -> anyhow::Result<Self> {
        let decoder = symphonia::default::get_codecs()
            .make(&params, &DecoderOptions::default())
            .with_context(|| format!("failed to create decoder for {}", format.description()))?;
        Ok(Self {
            decoder,
            format,
            frames_per_packet,
            next_ts: 0,
        })
    }

    pub fn decode(&mut self, input: &[u8]) -> anyhow::Result<DecodedFrame> {
        if input.is_empty() {
            return Err(anyhow!("empty audio packet"));
        }

        let duration = self.frames_per_packet as u64;
        let packet = Packet::new_from_slice(0, self.next_ts, duration, input);
        self.next_ts = self.next_ts.wrapping_add(duration);

        let decoded = self
            .decoder
            .decode(&packet)
            .with_context(|| format!("Symphonia failed to decode {}", self.format.description()))?;
        interleaved_f32(decoded)
    }
}

fn apply_channel_layout(params: &mut CodecParameters, channels: u16) -> anyhow::Result<()> {
    let layout = match channels {
        1 => Layout::Mono,
        2 => Layout::Stereo,
        6 => Layout::FivePointOne,
        _ => return Err(anyhow!("unsupported channel count {channels}")),
    };
    params.with_channel_layout(layout);
    Ok(())
}

fn interleaved_f32(decoded: AudioBufferRef<'_>) -> anyhow::Result<DecodedFrame> {
    let spec = *decoded.spec();
    let frames = decoded.frames();
    let mut samples = SampleBuffer::<f32>::new(frames as u64, spec);
    samples.copy_interleaved_ref(decoded);
    Ok(DecodedFrame {
        samples: samples.samples().to_vec(),
        sample_rate: spec.rate,
        channels: spec.channels.count() as u16,
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

/// Sample rate conversion cache.
pub struct ResamplerCache {
    cached_input_rate: u32,
    cached_output_rate: u32,
    cached_channels: u16,
    cached_input_frames: usize,
    pending_output_frames: f64,
    resampler: Option<Async<f32>>,
}

impl ResamplerCache {
    pub fn new() -> Self {
        Self {
            cached_input_rate: 0,
            cached_output_rate: 0,
            cached_channels: 0,
            cached_input_frames: 0,
            pending_output_frames: 0.0,
            resampler: None,
        }
    }

    pub fn resample(
        &mut self,
        input: &[f32],
        input_rate: u32,
        output_rate: u32,
        channels: u16,
    ) -> Vec<f32> {
        if input_rate == output_rate || input_rate == 0 || output_rate == 0 {
            return input.to_vec();
        }
        let ch = channels.max(1) as usize;
        let input_frames = input.len() / ch;
        if input_frames == 0 {
            return Vec::new();
        }

        if self.cached_input_rate != input_rate
            || self.cached_output_rate != output_rate
            || self.cached_channels as usize != ch
            || self.cached_input_frames != input_frames
        {
            self.cached_input_rate = input_rate;
            self.cached_output_rate = output_rate;
            self.cached_channels = channels;
            self.cached_input_frames = input_frames;
            self.pending_output_frames = 0.0;
            self.resampler = Self::build_resampler(input_rate, output_rate, ch, input_frames);
        }

        let expected_frames = self.expected_output_frames(input_frames, input_rate, output_rate);
        let expected_samples = expected_frames * ch;
        let output = self
            .resampler
            .as_mut()
            .and_then(|resampler| resample_with_rubato(resampler, input, ch, input_frames))
            .unwrap_or_else(|| linear_resample(input, input_rate, output_rate, channels));
        normalize_resampled_len(output, expected_samples, ch)
    }

    fn build_resampler(
        input_rate: u32,
        output_rate: u32,
        channels: usize,
        input_frames: usize,
    ) -> Option<Async<f32>> {
        let ratio = output_rate as f64 / input_rate as f64;
        let window = WindowFunction::BlackmanHarris2;
        let sinc_len = 64;
        let params = SincInterpolationParameters {
            sinc_len,
            f_cutoff: calculate_cutoff::<f32>(sinc_len, window),
            interpolation: SincInterpolationType::Linear,
            oversampling_factor: 128,
            window,
        };

        Async::<f32>::new_sinc(
            ratio,
            1.1,
            &params,
            input_frames,
            channels,
            FixedAsync::Input,
        )
        .ok()
    }

    fn expected_output_frames(
        &mut self,
        input_frames: usize,
        input_rate: u32,
        output_rate: u32,
    ) -> usize {
        let exact = input_frames as f64 * output_rate as f64 / input_rate as f64
            + self.pending_output_frames;
        let frames = exact.floor().max(1.0) as usize;
        self.pending_output_frames = exact - frames as f64;
        frames
    }
}

impl Default for ResamplerCache {
    fn default() -> Self {
        Self::new()
    }
}

fn resample_with_rubato(
    resampler: &mut Async<f32>,
    input: &[f32],
    channels: usize,
    input_frames: usize,
) -> Option<Vec<f32>> {
    let input = InterleavedSlice::new(input, channels, input_frames).ok()?;
    resampler
        .process(&input, 0, None)
        .ok()
        .map(|output| output.take_data())
}

fn normalize_resampled_len(
    mut output: Vec<f32>,
    expected_samples: usize,
    channels: usize,
) -> Vec<f32> {
    if expected_samples == 0 || output.len() == expected_samples {
        return output;
    }

    if output.len() > expected_samples {
        output.truncate(expected_samples);
        return output;
    }

    let first_pad = output.len().saturating_sub(channels).min(output.len());
    let last_frame: Vec<f32> = output
        .get(first_pad..first_pad + channels.min(output.len() - first_pad))
        .unwrap_or(&[])
        .to_vec();

    while output.len() < expected_samples {
        for ch in 0..channels {
            let sample = last_frame.get(ch).copied().unwrap_or(0.0);
            output.push(sample);
            if output.len() == expected_samples {
                break;
            }
        }
    }
    output
}

/// Fallback sample rate conversion (linear interpolation).
fn linear_resample(input: &[f32], input_rate: u32, output_rate: u32, channels: u16) -> Vec<f32> {
    if input_rate == output_rate || input_rate == 0 || output_rate == 0 {
        return input.to_vec();
    }
    let ratio = output_rate as f64 / input_rate as f64;
    let input_frames = input.len() / channels.max(1) as usize;
    if input_frames == 0 {
        return Vec::new();
    }

    let output_frames = ((input_frames as f64 * ratio) as usize)
        .max(1)
        .min(1_000_000);
    let mut output = vec![0.0f32; output_frames * channels as usize];

    for out_frame in 0..output_frames {
        let in_frame_f = out_frame as f64 / ratio;
        let in_frame = in_frame_f as usize;
        let frac = in_frame_f - in_frame as f64;
        let next_in = (in_frame + 1).min(input_frames - 1);

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
    fn surround_formats_are_recognized_but_not_playable() {
        assert_eq!(
            AudioFormat::from_ssrc(0x2700_0000),
            Some(AudioFormat::Aac48000F24_5_1)
        );
        assert_eq!(
            AudioFormat::from_ssrc(0x2800_0000),
            Some(AudioFormat::Aac48000F24_7_1)
        );
        assert!(!AudioFormat::Aac48000F24_5_1.is_playable());
        assert!(!AudioFormat::Aac48000F24_7_1.is_playable());
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
        let output = linear_resample(&input, 44100, 44100, 2);
        assert_eq!(output, input);
    }

    #[test]
    fn resample_changes_length() {
        let input = vec![0.0; 100];
        let output = linear_resample(&input, 44100, 48000, 2);
        // 100 samples at 44100 Hz = 50 frames
        // At 48000 Hz, 50 frames = 50 * 48000/44100 ≈ 54 frames = 108 samples
        assert!(output.len() > 100);
    }

    #[test]
    fn resampler_cache_doubles_48k_stereo_to_96k() {
        let mut cache = ResamplerCache::new();
        let input = vec![0.0; 2048];
        let output = cache.resample(&input, 48_000, 96_000, 2);
        assert_eq!(output.len(), 4096);
    }

    #[test]
    fn constructs_stereo_symphonia_decoders() {
        let alac_441 = alac_specific_config(44_100, 16);
        let alac_480 = alac_specific_config(48_000, 24);

        assert!(AudioDecoder::new_for_format(AudioFormat::Aac44100F24Stereo, None).is_ok());
        assert!(AudioDecoder::new_for_format(AudioFormat::Aac48000F24Stereo, None).is_ok());
        assert!(
            AudioDecoder::new_for_format(AudioFormat::Alac44100S16Stereo, Some(&alac_441)).is_ok()
        );
        assert!(
            AudioDecoder::new_for_format(AudioFormat::Alac48000S24Stereo, Some(&alac_480)).is_ok()
        );
        assert!(AudioDecoder::new_for_format(AudioFormat::Aac48000F24_5_1, None).is_err());
    }

    #[test]
    fn decoder_rejects_empty_packet() {
        let mut decoder = AudioDecoder::new_for_format(AudioFormat::Aac44100F24Stereo, None)
            .expect("AAC decoder should construct");
        assert!(decoder.decode(&[]).is_err());
    }

    fn alac_specific_config(sample_rate: u32, sample_size: u32) -> [u8; 24] {
        let mut config = [0u8; 24];
        config[0..4].copy_from_slice(&352u32.to_be_bytes());
        config[4] = 0;
        config[5] = sample_size as u8;
        config[6] = 40;
        config[7] = 10;
        config[8] = 14;
        config[9] = 2;
        config[10..12].copy_from_slice(&255u16.to_be_bytes());
        config[12..16].copy_from_slice(&0u32.to_be_bytes());
        config[16..20].copy_from_slice(&0u32.to_be_bytes());
        config[20..24].copy_from_slice(&sample_rate.to_be_bytes());
        config
    }
}
