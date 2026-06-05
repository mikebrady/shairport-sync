use base64::Engine;
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
pub struct SdpSession {
    pub origin: Option<String>,
    pub session_name: Option<String>,
    pub media: Vec<SdpMedia>,
    pub attributes: Vec<(String, Option<String>)>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
pub struct SdpMedia {
    pub media_type: String,
    pub port: u16,
    pub protocol: String,
    pub formats: Vec<String>,
    pub attributes: Vec<(String, Option<String>)>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
pub struct ClassicAirplayParams {
    /// RSA-encrypted AES key (base64 raw bytes from `rsaaeskey`)
    pub rsaaeskey: Option<Vec<u8>>,
    /// AES IV (hex-decoded from `aesiv`)
    pub aesiv: Option<Vec<u8>>,
    /// ALAC frames per packet from fmtp
    pub frames_per_packet: Option<u32>,
    /// ALAC bit depth from fmtp
    pub alac_bit_depth: Option<u32>,
    /// ALAC sample rate from fmtp
    pub alac_sample_rate: Option<u32>,
    /// Latency in frames from `a=latency`
    pub latency_frames: Option<u32>,
    /// ALACSpecificConfig bytes (first 24 bytes from fmtp params)
    pub alac_specific_config: Option<Vec<u8>>,
}

impl SdpSession {
    pub fn source_format_description(&self) -> Option<String> {
        self.media.first().map(|media| {
            let fmt = media.formats.join(",");
            format!("{} {}/{}", media.media_type, media.protocol, fmt)
        })
    }

    /// Extract classic AirPlay parameters from the SDP.
    /// Looks for `a=rsaaeskey`, `a=aesiv`, `a=fmtp:N ...`, `a=latency:N`.
    pub fn classic_params(&self) -> ClassicAirplayParams {
        let mut params = ClassicAirplayParams::default();

        for (key, value) in &self.attributes {
            if key == "latency" {
                if let Some(v) = value {
                    params.latency_frames = v.trim().parse().ok();
                }
            }
        }

        for media in &self.media {
            for (key, value) in &media.attributes {
                match key.as_str() {
                    "rsaaeskey" => {
                        if let Some(v) = value {
                            params.rsaaeskey =
                                base64::engine::general_purpose::STANDARD
                                    .decode(v.trim())
                                    .ok();
                        }
                    }
                    "aesiv" => {
                        if let Some(v) = value {
                            params.aesiv = hex_decode(v.trim());
                        }
                    }
                    "fmtp" => {
                        if let Some(v) = value {
                            let parts: Vec<&str> = v.splitn(2, ' ').collect();
                            if parts.len() == 2 {
                                let numbers: Vec<&str> = parts[1].split_whitespace().collect();
                                if numbers.len() >= 11 {
                                    params.frames_per_packet = numbers[0].parse().ok();
                                    params.alac_bit_depth = numbers[2].parse().ok();
                                    params.alac_sample_rate = numbers[10].parse().ok();
                                    let asc = build_alac_specific_config(&numbers);
                                    if !asc.is_empty() {
                                        params.alac_specific_config = Some(asc);
                                    }
                                }
                            }
                        }
                    }
                    "latency" => {
                        if let Some(v) = value {
                            params.latency_frames = v.trim().parse().ok();
                        }
                    }
                    _ => {}
                }
            }
        }

        params
    }
}

/// Build ALACSpecificConfig (24 bytes) from parsed fmtp numbers.
/// fmtp layout: <format> <framesPerPacket> <compatibleVersion> <bitDepth> <packetSize> <historyMult> <initialHistory> <kModifier> <maxKbps> <unused> <unused> <sampleRate>
fn build_alac_specific_config(numbers: &[&str]) -> Vec<u8> {
    if numbers.len() < 11 {
        return Vec::new();
    }

    let frame_length: u32 = numbers[0].parse().unwrap_or(352);
    let compatible_version: u8 = numbers[1].parse().unwrap_or(0);
    let bit_depth: u8 = numbers[2].parse().unwrap_or(16);
    let pb: u8 = numbers[3].parse().unwrap_or(40);
    let mb: u8 = numbers[4].parse().unwrap_or(10);
    let kb: u8 = numbers[5].parse().unwrap_or(14);
    let num_channels: u8 = numbers[6].parse().unwrap_or(2);
    let max_run: u16 = numbers[7].parse().unwrap_or(255);
    let max_frame_bytes: u32 = numbers[8].parse().unwrap_or(0);
    let avg_bit_rate: u32 = numbers[9].parse().unwrap_or(0);
    let sample_rate: u32 = numbers[10].parse().unwrap_or(44100);

    // ALACSpecificConfig format (24 bytes):
    //   uint32_t frameLength;           // 4
    //   uint8_t  compatibleVersion;     // 1
    //   uint8_t  bitDepth;              // 1
    //   uint8_t  pb;                    // 1
    //   uint8_t  mb;                    // 1
    //   uint8_t  kb;                    // 1
    //   uint8_t  numChannels;           // 1
    //   uint16_t maxRun;                // 2
    //   uint32_t maxFrameBytes;         // 4
    //   uint32_t avgBitRate;            // 4
    //   uint32_t sampleRate;            // 4
    // total: 24 bytes

    let mut asc = Vec::with_capacity(24);
    asc.extend_from_slice(&frame_length.to_be_bytes());
    asc.push(compatible_version);
    asc.push(bit_depth);
    asc.push(pb);
    asc.push(mb);
    asc.push(kb);
    asc.push(num_channels);
    asc.extend_from_slice(&max_run.to_be_bytes());
    asc.extend_from_slice(&max_frame_bytes.to_be_bytes());
    asc.extend_from_slice(&avg_bit_rate.to_be_bytes());
    asc.extend_from_slice(&sample_rate.to_be_bytes());
    asc
}

pub fn parse_sdp(input: &str) -> SdpSession {
    let mut session = SdpSession::default();
    let mut current_media: Option<SdpMedia> = None;

    for line in input.lines().map(str::trim).filter(|line| line.len() >= 2) {
        let Some((prefix, value)) = line.split_once('=') else {
            continue;
        };
        match prefix {
            "o" => session.origin = Some(value.to_string()),
            "s" => session.session_name = Some(value.to_string()),
            "m" => {
                if let Some(media) = current_media.take() {
                    session.media.push(media);
                }
                let mut parts = value.split_whitespace();
                let media_type = parts.next().unwrap_or_default().to_string();
                let port = parts.next().and_then(|p| p.parse().ok()).unwrap_or(0);
                let protocol = parts.next().unwrap_or_default().to_string();
                let formats = parts.map(str::to_string).collect();
                current_media = Some(SdpMedia {
                    media_type,
                    port,
                    protocol,
                    formats,
                    attributes: Vec::new(),
                });
            }
            "a" => {
                let attr = parse_attribute(value);
                if let Some(media) = current_media.as_mut() {
                    media.attributes.push(attr);
                } else {
                    session.attributes.push(attr);
                }
            }
            _ => {}
        }
    }

    if let Some(media) = current_media {
        session.media.push(media);
    }
    session
}

fn parse_attribute(value: &str) -> (String, Option<String>) {
    value
        .split_once(':')
        .map(|(k, v)| (k.to_string(), Some(v.to_string())))
        .unwrap_or_else(|| (value.to_string(), None))
}

fn hex_decode(s: &str) -> Option<Vec<u8>> {
    let s = s.trim();
    if s.len() % 2 != 0 {
        return None;
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).ok())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_media_and_attributes() {
        let sdp = "v=0\r\no=iTunes 1 0 IN IP4 127.0.0.1\r\ns=AirTunes\r\nm=audio 0 RTP/AVP 96\r\na=rtpmap:96 AppleLossless\r\n";
        let parsed = parse_sdp(sdp);
        assert_eq!(parsed.session_name.as_deref(), Some("AirTunes"));
        assert_eq!(parsed.media[0].media_type, "audio");
        assert_eq!(parsed.media[0].formats, vec!["96"]);
        assert_eq!(
            parsed.source_format_description().as_deref(),
            Some("audio RTP/AVP/96")
        );
    }

