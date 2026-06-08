use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
pub struct Tlv {
    values: BTreeMap<u8, Vec<Vec<u8>>>,
}

impl Tlv {
    pub fn parse(mut input: &[u8]) -> Self {
        let mut values: BTreeMap<u8, Vec<Vec<u8>>> = BTreeMap::new();
        loop {
            if input.len() < 2 {
                break;
            }
            let ty = input[0];
            let len = input[1] as usize;
            input = &input[2..];
            if input.len() < len {
                break;
            }
            values.entry(ty).or_default().push(input[..len].to_vec());
            input = &input[len..];
        }
        Self { values }
    }

    pub fn insert(&mut self, ty: u8, value: impl AsRef<[u8]>) {
        let value = value.as_ref();
        self.values.entry(ty).or_default().push(value.to_vec());
    }

    pub fn first(&self, ty: u8) -> Option<&[u8]> {
        self.values
            .get(&ty)
            .and_then(|values| values.first())
            .map(Vec::as_slice)
    }

    pub fn joined(&self, ty: u8) -> Option<Vec<u8>> {
        let values = self.values.get(&ty)?;
        let len = values.iter().map(Vec::len).sum();
        let mut out = Vec::with_capacity(len);
        for value in values {
            out.extend_from_slice(value);
        }
        Some(out)
    }

    pub fn debug_summary(&self) -> String {
        self.values
            .iter()
            .map(|(ty, values)| {
                let joined_len: usize = values.iter().map(Vec::len).sum();
                let fragments = values.len();
                let first = values.first().map(Vec::as_slice).unwrap_or_default();
                format!(
                    "{ty}:frags={fragments},len={joined_len},first={}",
                    hex_prefix(first, 12)
                )
            })
            .collect::<Vec<_>>()
            .join("; ")
    }

    pub fn encode(&self) -> Vec<u8> {
        let mut out = Vec::new();
        for (ty, values) in &self.values {
            for value in values {
                // Apple HAP uses fragmentation: values > 255 bytes are split into
                // multiple entries of the same type, each with its own 1-byte length.
                let chunks = value.chunks(255);
                let num_chunks = chunks.len();
                for (i, chunk) in value.chunks(255).enumerate() {
                    out.push(*ty);
                    // Only the last chunk may be < 255 bytes
                    // All preceding chunks must signal 255
                    if i < num_chunks - 1 {
                        out.push(255u8);
                    } else {
                        out.push(chunk.len() as u8);
                    }
                    out.extend_from_slice(chunk);
                }
            }
        }
        out
    }
}

fn hex_prefix(bytes: &[u8], limit: usize) -> String {
    let mut out = bytes
        .iter()
        .take(limit)
        .map(|byte| format!("{byte:02x}"))
        .collect::<Vec<_>>()
        .join("");
    if bytes.len() > limit {
        out.push_str("...");
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tlv_round_trips_large_value() {
        let mut tlv = Tlv::default();
        let value = vec![7u8; 300];
        tlv.insert(1, &value);
        let encoded = tlv.encode();
        let parsed = Tlv::parse(&encoded);
        let decoded = parsed.joined(1).unwrap();
        assert_eq!(decoded, value);
    }

    #[test]
    fn tlv_round_trips_384_byte_value() {
        let mut tlv = Tlv::default();
        let value = vec![0xAAu8; 384];
        tlv.insert(3, &value);
        let encoded = tlv.encode();
        let parsed = Tlv::parse(&encoded);
        let decoded = parsed.joined(3).unwrap();
        assert_eq!(decoded.len(), 384);
        assert_eq!(decoded, value);
    }

    #[test]
    fn tlv_parses_varint_header() {
        // Apple HAP uses fragmentation: [type][len=255][255 bytes][type][len=129][129 bytes]
        // This test verifies the parser handles fragmented values via joined()
        let mut encoded = vec![0x03, 255];
        encoded.extend_from_slice(&vec![0x42u8; 255]);
        encoded.push(0x03);
        encoded.push(129);
        encoded.extend_from_slice(&vec![0x42u8; 129]);
        encoded.push(0x06);
        encoded.push(0x01);
        encoded.push(0x03);
        let parsed = Tlv::parse(&encoded);
        assert_eq!(parsed.joined(3).unwrap().len(), 384);
        assert_eq!(parsed.first(6).unwrap(), &[0x03]);
    }

    #[test]
    fn tlv_small_value_uses_single_byte_length() {
        let mut tlv = Tlv::default();
        tlv.insert(1, &[0x42u8; 100]);
        let encoded = tlv.encode();
        assert_eq!(encoded[1], 100);
        let parsed = Tlv::parse(&encoded);
        assert_eq!(parsed.joined(1).unwrap().len(), 100);
    }

    #[test]
    fn tlv_multiple_values_same_type() {
        let mut tlv = Tlv::default();
        tlv.insert(1, &[1u8; 10]);
        tlv.insert(1, &[2u8; 20]);
        let encoded = tlv.encode();
        let parsed = Tlv::parse(&encoded);
        assert_eq!(parsed.joined(1).unwrap().len(), 30);
    }

    #[test]
    fn tlv_parse_fragmented_entries() {
        // Simulate fragmented encoding: [1][5]["hello"][2][255][255 bytes][2][1][1 byte]
        let mut buf = Vec::new();
        buf.extend_from_slice(&[0x01, 0x05]);
        buf.extend_from_slice(b"hello");
        buf.extend_from_slice(&[0x02, 255]);
        buf.extend_from_slice(&vec![0xFFu8; 255]);
        buf.extend_from_slice(&[0x02, 1]);
        buf.extend_from_slice(&[0x42u8; 1]);
        let parsed = Tlv::parse(&buf);
        assert_eq!(parsed.first(1).unwrap(), b"hello");
        assert_eq!(parsed.joined(2).unwrap().len(), 256);
    }

    #[test]
    fn tlv_debug_summary_includes_fragments_and_lengths() {
        let mut tlv = Tlv::default();
        tlv.insert(1, b"hello");
        tlv.insert(3, vec![0x42u8; 300]);
        let summary = Tlv::parse(&tlv.encode()).debug_summary();
        assert!(summary.contains("1:frags=1,len=5,first=68656c6c6f"));
        assert!(summary.contains("3:frags=2,len=300,first=424242424242424242424242..."));
    }
}
