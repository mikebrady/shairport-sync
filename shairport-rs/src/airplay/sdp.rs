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

impl SdpSession {
    pub fn source_format_description(&self) -> Option<String> {
        self.media.first().map(|media| {
            let fmt = media.formats.join(",");
            format!("{} {}/{}", media.media_type, media.protocol, fmt)
        })
    }
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
}
