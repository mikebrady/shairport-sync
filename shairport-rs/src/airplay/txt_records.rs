use serde::{Deserialize, Serialize};

use crate::config::Config;

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
    vec![
        AirplayService {
            service_type: "_raop._tcp.local.".to_string(),
            instance_name: format!(
                "{}@{}",
                config.airplay.device_id.replace(':', "").to_uppercase(),
                config.mdns.service_name
            ),
            port: airplay_port,
            txt: raop_txt(config),
        },
        AirplayService {
            service_type: "_airplay._tcp.local.".to_string(),
            instance_name: config.mdns.service_name.clone(),
            port: airplay_port,
            txt: airplay_txt(config),
        },
    ]
}

pub fn raop_txt(config: &Config) -> Vec<String> {
    let _ = config;
    vec![
        "txtvers=1".to_string(),
        "ch=2".to_string(),
        "cn=0,1,2,3".to_string(),
        "da=true".to_string(),
        "et=0,3,5".to_string(),
        "ft=0x5A7FFFF7,0x1E".to_string(),
        "md=0,1,2".to_string(),
        "pw=false".to_string(),
        "sr=44100".to_string(),
        "ss=16".to_string(),
        "sv=false".to_string(),
        "tp=UDP".to_string(),
        "vn=65537".to_string(),
        "vs=220.68".to_string(),
        "sf=0x4".to_string(),
    ]
}

pub fn airplay_txt(config: &Config) -> Vec<String> {
    vec![
        "txtvers=1".to_string(),
        "features=0x5A7FFFF7,0x1E".to_string(),
        "flags=0x4".to_string(),
        "model=ShairportRS1,1".to_string(),
        format!("manufacturer={}", "Shairport RS"),
        format!("serialNumber={}", config.mdns.hostname),
        "protovers=1.1".to_string(),
        "srcvers=220.68".to_string(),
        "pi=00000000-0000-0000-0000-000000000000".to_string(),
        "gid=00000000-0000-0000-0000-000000000000".to_string(),
        "gcgl=0".to_string(),
    ]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn publishes_both_airplay_service_types() {
        let services = airplay_services(&Config::default());
        assert_eq!(services.len(), 2);
        assert!(
            services
                .iter()
                .any(|s| s.service_type == "_raop._tcp.local.")
        );
        assert!(
            services
                .iter()
                .any(|s| s.service_type == "_airplay._tcp.local.")
        );
    }
}
