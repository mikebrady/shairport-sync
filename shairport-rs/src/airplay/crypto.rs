use chacha20poly1305::{
    ChaCha20Poly1305, KeyInit,
    aead::{Aead, Payload},
};
use ed25519_dalek::{Signature, Signer, SigningKey, Verifier, VerifyingKey};
use hkdf::Hkdf;
use rand_core::OsRng;
use serde::{Deserialize, Serialize};
use sha2::Sha512;
use x25519_dalek::{PublicKey, StaticSecret};

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct DerivedKey(pub [u8; 32]);

#[allow(dead_code)]
#[derive(Clone, Debug)]
pub struct PairCipher {
    encryption_key: DerivedKey,
    decryption_key: DerivedKey,
    encryption_counter: u64,
    decryption_counter: u64,
}

pub struct IdentityKey {
    signing: SigningKey,
}

pub struct AgreementKey {
    secret: StaticSecret,
    public: PublicKey,
}

impl IdentityKey {
    #[allow(dead_code)]
    pub fn generate() -> Self {
        Self {
            signing: SigningKey::generate(&mut OsRng),
        }
    }

    pub fn from_seed(seed: [u8; 32]) -> Self {
        Self {
            signing: SigningKey::from_bytes(&seed),
        }
    }

    /// Load identity key from a file, or generate and save if not present.
    pub fn load_or_generate(path: Option<&std::path::Path>) -> Self {
        if let Some(path) = path {
            if let Ok(data) = std::fs::read(path) {
                if data.len() == 32 {
                    let mut seed = [0u8; 32];
                    seed.copy_from_slice(&data);
                    return Self::from_seed(seed);
                }
            }
            // Generate and persist
            let key = Self::generate();
            if let Err(e) = std::fs::write(path, &key.signing.to_bytes()) {
                tracing::warn!(%e, "failed to persist identity key");
            }
            return key;
        }
        // Use deterministic seed from device_id as fallback
        Self::generate()
    }

    pub fn verifying_key(&self) -> [u8; 32] {
        self.signing.verifying_key().to_bytes()
    }

    pub fn sign(&self, message: &[u8]) -> [u8; 64] {
        self.signing.sign(message).to_bytes()
    }

    pub fn verify(public_key: &[u8; 32], message: &[u8], signature: &[u8; 64]) -> bool {
        let Ok(key) = VerifyingKey::from_bytes(public_key) else {
            return false;
        };
        let signature = Signature::from_bytes(signature);
        key.verify(message, &signature).is_ok()
    }
}

pub fn nonce_from_label(label: &[u8]) -> [u8; 12] {
    let mut nonce = [0u8; 12];
    let len = label.len().min(8);
    nonce[4..4 + len].copy_from_slice(&label[..len]);
    nonce
}

pub fn accessory_public_key_for_device_id(device_id: &str) -> [u8; 32] {
    let mut seed = [0u8; 32];
    let id = device_id.as_bytes();
    let len = id.len().min(seed.len());
    seed[..len].copy_from_slice(&id[..len]);
    IdentityKey::from_seed(seed).verifying_key()
}

impl AgreementKey {
    pub fn generate() -> Self {
        let secret = StaticSecret::random_from_rng(OsRng);
        let public = PublicKey::from(&secret);
        Self { secret, public }
    }

    pub fn public_key(&self) -> [u8; 32] {
        self.public.to_bytes()
    }

    pub fn shared_secret(&self, peer_public: &[u8; 32]) -> [u8; 32] {
        self.secret
            .diffie_hellman(&PublicKey::from(*peer_public))
            .to_bytes()
    }
}

pub fn hkdf_sha512(secret: &[u8], salt: &[u8], info: &[u8]) -> DerivedKey {
    let hk = Hkdf::<Sha512>::new(Some(salt), secret);
    let mut out = [0u8; 32];
    hk.expand(info, &mut out)
        .expect("32-byte HKDF output is valid");
    DerivedKey(out)
}

pub fn seal(
    key: &DerivedKey,
    nonce: &[u8; 12],
    aad: &[u8],
    plaintext: &[u8],
) -> anyhow::Result<Vec<u8>> {
    let cipher = ChaCha20Poly1305::new((&key.0).into());
    cipher
        .encrypt(
            nonce.into(),
            Payload {
                msg: plaintext,
                aad,
            },
        )
        .map_err(|_| anyhow::anyhow!("chacha20-poly1305 encryption failed"))
}

pub fn open(
    key: &DerivedKey,
    nonce: &[u8; 12],
    aad: &[u8],
    ciphertext: &[u8],
) -> anyhow::Result<Vec<u8>> {
    let cipher = ChaCha20Poly1305::new((&key.0).into());
    cipher
        .decrypt(
            nonce.into(),
            Payload {
                msg: ciphertext,
                aad,
            },
        )
        .map_err(|_| anyhow::anyhow!("chacha20-poly1305 decryption failed"))
}

