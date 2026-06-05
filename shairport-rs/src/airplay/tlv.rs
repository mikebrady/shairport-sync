use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
pub struct Tlv {
    values: BTreeMap<u8, Vec<Vec<u8>>>,
}

impl Tlv {
    pub fn parse(mut input: &[u8]) -> Self {
        let mut values: BTreeMap<u8, Vec<Vec<u8>>> = BTreeMap::new();
        while input.len() >= 2 {
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
        if value.is_empty() {
            self.values.entry(ty).or_default().push(Vec::new());
            return;
        }
        for chunk in value.chunks(255) {
            self.values.entry(ty).or_default().push(chunk.to_vec());
        }
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

    pub fn encode(&self) -> Vec<u8> {
        let mut out = Vec::new();
        for (ty, values) in &self.values {
            for value in values {
                for chunk in value.chunks(255) {
                    out.push(*ty);
                    out.push(chunk.len() as u8);
                    out.extend_from_slice(chunk);
                }
            }
        }
        out
    }
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

        let chunks = parsed.values.get(&1).unwrap();
        assert_eq!(chunks.len(), 2);
        assert_eq!(chunks[0].len(), 255);
        assert_eq!(chunks[1].len(), 45);
        assert_eq!(parsed.joined(1).unwrap(), value);
    }
}
