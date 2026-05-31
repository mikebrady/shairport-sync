use std::{fmt, fs, path::Path};

use anyhow::Context;
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(default)]
pub struct Config {
    pub server: ServerConfig,
    pub airplay: AirplayConfig,
    pub mdns: MdnsConfig,
    pub audio: AudioConfig,
    pub ptp: PtpConfig,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(default)]
pub struct ServerConfig {
    pub bind: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(default)]
pub struct AirplayConfig {
    pub enabled: bool,
    pub bind: String,
    pub device_id: String,
    pub audio_port: u16,
    pub control_port: u16,
    pub timing_port: u16,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(default)]
pub struct MdnsConfig {
    pub backend: MdnsBackendName,
    pub interface: Option<String>,
    pub hostname: String,
    pub service_name: String,
    pub external_command: Option<String>,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum MdnsBackendName {
    Builtin,
    Avahi,
    DnsSd,
    External,
    Off,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(default)]
pub struct AudioConfig {
    pub backend: AudioBackendName,
    pub host: AudioHostName,
    pub device: Option<String>,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum AudioBackendName {
    Cpal,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum AudioHostName {
    Default,
    Alsa,
    Coreaudio,
    Wasapi,
    Asio,
    Jack,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(default)]
pub struct PtpConfig {
    pub enabled: bool,
    pub event_port: u16,
    pub general_port: u16,
}

impl Config {
    pub fn load(path: Option<&Path>) -> anyhow::Result<Self> {
        let Some(path) = path else {
            return Ok(Self::default());
        };
        let raw = fs::read_to_string(path)
            .with_context(|| format!("failed to read config {}", path.display()))?;
        toml::from_str(&raw).with_context(|| format!("failed to parse config {}", path.display()))
    }
}

impl Default for Config {
    fn default() -> Self {
        Self {
            server: ServerConfig::default(),
            airplay: AirplayConfig::default(),
            mdns: MdnsConfig::default(),
            audio: AudioConfig::default(),
            ptp: PtpConfig::default(),
        }
    }
}

impl Default for AirplayConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            bind: "0.0.0.0:7000".to_string(),
            device_id: "00:11:22:33:44:55".to_string(),
            audio_port: 6000,
            control_port: 6001,
            timing_port: 6002,
        }
    }
}

impl Default for ServerConfig {
    fn default() -> Self {
        Self {
            bind: "127.0.0.1:3689".to_string(),
        }
    }
}

impl Default for MdnsConfig {
    fn default() -> Self {
        Self {
            backend: MdnsBackendName::Builtin,
            interface: None,
            hostname: "shairport-rs".to_string(),
            service_name: "Shairport RS".to_string(),
            external_command: None,
        }
    }
}

impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            backend: AudioBackendName::Cpal,
            host: AudioHostName::Default,
            device: None,
        }
    }
}

impl Default for PtpConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            event_port: 319,
            general_port: 320,
        }
    }
}

impl fmt::Display for MdnsBackendName {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let value = match self {
            Self::Builtin => "builtin",
            Self::Avahi => "avahi",
            Self::DnsSd => "dns-sd",
            Self::External => "external",
            Self::Off => "off",
        };
        f.write_str(value)
    }
}

impl fmt::Display for AudioHostName {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let value = match self {
            Self::Default => "default",
            Self::Alsa => "alsa",
            Self::Coreaudio => "coreaudio",
            Self::Wasapi => "wasapi",
            Self::Asio => "asio",
            Self::Jack => "jack",
        };
        f.write_str(value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_public_config_shape() {
        let config: Config = toml::from_str(
            r#"
            [mdns]
            backend = "dns-sd"
            interface = "eth0"
            hostname = "living-room"
            service_name = "Living Room"

            [audio]
            backend = "cpal"
            host = "asio"
            "#,
        )
        .unwrap();

        assert_eq!(config.mdns.backend, MdnsBackendName::DnsSd);
        assert_eq!(config.audio.host, AudioHostName::Asio);
    }
}