impl PairCipher {
    pub fn control_for_server(shared_secret: &[u8]) -> Self {
        Self::new(
            shared_secret,
            b"Control-Salt",
            b"Control-Read-Encryption-Key",
            b"Control-Salt",
            b"Control-Write-Encryption-Key",
        )
    }

    #[allow(dead_code)]
    pub fn events_for_server(shared_secret: &[u8]) -> Self {
        Self::new(
            shared_secret,
            b"Events-Salt",
            b"Events-Write-Encryption-Key",
            b"Events-Salt",
            b"Events-Read-Encryption-Key",
        )
    }

    #[allow(dead_code)]
    pub fn data_for_server(shared_secret: &[u8], seed: &str) -> Self {
        let write_salt = format!("DataStream-Salt{seed}");
        let read_salt = format!("DataStream-Salt{seed}");
        Self::new(
            shared_secret,
            write_salt.as_bytes(),
            b"DataStream-Write-Encryption-Key",
            read_salt.as_bytes(),
            b"DataStream-Read-Encryption-Key",
        )
    }

    fn new(
        shared_secret: &[u8],
        write_salt: &[u8],
        write_info: &[u8],
        read_salt: &[u8],
        read_info: &[u8],
    ) -> Self {
        Self {
            encryption_key: hkdf_sha512(shared_secret, write_salt, write_info),
            decryption_key: hkdf_sha512(shared_secret, read_salt, read_info),
            encryption_counter: 0,
            decryption_counter: 0,
        }
    }

    #[allow(dead_code)]
    pub fn encrypt_blocks(&mut self, plaintext: &[u8]) -> anyhow::Result<Vec<u8>> {
        const MAX_BLOCK: usize = 1024;
        let mut out = Vec::with_capacity(plaintext.len() + 18);
        for block in plaintext.chunks(MAX_BLOCK) {
            let block_len = block.len() as u16;
            let block_len_bytes = block_len.to_le_bytes();
            let nonce = counter_nonce(self.encryption_counter);
            let encrypted = seal(&self.encryption_key, &nonce, &block_len_bytes, block)?;
            out.extend_from_slice(&block_len_bytes);
            out.extend_from_slice(&encrypted);
            self.encryption_counter = self.encryption_counter.saturating_add(1);
        }
        Ok(out)
    }

    #[allow(dead_code)]
    pub fn decrypt_blocks(&mut self, ciphertext: &[u8]) -> anyhow::Result<(Vec<u8>, usize)> {
        let mut consumed = 0;
        let mut out = Vec::new();
        while ciphertext.len().saturating_sub(consumed) >= 18 {
            let block_len =
                u16::from_le_bytes([ciphertext[consumed], ciphertext[consumed + 1]]) as usize;
            let block_total = 2 + block_len + 16;
            if ciphertext.len() - consumed < block_total {
                break;
            }
            let block_len_bytes = [ciphertext[consumed], ciphertext[consumed + 1]];
            let payload = &ciphertext[consumed + 2..consumed + block_total];
            let nonce = counter_nonce(self.decryption_counter);
            let decrypted = open(&self.decryption_key, &nonce, &block_len_bytes, payload)?;
            out.extend_from_slice(&decrypted);
            consumed += block_total;
            self.decryption_counter = self.decryption_counter.saturating_add(1);
        }
        Ok((out, consumed))
    }
}

#[allow(dead_code)]
fn counter_nonce(counter: u64) -> [u8; 12] {
    let mut nonce = [0u8; 12];
    nonce[4..].copy_from_slice(&counter.to_le_bytes());
    nonce
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn identity_signatures_verify() {
        let key = IdentityKey::generate();
        let public = key.verifying_key();
        let signature = key.sign(b"message");
        assert!(IdentityKey::verify(&public, b"message", &signature));
        assert!(!IdentityKey::verify(&public, b"other", &signature));
    }

    #[test]
    fn x25519_shared_secret_matches() {
        let a = AgreementKey::generate();
        let b = AgreementKey::generate();
        assert_eq!(
            a.shared_secret(&b.public_key()),
            b.shared_secret(&a.public_key())
        );
    }

    #[test]
    fn chacha_round_trip() {
        let key = hkdf_sha512(b"secret", b"salt", b"info");
        let nonce = *b"123456789012";
        let ciphertext = seal(&key, &nonce, b"aad", b"plain").unwrap();
        let plaintext = open(&key, &nonce, b"aad", &ciphertext).unwrap();
        assert_eq!(plaintext, b"plain");
    }

    #[test]
    fn pair_cipher_round_trips_blocks() {
        let secret = [9u8; 32];
        let mut writer = PairCipher::new(&secret, b"salt-a", b"info-a", b"salt-b", b"info-b");
        let mut reader = PairCipher::new(&secret, b"salt-b", b"info-b", b"salt-a", b"info-a");
        let encrypted = writer.encrypt_blocks(b"hello encrypted rtsp").unwrap();
        let (plain, consumed) = reader.decrypt_blocks(&encrypted).unwrap();
        assert_eq!(consumed, encrypted.len());
        assert_eq!(plain, b"hello encrypted rtsp");
    }
}
