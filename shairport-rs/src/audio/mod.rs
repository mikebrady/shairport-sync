use anyhow::Context;
use cpal::{
    SampleFormat, Stream, SupportedStreamConfig,
    traits::{DeviceTrait, HostTrait, StreamTrait},
};
use parking_lot::Mutex;
use ringbuf::{
    HeapRb,
    traits::{Consumer, Observer, Producer, Split},
};
use serde::{Deserialize, Serialize};
use std::sync::Arc;

use crate::codec;
use crate::config::{AudioConfig, AudioHostName};

#[derive(Clone)]
pub struct AudioManager {
    config: AudioConfig,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct AudioDevice {
    pub id: String,
    pub name: String,
    pub host: String,
    pub is_default: bool,
    pub supported_output_configs: Vec<String>,
}

#[derive(Debug, Deserialize)]
pub struct SelectAudioDeviceRequest {
    pub device_id: Option<String>,
}

#[allow(dead_code)]
#[derive(Clone)]
pub struct AudioEngine {
    producer: Arc<Mutex<ringbuf::HeapProd<f32>>>,
    consumer: Arc<Mutex<ringbuf::HeapCons<f32>>>,
    output_format: Arc<Mutex<AudioOutputFormat>>,
}

pub struct AudioOutput {
    _stream: Stream,
    pub sample_rate: u32,
    pub channels: u16,
    pub sample_format: SampleFormat,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AudioEngineStatus {
    pub queued_samples: usize,
    pub capacity_samples: usize,
    pub output_sample_rate: u32,
    pub output_channels: u16,
}

#[derive(Clone, Copy, Debug)]
struct AudioOutputFormat {
    sample_rate: u32,
    channels: u16,
}

impl AudioManager {
    pub fn new(config: AudioConfig) -> Self {
        Self { config }
    }

    #[allow(deprecated)]
    pub fn list_devices(&self) -> Vec<AudioDevice> {
        let hosts = match self.config.host {
            AudioHostName::Default => cpal::available_hosts(),
            host => vec![host_to_cpal(host)].into_iter().flatten().collect(),
        };

        hosts
            .into_iter()
            .filter_map(|host_id| cpal::host_from_id(host_id).ok().map(|host| (host_id, host)))
            .flat_map(|(host_id, host)| {
                let default_name = host.default_output_device().and_then(|d| d.name().ok());
                host.output_devices()
                    .map(|devices| {
                        devices
                            .filter_map(move |device| {
                                let name = device.name().ok()?;
                                let configs = device
                                    .supported_output_configs()
                                    .map(|supported| {
                                        supported
                                            .map(|config| {
                                                format!(
                                                    "{:?}/{:?}/{:?}-{:?}",
                                                    config.sample_format(),
                                                    config.channels(),
                                                    config.min_sample_rate(),
                                                    config.max_sample_rate()
                                                )
                                            })
                                            .collect()
                                    })
                                    .unwrap_or_default();
                                Some(AudioDevice {
                                    id: format!("{host_id:?}:{name}"),
                                    name: name.clone(),
                                    host: format!("{host_id:?}").to_lowercase(),
                                    is_default: default_name.as_ref() == Some(&name),
                                    supported_output_configs: configs,
                                })
                            })
                            .collect::<Vec<_>>()
                    })
                    .unwrap_or_default()
            })
            .collect()
    }

