use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::{airplay::crypto::accessory_public_key_for_device_id, config::Config};

const SRCVERS: &str = "366.0";
const OSVERS: &str = "15.0";
const FIRMWARE_VERSION: &str = "5.0-shairport-rs";

pub const AP2_FEATURES: u64 = 0x1C340405D4A00 & !((1 << 17) | (1 << 16) | (1 << 15) | (1u64 << 50));
pub const AP2_STATUS_FLAGS: u32 = 0x4;

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AirplayService {
    pub service_type: String,
    pub instance_name: String,
    pub port: u16,
    pub txt: Vec<String>,
}

pub fn airplay_services(config: &Config) -> Vec<AirplayService> {
    let airplay_port = config
        .airplay
        .bind
        .rsplit_once(':')
        .and_then(|(_, port)| port.parse::<u16>().ok())
        .unwrap_or(7000);

    let raop = AirplayService {
        service_type: "_raop._tcp.local.".to_string(),
        instance_name: format!(
            "{}@{}",
            config.airplay.device_id.replace(':', "").to_uppercase(),
            config.mdns.service_name
        ),
        port: airplay_port,
        txt: if config.airplay.airplay2_enabled {
            raop_ap2_txt(config)
        } else {
            raop_ap1_txt(config)
        },
    };

    if config.airplay.airplay2_enabled {
        let airplay = AirplayService {
            service_type: "_airplay._tcp.local.".to_string(),
            instance_name: config.mdns.service_name.clone(),
            port: airplay_port,
            txt: airplay_txt(config),
        };
        vec![raop, airplay]
    } else {
        vec![raop]
    }
}

pub fn raop_ap1_txt(config: &Config) -> Vec<String> {
    vec![
        "sf=0x4".to_string(),
        format!("fv={FIRMWARE_VERSION}"),
        "am=ShairportSync".to_string(),
        "vs=105.1".to_string(),
        "tp=TCP,UDP".to_string(),
        "vn=65537".to_string(),
        "md=0,1,2".to_string(),
        "ss=16".to_string(),
        "sr=44100".to_string(),
        "da=true".to_string(),
        "sv=false".to_string(),
        "et=0,1".to_string(),
        "ek=1".to_string(),
        "cn=0,1".to_string(),
        "ch=2".to_string(),
        "txtvers=1".to_string(),
        "pw=false".to_string(),
        format!("pk={}", public_key_hex(&config.airplay.device_id)),
    ]
}

pub fn raop_ap2_txt(config: &Config) -> Vec<String> {
    let (features_lo, features_hi) = feature_words(AP2_FEATURES);
    vec![
        "cn=0,1".to_string(),
        "da=true".to_string(),
        "et=0,1".to_string(),
        "pw=false".to_string(),
        format!("ft=0x{features_lo:X},0x{features_hi:X}"),
        format!("fv={FIRMWARE_VERSION}"),
        format!("sf=0x{AP2_STATUS_FLAGS:X}"),
        "md=0,1,2".to_string(),
        "am=ShairportSync".to_string(),
        format!("pk={}", public_key_hex(&config.airplay.device_id)),
        "tp=UDP".to_string(),
        "vn=65537".to_string(),
        format!("vs={SRCVERS}"),
        format!("ov={OSVERS}"),
    ]
}

pub fn airplay_txt(config: &Config) -> Vec<String> {
    let (features_lo, features_hi) = feature_words(AP2_FEATURES);
    let pi = stable_uuid("pi", &config.airplay.device_id);
    let psi = stable_uuid("psi", &config.airplay.device_id);
    let fex = features_ex(AP2_FEATURES);
    vec![
        "acl=0".to_string(),
        "btaddr=00:00:00:00:00:00".to_string(),
        format!("deviceid={}", config.airplay.device_id),
        format!("fex={fex}"),
        format!("features=0x{features_lo:X},0x{features_hi:X}"),
        format!("flags=0x{AP2_STATUS_FLAGS:X}"),
        format!("gid={pi}"),
        "igl=0".to_string(),
        "gcgl=0".to_string(),
        "model=ShairportSync".to_string(),
        "protovers=1.1".to_string(),
        format!("pi={pi}"),
        format!("psi={psi}"),
        format!("pk={}", public_key_hex(&config.airplay.device_id)),
        format!("srcvers={SRCVERS}"),
        format!("osvers={OSVERS}"),
        "vv=2".to_string(),
        format!("fv={FIRMWARE_VERSION}"),
    ]
}

fn feature_words(features: u64) -> (u32, u32) {
    ((features & 0xffff_ffff) as u32, (features >> 32) as u32)
}

fn public_key_hex(device_id: &str) -> String {
    accessory_public_key_for_device_id(device_id)
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect()
}

fn stable_uuid(label: &str, device_id: &str) -> Uuid {
    Uuid::new_v5(
        &Uuid::NAMESPACE_DNS,
        format!("shairport-rs:{label}:{device_id}").as_bytes(),
    )
}

fn features_ex(features: u64) -> String {
    use base64::Engine;

    let bytes = features.to_le_bytes();
    base64::engine::general_purpose::STANDARD_NO_PAD.encode(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ap1_mode_only_publishes_raop() {
        let config = Config::default();
        assert!(!config.airplay.airplay2_enabled);
        let services = airplay_services(&config);
        assert_eq!(services.len(), 1);
        assert_eq!(services[0].service_type, "_raop._tcp.local.");
        assert!(services[0].txt.iter().any(|e| e.starts_with("sr=44100")));
        assert!(services[0].txt.iter().any(|e| e.starts_with("txtvers=1")));
    }

    #[test]
    fn ap2_mode_publishes_both_services() {
        let mut config = Config::default();
        config.airplay.airplay2_enabled = true;
        let services = airplay_services(&config);
        assert_eq!(services.len(), 2);
        assert!(services.iter().any(|s| s.service_type == "_raop._tcp.local."));
        assert!(services.iter().any(|s| s.service_type == "_airplay._tcp.local."));
    }

    #[test]
    fn airplay_txt_contains_required_discovery_fields() {
        let mut config = Config::default();
        config.airplay.airplay2_enabled = true;
        let txt = airplay_txt(&config);
        assert!(txt.iter().any(|entry| entry.starts_with("deviceid=")));
        assert!(txt.iter().any(|entry| entry.starts_with("pk=")));
        assert!(txt.iter().any(|entry| entry == "vv=2"));
        assert!(txt.iter().any(|entry| entry.starts_with("fex=")));
        assert!(
            !txt.iter()
                .any(|entry| entry.contains("00000000-0000-0000-0000-000000000000"))
        );
    }

    #[test]
    fn raop_ap2_txt_has_ft_field() {
        let mut config = Config::default();
        config.airplay.airplay2_enabled = true;
        let txt = raop_ap2_txt(&config);
        assert!(txt.iter().any(|e| e.starts_with("ft=0x")));
    }

    #[test]
    fn raop_ap1_txt_has_classic_fields() {
        let config = Config::default();
        let txt = raop_ap1_txt(&config);
        assert!(txt.iter().any(|e| e == "txtvers=1"));
        assert!(txt.iter().any(|e| e.starts_with("sr=44100")));
        assert!(txt.iter().any(|e| e.starts_with("ss=16")));
        assert!(txt.iter().any(|e| e.starts_with("ch=2")));
        assert!(txt.iter().any(|e| e.starts_with("tp=TCP,UDP")));
        assert!(txt.iter().any(|e| e.starts_with("pk=")));
        assert!(!txt.iter().any(|e| e.starts_with("ft=0x")));
    }
}
