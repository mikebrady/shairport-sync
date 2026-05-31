use serde::{Deserialize, Serialize};

#[allow(dead_code)]
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct MetadataPacket {
    pub kind: [u8; 4],
    pub code: [u8; 4],
    pub value: Vec<u8>,
}

#[allow(dead_code)]
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
pub struct ParsedMetadata {
    pub title: Option<String>,
    pub artist: Option<String>,
    pub album: Option<String>,
    pub volume: Option<String>,
}

impl MetadataPacket {
    #[allow(dead_code)]
    pub fn parse_udp(packet: &[u8]) -> Option<Self> {
        if packet.len() < 8 {
            return None;
        }
        Some(Self {
            kind: packet[0..4].try_into().ok()?,
            code: packet[4..8].try_into().ok()?,
            value: packet[8..].to_vec(),
        })
    }
}

#[allow(dead_code)]
pub fn parse_metadata_packets(packets: &[MetadataPacket]) -> ParsedMetadata {
    let mut parsed = ParsedMetadata::default();
    for packet in packets {
        let value = decode_text(&packet.value);
        match &packet.code {
            b"minm" => parsed.title = value,
            b"asar" => parsed.artist = value,
            b"asal" => parsed.album = value,
            b"pvol" => parsed.volume = value,
            _ => {}
        }
    }
    parsed
}

#[allow(dead_code)]
fn decode_text(value: &[u8]) -> Option<String> {
    let text = String::from_utf8_lossy(value)
        .trim_matches(char::from(0))
        .trim()
        .to_string();
    (!text.is_empty()).then_some(text)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_shairport_metadata_packet_shape() {
        let mut raw = b"coreminm".to_vec();
        raw.extend_from_slice(b"Track");
        let packet = MetadataPacket::parse_udp(&raw).unwrap();
        let parsed = parse_metadata_packets(&[packet]);
        assert_eq!(parsed.title.as_deref(), Some("Track"));
    }
}
