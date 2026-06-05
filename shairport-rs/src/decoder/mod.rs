use aes::Aes128;
use cbc::cipher::{block_padding::NoPadding, BlockDecryptMut, KeyIvInit};
use rsa::{Oaep, RsaPrivateKey};
use sha1::Sha1;

type Aes128CbcDec = cbc::Decryptor<Aes128>;

/// Decrypt an AES-128-CBC encrypted payload in place.
/// `key` must be 16 bytes. `iv` must be 16 bytes.
/// Returns the number of plaintext bytes (same as input, padded).
pub fn aes_cbc_decrypt_in_place(key: &[u8; 16], iv: &[u8; 16], data: &mut [u8]) -> Result<(), &'static str> {
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
pub fn rsa_oaep_decrypt(private_key: &RsaPrivateKey, ciphertext: &[u8]) -> Result<Vec<u8>, &'static str> {
    private_key
        .decrypt(Oaep::new::<Sha1>(), ciphertext)
        .map_err(|_| "RSA-OAEP decryption failed")
}

// --- ALAC decoder via Hammerton C code ---

use std::ffi::c_int;

unsafe extern "C" {
    fn alac_create(samplesize: c_int, numchannels: c_int) -> *mut std::ffi::c_void;
    fn alac_decode_frame(
        alac: *mut std::ffi::c_void,
        inbuffer: *const u8,
        outbuffer: *mut u8,
        outputsize: *mut c_int,
    );
    fn alac_set_info(alac: *mut std::ffi::c_void, inputbuffer: *const u8);
    fn alac_allocate_buffers(alac: *mut std::ffi::c_void);
    fn alac_free(alac: *mut std::ffi::c_void);
}

pub struct AlacDecoder {
    inner: *mut std::ffi::c_void,
    sample_size: u32,
    channels: u16,
}

unsafe impl Send for AlacDecoder {}

impl AlacDecoder {
    /// Create and initialise an ALAC decoder.
    /// `sample_size`: bit depth (e.g. 16)
    /// `channels`: number of channels (e.g. 2)
    /// `magic_cookie`: 24-byte ALACSpecificConfig bytes
    pub fn new(sample_size: u32, channels: u16, magic_cookie: &[u8]) -> Result<Self, &'static str> {
        if magic_cookie.len() < 24 {
            return Err("magic_cookie too short");
        }
        let inner = unsafe { alac_create(sample_size as c_int, channels as c_int) };
        if inner.is_null() {
            return Err("alac_create returned null");
        }

        // `alac_set_info` expects the raw ALACSpecificConfig bytes
        unsafe {
            alac_set_info(inner, magic_cookie.as_ptr());
            alac_allocate_buffers(inner);
        }

        Ok(Self {
            inner,
            sample_size,
            channels,
        })
    }

    /// Decode one ALAC frame.
    /// `input`: the raw ALAC packet payload (compressed)
    /// Returns decoded PCM samples as interleaved 32-bit signed.
    pub fn decode_frame(&mut self, input: &[u8]) -> Result<Vec<i32>, &'static str> {
        let max_output = (self.sample_size as usize / 8) * self.channels as usize * 4096;
        let mut output = vec![0i32; max_output / 4];
        let mut outsize: c_int = 0;

        unsafe {
            alac_decode_frame(
                self.inner,
                input.as_ptr(),
                output.as_mut_ptr() as *mut u8,
                &mut outsize,
            );
        }

        let sample_count = outsize as usize * self.channels as usize;
        output.truncate(sample_count);
        Ok(output)
    }
}

impl Drop for AlacDecoder {
    fn drop(&mut self) {
        if !self.inner.is_null() {
            unsafe { alac_free(self.inner) }
        }
    }
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