    pub fn start_output(&self, engine: AudioEngine) -> anyhow::Result<AudioOutput> {
        let host_id = match self.config.host {
            AudioHostName::Default => cpal::default_host().id(),
            host => host_to_cpal(host)
                .context("requested CPAL host is not available on this platform")?,
        };
        let host = cpal::host_from_id(host_id).context("failed to initialise CPAL host")?;
        let device = if let Some(selected) = &self.config.device {
            host.output_devices()?
                .find(|device| {
                    #[allow(deprecated)]
                    let name = device.name().unwrap_or_default();
                    format!("{host_id:?}:{name}") == *selected
                })
                .or_else(|| host.default_output_device())
        } else {
            host.default_output_device()
        }
        .context("no CPAL output device available")?;

        let default_config = device.default_output_config()?;
        let config = select_output_config(&device, default_config);
        let stream_config = config.config();
        engine.set_output_format(stream_config.sample_rate, stream_config.channels);
        tracing::info!(
            sample_rate = stream_config.sample_rate,
            channels = stream_config.channels,
            sample_format = ?config.sample_format(),
            "CPAL output stream format"
        );
        let err_fn = |err| tracing::warn!(%err, "CPAL output stream error");
        let stream = match config.sample_format() {
            SampleFormat::F32 => device.build_output_stream(
                &stream_config,
                move |data: &mut [f32], _| {
                    engine.fill_output(data);
                },
                err_fn,
                None,
            )?,
            SampleFormat::F64 => device.build_output_stream(
                &stream_config,
                move |data: &mut [f64], _| fill_converted(data, &engine),
                err_fn,
                None,
            )?,
            SampleFormat::I8 => device.build_output_stream(
                &stream_config,
                move |data: &mut [i8], _| fill_converted(data, &engine),
                err_fn,
                None,
            )?,
            SampleFormat::I16 => device.build_output_stream(
                &stream_config,
                move |data: &mut [i16], _| fill_converted(data, &engine),
                err_fn,
                None,
            )?,
            SampleFormat::I32 => device.build_output_stream(
                &stream_config,
                move |data: &mut [i32], _| fill_converted(data, &engine),
                err_fn,
                None,
            )?,
            SampleFormat::I64 => device.build_output_stream(
                &stream_config,
                move |data: &mut [i64], _| fill_converted(data, &engine),
                err_fn,
                None,
            )?,
            SampleFormat::U8 => device.build_output_stream(
                &stream_config,
                move |data: &mut [u8], _| fill_converted(data, &engine),
                err_fn,
                None,
            )?,
            SampleFormat::U16 => device.build_output_stream(
                &stream_config,
                move |data: &mut [u16], _| fill_converted(data, &engine),
                err_fn,
                None,
            )?,
            SampleFormat::U32 => device.build_output_stream(
                &stream_config,
                move |data: &mut [u32], _| fill_converted(data, &engine),
                err_fn,
                None,
            )?,
            SampleFormat::U64 => device.build_output_stream(
                &stream_config,
                move |data: &mut [u64], _| fill_converted(data, &engine),
                err_fn,
                None,
            )?,
            sample_format => anyhow::bail!("unsupported CPAL sample format {sample_format:?}"),
        };
        stream.play()?;
        Ok(AudioOutput {
            _stream: stream,
            sample_rate: stream_config.sample_rate,
            channels: stream_config.channels,
            sample_format: config.sample_format(),
        })
    }
}

fn select_output_config(
    device: &cpal::Device,
    default_config: SupportedStreamConfig,
) -> SupportedStreamConfig {
    const PREFERRED_RATE: u32 = 48_000;
    const PREFERRED_CHANNELS: u16 = 2;

    device
        .supported_output_configs()
        .ok()
        .and_then(|configs| {
            configs
                .filter(|config| {
                    config.channels() == PREFERRED_CHANNELS
                        && config.min_sample_rate() <= PREFERRED_RATE
                        && config.max_sample_rate() >= PREFERRED_RATE
                })
                .map(|config| config.with_sample_rate(PREFERRED_RATE))
                .next()
        })
        .unwrap_or(default_config)
}

fn fill_converted<T>(output: &mut [T], engine: &AudioEngine)
where
    T: cpal::Sample + cpal::FromSample<f32>,
{
    let mut scratch = vec![0.0; output.len()];
    engine.fill_output(&mut scratch);
    for (target, source) in output.iter_mut().zip(scratch) {
        *target = T::from_sample(source);
    }
}

impl AudioEngine {
    pub fn new(capacity_samples: usize) -> Self {
        let rb = HeapRb::<f32>::new(capacity_samples);
        let (producer, consumer) = rb.split();
        Self {
            producer: Arc::new(Mutex::new(producer)),
            consumer: Arc::new(Mutex::new(consumer)),
            output_format: Arc::new(Mutex::new(AudioOutputFormat {
                sample_rate: 48_000,
                channels: 2,
            })),
        }
    }

    pub fn set_output_format(&self, sample_rate: u32, channels: u16) {
        *self.output_format.lock() = AudioOutputFormat {
            sample_rate: sample_rate.max(1),
            channels: channels.max(1),
        };
    }

    #[allow(dead_code)]
    pub fn enqueue_interleaved(&self, samples: &[f32]) -> usize {
        let format = *self.output_format.lock();
        return self
            .enqueue_interleaved_for_output(samples, format.sample_rate, format.channels)
            .0;
    }

    pub fn enqueue_interleaved_for_output(
        &self,
        samples: &[f32],
        input_sample_rate: u32,
        input_channels: u16,
    ) -> (usize, usize) {
        let converted =
            self.convert_interleaved_for_output(samples, input_sample_rate, input_channels);
        let total = converted.len();
        let enqueued = self.enqueue_output_samples(&converted);
        (enqueued, total)
    }

    pub fn convert_interleaved_for_output(
        &self,
        samples: &[f32],
        input_sample_rate: u32,
        input_channels: u16,
    ) -> Vec<f32> {
        let output_format = *self.output_format.lock();
        let converted_channels = convert_channels(samples, input_channels, output_format.channels);
        if input_sample_rate != output_format.sample_rate {
            codec::resample(
                &converted_channels,
                input_sample_rate,
                output_format.sample_rate,
                output_format.channels,
            )
        } else {
            converted_channels
        }
    }

