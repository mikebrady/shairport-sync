use aes::Aes128;
use cbc::cipher::{BlockDecryptMut, KeyIvInit, block_padding::NoPadding};
use rsa::{Oaep, RsaPrivateKey};
use sha1::Sha1;

type Aes128CbcDec = cbc::Decryptor<Aes128>;

/// Decrypt an AES-128-CBC encrypted payload in place.
/// `key` must be 16 bytes. `iv` must be 16 bytes.
/// Returns the number of plaintext bytes (same as input, padded).
pub fn aes_cbc_decrypt_in_place(
    key: &[u8; 16],
    iv: &[u8; 16],
    data: &mut [u8],
) -> Result<(), &'static str> {
    let plaintext = Aes128CbcDec::new(key.into(), iv.into())
        .decrypt_padded_mut::<NoPadding>(data)
        .map_err(|_| "AES-CBC decryption failed")?;
    let _ = plaintext;
    Ok(())
}

/// Generate a 2048-bit RSA private key.
pub fn generate_rsa_key() -> RsaPrivateKey {
    let mut rng = rand_core::OsRng;
    RsaPrivateKey::new(&mut rng, 2048).expect("failed to generate RSA key")
}

/// Decrypt an RSA-OAEP-SHA1 encrypted blob using the private key.
pub fn rsa_oaep_decrypt(
    private_key: &RsaPrivateKey,
    ciphertext: &[u8],
) -> Result<Vec<u8>, &'static str> {
    private_key
        .decrypt(Oaep::new::<Sha1>(), ciphertext)
        .map_err(|_| "RSA-OAEP decryption failed")
}

#[cfg(test)]
mod tests {
    use super::*;
    use rsa::traits::PublicKeyParts;

    #[test]
    fn aes_cbc_round_trip() {
        let key = [0x2b; 16];
        let iv = [0x01; 16];
        let plaintext: [u8; 16] = [0x10; 16];
        let mut ciphertext = plaintext.clone();
        let _ = aes_cbc_decrypt_in_place(&key, &iv, &mut ciphertext);
    }

    #[test]
    fn rsa_key_generates() {
        let key = generate_rsa_key();
        assert!(key.n().bits() >= 2048);
    }

    #[test]
    fn rsa_oaep_round_trip() {
        let key = generate_rsa_key();
        let data = b"hello alac world!";
        let mut rng = rand_core::OsRng;
        let pub_key = rsa::RsaPublicKey::from(&key);
        let encrypted = pub_key
            .encrypt(&mut rng, Oaep::new::<Sha1>(), data)
            .expect("RSA encrypt");
        let decrypted = rsa_oaep_decrypt(&key, &encrypted).expect("RSA decrypt");
        assert_eq!(&decrypted, data);
    }
}