    #[test]
    fn extracts_classic_airplay_params() {
        let sdp = "v=0\r\no=iTunes 1 0 IN IP4 10.0.0.2\r\ns=AirTunes\r\nm=audio 0 RTP/AVP 96\r\na=rtpmap:96 AppleLossless\r\na=rsaaeskey:YWJjZGVmZ2hpamtsbW5vcA==\r\na=aesiv:0102030405060708090a0b0c0d0e0f10\r\na=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\na=latency:22050\r\n";
        let parsed = parse_sdp(sdp);
        let params = parsed.classic_params();
        assert_eq!(params.rsaaeskey.as_deref(), Some(&b"abcdefghijklmnop"[..]));
        assert_eq!(
            params.aesiv.as_deref(),
            Some(&[1u8, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16][..])
        );
        assert_eq!(params.frames_per_packet, Some(352));
        assert_eq!(params.alac_bit_depth, Some(16));
        assert_eq!(params.alac_sample_rate, Some(44100));
        assert_eq!(params.latency_frames, Some(22050));

        // Verify ALACSpecificConfig
        let asc = params.alac_specific_config.unwrap();
        assert_eq!(asc.len(), 24);
        // frameLength 352 = 0x160
        assert_eq!(asc[0], 0);
        assert_eq!(asc[1], 0);
        assert_eq!(asc[2], 1);
        assert_eq!(asc[3], 0x60);
        // compatibleVersion
        assert_eq!(asc[4], 0);
        // bitDepth
        assert_eq!(asc[5], 16);
    }

    #[test]
    fn classic_params_are_empty_for_non_airplay_sdp() {
        let sdp = "v=0\r\no=- 0 0 IN IP4 0.0.0.0\r\ns=Test\r\nm=video 0 RTP/AVP 98\r\n";
        let parsed = parse_sdp(sdp);
        let params = parsed.classic_params();
        assert!(params.rsaaeskey.is_none());
        assert!(params.aesiv.is_none());
        assert!(params.frames_per_packet.is_none());
    }

    #[test]
    fn hex_decode_works() {
        assert_eq!(hex_decode("0102ff").as_deref(), Some(&[1u8, 2, 255][..]));
        assert_eq!(hex_decode(""), Some(vec![]));
        assert!(hex_decode("abc").is_none()); // odd length
    }

    #[test]
    fn builds_alac_specific_config_correctly() {
        let numbers = vec!["352", "0", "16", "40", "10", "14", "2", "255", "0", "0", "44100"];
        let asc = build_alac_specific_config(&numbers);
        assert_eq!(asc.len(), 24);
        // 352 = 0x160 in big-endian
        assert_eq!(&asc[0..4], &[0, 0, 1, 0x60]);
        assert_eq!(asc[5], 16);  // bitDepth
        assert_eq!(asc[9], 2);   // numChannels
        // maxRun = 255 = 0x00FF
        assert_eq!(asc[10], 0);
        assert_eq!(asc[11], 0xFF);
        // 44100 = 0xAC44
        assert_eq!(&asc[20..24], &[0, 0, 0xAC, 0x44]);
    }
}