    pub fn enqueue_output_samples(&self, samples: &[f32]) -> usize {
        let mut producer = self.producer.lock();
        samples
            .iter()
            .copied()
            .take_while(|sample| producer.try_push(*sample).is_ok())
            .count()
    }

    pub fn available_samples(&self) -> usize {
        let consumer = self.consumer.lock();
        consumer.capacity().get() - consumer.occupied_len()
    }

    pub fn fill_output(&self, output: &mut [f32]) -> usize {
        let mut consumer = self.consumer.lock();
        let mut filled = 0;
        for sample in output.iter_mut() {
            match consumer.try_pop() {
                Some(value) => {
                    *sample = value;
                    filled += 1;
                }
                None => *sample = 0.0,
            }
        }
        filled
    }

    pub fn status(&self) -> AudioEngineStatus {
        let consumer = self.consumer.lock();
        let output_format = *self.output_format.lock();
        AudioEngineStatus {
            queued_samples: consumer.occupied_len(),
            capacity_samples: consumer.capacity().get(),
            output_sample_rate: output_format.sample_rate,
            output_channels: output_format.channels,
        }
    }
}

fn convert_channels(samples: &[f32], input_channels: u16, output_channels: u16) -> Vec<f32> {
    let input_channels = input_channels.max(1) as usize;
    let output_channels = output_channels.max(1) as usize;
    if input_channels == output_channels {
        return samples.to_vec();
    }

    let frames = samples.len() / input_channels;
    let mut output = Vec::with_capacity(frames * output_channels);
    for frame in 0..frames {
        let input_offset = frame * input_channels;
        let left = samples.get(input_offset).copied().unwrap_or(0.0);
        let right = if input_channels > 1 {
            samples.get(input_offset + 1).copied().unwrap_or(left)
        } else {
            left
        };

        for ch in 0..output_channels {
            output.push(match ch {
                0 => left,
                1 => right,
                _ => 0.0,
            });
        }
    }
    output
}

fn host_to_cpal(host: AudioHostName) -> Option<cpal::HostId> {
    match host {
        AudioHostName::Default => None,
        #[cfg(target_os = "linux")]
        AudioHostName::Alsa => Some(cpal::HostId::Alsa),
        #[cfg(not(target_os = "linux"))]
        AudioHostName::Alsa => None,
        #[cfg(any(target_os = "macos", target_os = "ios"))]
        AudioHostName::Coreaudio => Some(cpal::HostId::CoreAudio),
        #[cfg(not(any(target_os = "macos", target_os = "ios")))]
        AudioHostName::Coreaudio => None,
        #[cfg(target_os = "windows")]
        AudioHostName::Wasapi => Some(cpal::HostId::Wasapi),
        #[cfg(not(target_os = "windows"))]
        AudioHostName::Wasapi => None,
        #[cfg(all(target_os = "windows", feature = "asio"))]
        AudioHostName::Asio => Some(cpal::HostId::Asio),
        #[cfg(not(all(target_os = "windows", feature = "asio")))]
        AudioHostName::Asio => None,
        #[cfg(feature = "jack")]
        AudioHostName::Jack => Some(cpal::HostId::Jack),
        #[cfg(not(feature = "jack"))]
        AudioHostName::Jack => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn audio_engine_preserves_sample_order_and_zeros_underrun() {
        let engine = AudioEngine::new(4);
        assert_eq!(engine.enqueue_interleaved(&[0.1, 0.2, 0.3]), 3);
        let mut out = [1.0; 5];
        assert_eq!(engine.fill_output(&mut out), 3);
        assert_eq!(out, [0.1, 0.2, 0.3, 0.0, 0.0]);
    }

    #[test]
    fn audio_engine_resamples_to_output_rate() {
        let engine = AudioEngine::new(256);
        engine.set_output_format(44_100, 2);
        let input = vec![0.0; 96];
        let (enqueued, total) = engine.enqueue_interleaved_for_output(&input, 48_000, 2);
        assert_eq!(enqueued, total);
        assert!(total < input.len());
    }

    #[test]
    fn audio_engine_expands_stereo_to_multichannel_output() {
        let engine = AudioEngine::new(16);
        engine.set_output_format(48_000, 4);
        let (enqueued, total) =
            engine.enqueue_interleaved_for_output(&[1.0, -1.0, 0.5, -0.5], 48_000, 2);
        assert_eq!(enqueued, 8);
        assert_eq!(total, 8);
        let mut out = [9.0; 8];
        engine.fill_output(&mut out);
        assert_eq!(out, [1.0, -1.0, 0.0, 0.0, 0.5, -0.5, 0.0, 0.0]);
    }
}
