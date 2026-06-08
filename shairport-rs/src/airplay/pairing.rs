use std::path::Path;

use chacha20poly1305::{
    ChaCha20Poly1305, KeyInit,
    aead::{Aead, Payload},
};
use rand_core::{OsRng, RngCore};
use serde::{Deserialize, Serialize};
use sha2_011::Sha512 as SrpSha512;
use srp::{ClientG3072, ServerG3072};
use tracing::{debug, info, warn};

use crate::airplay::{
    crypto::{AgreementKey, DerivedKey, IdentityKey, hkdf_sha512, nonce_from_label, open, seal},
    tlv::Tlv,
};

pub const TLV_METHOD: u8 = 0;
pub const TLV_IDENTIFIER: u8 = 1;
pub const TLV_SALT: u8 = 2;
pub const TLV_PUBLIC_KEY: u8 = 3;
pub const TLV_PROOF: u8 = 4;
pub const TLV_ENCRYPTED_DATA: u8 = 5;
pub const TLV_STATE: u8 = 6;
pub const TLV_ERROR: u8 = 7;
pub const TLV_SIGNATURE: u8 = 10;
pub const TLV_FLAGS: u8 = 19;
pub const TLV_ERROR_AUTHENTICATION: u8 = 2;
const PAIRING_FLAGS_TRANSIENT: u8 = 0x10;
const PAIR_SETUP_USERNAME: &[u8] = b"Pair-Setup";

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub enum PairingEndpoint {
    Setup,
    Verify,
    Add,
    Remove,
    List,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct PairingReply {
    pub status_code: u16,
    pub body: Vec<u8>,
}

#[derive(Default)]
pub struct PairingSession {
    setup: Option<PairSetupState>,
    // Raw SRP session key K (64 bytes), available after M3
    setup_session_key: Option<Vec<u8>>,
    verify_agreement: Option<AgreementKey>,
    verify_shared_secret: Option<[u8; 32]>,
    verify_session_key: Option<DerivedKey>,
    client_ephemeral_public_key: Option<[u8; 32]>,
    pub verified: bool,
    // Client's Ed25519 public key (from M5 inner TLV), stored for pair-verify M3
    pub client_public_key: Option<[u8; 32]>,
    // Client's device identifier
    pub client_device_id: Option<String>,
}

type SrpServer = ServerG3072<sha2_011::Sha512>;

struct PairSetupState {
    salt: [u8; 16],
    verifier_bytes: Vec<u8>,
    server_private: Vec<u8>,
    server_public: Vec<u8>,
    is_transient: bool,
}

impl std::fmt::Debug for PairSetupState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PairSetupState")
            .field("is_transient", &self.is_transient)
            .field("server_public_len", &self.server_public.len())
            .finish_non_exhaustive()
    }
}

impl Clone for PairSetupState {
    fn clone(&self) -> Self {
        Self {
            salt: self.salt,
            verifier_bytes: self.verifier_bytes.clone(),
            server_private: self.server_private.clone(),
            server_public: self.server_public.clone(),
            is_transient: self.is_transient,
        }
    }
}

impl PartialEq for PairSetupState {
    fn eq(&self, other: &Self) -> bool {
        self.salt == other.salt
            && self.verifier_bytes == other.verifier_bytes
            && self.server_private == other.server_private
            && self.server_public == other.server_public
            && self.is_transient == other.is_transient
    }
}

impl Eq for PairSetupState {}

impl PairingSession {
    pub fn shared_secret(&self) -> Option<&[u8; 32]> {
        self.verify_shared_secret.as_ref().filter(|_| self.verified)
    }

    /// Raw SRP session key K, for HKDF key derivation in M5/M6
    pub fn session_key(&self) -> Option<&[u8]> {
        self.setup_session_key.as_deref()
    }
}

/// A stored client pairing entry.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct PairedClient {
    pub identifier: String,
    #[serde(with = "hex_serde")]
    pub public_key: [u8; 32],
    pub added_at: String,
}

/// Persistent pairing database.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct PairingDatabase {
    pub allowed_clients: Vec<PairedClient>,
}

impl PairingDatabase {
    pub fn load(path: Option<&Path>) -> Self {
        let Some(path) = path else {
            return Self {
                allowed_clients: Vec::new(),
            };
        };
        match std::fs::read_to_string(path) {
            Ok(content) => match serde_json::from_str(&content) {
                Ok(db) => db,
                Err(e) => {
                    warn!(%e, "failed to parse pairing DB, starting fresh");
                    Self {
                        allowed_clients: Vec::new(),
                    }
                }
            },
            Err(_) => Self {
                allowed_clients: Vec::new(),
            },
        }
    }

    pub fn save(&self, path: Option<&Path>) {
        let Some(path) = path else {
            return;
        };
        if let Ok(content) = serde_json::to_string_pretty(self) {
            let _ = std::fs::write(path, &content);
        }
    }

    pub fn find_client(&self, identifier: &str) -> Option<&PairedClient> {
        self.allowed_clients
            .iter()
            .find(|c| c.identifier == identifier)
    }

    pub fn add_client(&mut self, identifier: String, public_key: [u8; 32]) {
        if self
            .allowed_clients
            .iter()
            .any(|c| c.identifier == identifier)
        {
            return;
        }
        let now = std::time::UNIX_EPOCH
            .elapsed()
            .map(|d| d.as_secs().to_string())
            .unwrap_or_else(|_| "0".to_string());
        self.allowed_clients.push(PairedClient {
            identifier,
            public_key,
            added_at: now,
        });
    }

    pub fn remove_client(&mut self, identifier: &str) -> bool {
        let len = self.allowed_clients.len();
        self.allowed_clients.retain(|c| c.identifier != identifier);
        self.allowed_clients.len() < len
    }
}

pub struct PairingService {
    identity: IdentityKey,
    device_id: String,
    pin_text: String,
    db_path: Option<std::path::PathBuf>,
    pub db: parking_lot::RwLock<PairingDatabase>,
}

impl PairingService {
    pub fn new(
        identity: IdentityKey,
        device_id: impl Into<String>,
        pin_text: impl Into<String>,
        db_path: Option<impl Into<std::path::PathBuf>>,
    ) -> Self {
        let device_id = device_id.into();
        let pin_text = pin_text.into();
        let db_path = db_path.map(|p| p.into());
        let db = PairingDatabase::load(db_path.as_deref());
        Self {
            identity,
            device_id,
            pin_text,
            db_path,
            db: parking_lot::RwLock::new(db),
        }
    }

    pub fn handle(
        &self,
        session: &mut PairingSession,
        endpoint: PairingEndpoint,
        body: &[u8],
    ) -> PairingReply {
        let incoming = Tlv::parse(body);
        let requested_state = incoming
            .first(TLV_STATE)
            .and_then(|state| state.first().copied())
            .unwrap_or(1);
        debug!(
            ?endpoint,
            requested_state,
            body_len = body.len(),
            tlv = %incoming.debug_summary(),
            session_verified = session.verified,
            has_setup_key = session.setup_session_key.is_some(),
            has_verify_key = session.verify_session_key.is_some(),
            "pairing request"
        );

        let out = match endpoint {
            PairingEndpoint::Setup => match requested_state {
                1 => self.setup_m1(session, &incoming),
                3 => self.setup_m3(session, &incoming),
                5 => self.setup_m5(session, &incoming),
                _ => auth_error(requested_state.saturating_add(1).min(6)),
            },
            PairingEndpoint::Verify => match requested_state {
                1 => self.verify_m1(session, &incoming),
                3 => self.verify_m3(session, &incoming),
                _ => auth_error(requested_state.saturating_add(1).min(4)),
            },
            PairingEndpoint::Add => self.pair_add(session, &incoming),
            PairingEndpoint::Remove => self.pair_remove(&incoming),
            PairingEndpoint::List => self.pair_list(),
        };
        debug!(
            ?endpoint,
            requested_state,
            response_tlv = %out.debug_summary(),
            has_error = out.first(TLV_ERROR).is_some(),
            "pairing response"
        );

        let code = if out.first(TLV_ERROR).is_some() {
            200 // Appple uses 200 with error TLV
        } else {
            200
        };

        PairingReply {
            status_code: code,
            body: out.encode(),
        }
    }

    fn setup_m1(&self, session: &mut PairingSession, incoming: &Tlv) -> Tlv {
        let method = incoming
            .first(TLV_METHOD)
            .and_then(|value| value.first().copied())
            .unwrap_or(0);
        if method != 0 {
            warn!(method, "unsupported pair-setup method");
            return auth_error(2);
        }

        let is_transient = incoming
            .first(TLV_FLAGS)
            .and_then(|value| value.first().copied())
            .is_some_and(|flags| flags & PAIRING_FLAGS_TRANSIENT != 0);
        let flags = incoming
            .first(TLV_FLAGS)
            .and_then(|value| value.first().copied())
            .unwrap_or(0);

        let pin_bytes = self.pin_text.as_bytes();
        let server = ServerG3072::<SrpSha512>::new_with_options(true);
        let client = ClientG3072::<SrpSha512>::new_with_options(true);
        let salt = random_salt();
        let server_secret = random_server_secret();
        let verifier = client.compute_verifier(PAIR_SETUP_USERNAME, pin_bytes, &salt);
        let server_public = server.compute_public_ephemeral(&server_secret, &verifier);

        debug!(
            method,
            flags = format_args!("0x{flags:02x}"),
            is_transient,
            srp_username_in_x = true,
            srp_pad_g_in_m1 = false,
            pin_len = pin_bytes.len(),
            salt = %hex_prefix(&salt, 16),
            verifier_len = verifier.len(),
            verifier = %hex_prefix(&verifier, 16),
            server_private_len = server_secret.len(),
            server_private = %hex_prefix(&server_secret, 16),
            server_public_len = server_public.len(),
            server_public = %hex_prefix(&server_public, 16),
            "pair-setup M1 accepted"
        );

        session.setup = Some(PairSetupState {
            salt,
            verifier_bytes: verifier,
            server_private: server_secret.to_vec(),
            server_public: server_public.clone(),
            is_transient,
        });

        let mut out = Tlv::default();
        out.insert(TLV_STATE, [2]);
        out.insert(TLV_SALT, salt);
        out.insert(TLV_PUBLIC_KEY, server_public);
        out
    }

    fn setup_m3(&self, session: &mut PairingSession, incoming: &Tlv) -> Tlv {
        let Some(setup) = session.setup.as_mut() else {
            warn!("pair-setup M3 received before M1 setup state");
            return auth_error(4);
        };
        let Some(client_public) = incoming.joined(TLV_PUBLIC_KEY) else {
            warn!(
                tlv = %incoming.debug_summary(),
                "pair-setup M3 missing client public key"
            );
            return auth_error(4);
        };
        let Some(client_proof) = incoming.joined(TLV_PROOF) else {
            warn!(
                tlv = %incoming.debug_summary(),
                "pair-setup M3 missing client proof"
            );
            return auth_error(4);
        };
        debug!(
            is_transient = setup.is_transient,
            salt = %hex_prefix(&setup.salt, 16),
            verifier_len = setup.verifier_bytes.len(),
            verifier = %hex_prefix(&setup.verifier_bytes, 16),
            server_private_len = setup.server_private.len(),
            server_private = %hex_prefix(&setup.server_private, 16),
            server_public_len = setup.server_public.len(),
            server_public = %hex_prefix(&setup.server_public, 16),
            client_public_len = client_public.len(),
            client_public = %hex_prefix(&client_public, 16),
            client_proof_len = client_proof.len(),
            client_proof = %hex_prefix(&client_proof, 16),
            "pair-setup M3 SRP input"
        );

        let server = SrpServer::new_with_options(true);
        let verifier = match server.process_reply(
            PAIR_SETUP_USERNAME,
            &setup.salt,
            &setup.server_private,
            &setup.verifier_bytes,
            &client_public,
        ) {
            Ok(v) => {
                debug!(
                    premaster_len = v.key().len(),
                    premaster = %hex_prefix(v.key(), 16),
                    server_proof_len = v.proof().len(),
                    server_proof = %hex_prefix(v.proof(), 16),
                    "pair-setup M3 SRP verifier built"
                );
                v
            }
            Err(err) => {
                warn!(
                    %err,
                    client_public_len = client_public.len(),
                    client_public = %hex_prefix(&client_public, 16),
                    server_public_len = setup.server_public.len(),
                    server_public = %hex_prefix(&setup.server_public, 16),
                    "pair-setup SRP reply rejected"
                );
                return auth_error(4);
            }
        };

        let session_key = match verifier.verify_client(&client_proof) {
            Ok(session_key) => {
                debug!(
                    session_key_len = session_key.len(),
                    session_key = %hex_prefix(session_key, 16),
                    server_proof_len = verifier.proof().len(),
                    server_proof = %hex_prefix(verifier.proof(), 16),
                    "pair-setup M3 client proof verified"
                );
                session_key.to_vec()
            }
            Err(err) => {
                warn!(
                    %err,
                    client_proof_len = client_proof.len(),
                    client_proof = %hex_prefix(&client_proof, 32),
                    server_public_len = setup.server_public.len(),
                    server_public = %hex_prefix(&setup.server_public, 16),
                    premaster_len = verifier.key().len(),
                    premaster = %hex_prefix(verifier.key(), 16),
                    server_proof_len = verifier.proof().len(),
                    server_proof = %hex_prefix(verifier.proof(), 16),
                    "pair-setup client proof rejected"
                );
                return auth_error(4);
            }
        };

        info!("pair-setup SRP proof verified, session key established");

        if setup.is_transient {
            session.setup_session_key = Some(session_key);
            session.verified = true;
        }

        let mut out = Tlv::default();
        out.insert(TLV_STATE, [4]);
        out.insert(TLV_PROOF, verifier.proof().to_vec());
        out
    }

    /// M5: Client sends encrypted TLV (State=5, EncryptedData).
    /// Inner TLV: Identifier, PublicKey, Signature.
    fn setup_m5(&self, session: &mut PairingSession, incoming: &Tlv) -> Tlv {
        let session_key = match session.session_key() {
            Some(k) => k.to_vec(),
            None => {
                warn!("pair-setup M5 received without SRP session key");
                return auth_error(6);
            }
        };
        let Some(encrypted_data) = incoming.joined(TLV_ENCRYPTED_DATA) else {
            warn!(
                tlv = %incoming.debug_summary(),
                "pair-setup M5 missing encrypted data"
            );
            return auth_error(6);
        };
        debug!(
            session_key_len = session_key.len(),
            session_key = %hex_prefix(&session_key, 16),
            encrypted_len = encrypted_data.len(),
            encrypted = %hex_prefix(&encrypted_data, 24),
            "pair-setup M5 decrypt input"
        );

        // Derive encryption key: HKDF(session_key, "Pair-Setup-Encrypt-Salt", "Pair-Setup-Encrypt-Info")
        let enc_key = hkdf_sha512(
            &session_key,
            b"Pair-Setup-Encrypt-Salt",
            b"Pair-Setup-Encrypt-Info",
        );
        let nonce = nonce_from_label(b"PS-Msg05");

        // Decrypt
        let plaintext = match chacha_open(&enc_key, &nonce, &[], &encrypted_data) {
            Ok(p) => {
                debug!(
                    plaintext_len = p.len(),
                    plaintext = %hex_prefix(&p, 24),
                    "pair-setup M5 decrypted"
                );
                p
            }
            Err(e) => {
                warn!(
                    %e,
                    enc_key = %hex_prefix(&enc_key.0, 16),
                    nonce = %hex_prefix(&nonce, 12),
                    encrypted_len = encrypted_data.len(),
                    encrypted = %hex_prefix(&encrypted_data, 24),
                    "pair-setup M5 decryption failed"
                );
                return auth_error(6);
            }
        };

        let inner = Tlv::parse(&plaintext);
        debug!(inner_tlv = %inner.debug_summary(), "pair-setup M5 inner TLV");
        let Some(client_id) = inner
            .first(TLV_IDENTIFIER)
            .map(|v| String::from_utf8_lossy(v).to_string())
        else {
            warn!(inner_tlv = %inner.debug_summary(), "pair-setup M5 missing client id");
            return auth_error(6);
        };
        let Some(client_pk) = inner.first(TLV_PUBLIC_KEY).and_then(as_32_bytes) else {
            warn!(inner_tlv = %inner.debug_summary(), "pair-setup M5 missing client public key");
            return auth_error(6);
        };
        let Some(client_sig) = inner.first(TLV_SIGNATURE).and_then(as_64_bytes) else {
            warn!(inner_tlv = %inner.debug_summary(), "pair-setup M5 missing client signature");
            return auth_error(6);
        };

        // Derive device_x: HKDF(session_key, "Pair-Setup-Controller-Sign-Salt", "Pair-Setup-Controller-Sign-Info")
        let device_x = hkdf_sha512(
            &session_key,
            b"Pair-Setup-Controller-Sign-Salt",
            b"Pair-Setup-Controller-Sign-Info",
        );

        // Build signed info: device_x(32) || client_id || client_pk(32)
        let mut signed_info = Vec::with_capacity(32 + client_id.len() + 32);
        signed_info.extend_from_slice(&device_x.0);
        signed_info.extend_from_slice(client_id.as_bytes());
        signed_info.extend_from_slice(&client_pk);

        // Verify Ed25519 signature
        let vk = ed25519_dalek::VerifyingKey::from_bytes(&client_pk)
            .map_err(|_| "invalid client public key")
            .ok();
        let Some(vk) = vk else {
            warn!(
                client_id,
                client_pk = %hex_prefix(&client_pk, 16),
                "pair-setup M5: invalid client Ed25519 public key"
            );
            return auth_error(6);
        };
        let sig = ed25519_dalek::Signature::from_bytes(&client_sig);
        if vk.verify_strict(&signed_info, &sig).is_err() {
            warn!(
                client_id,
                client_pk = %hex_prefix(&client_pk, 16),
                client_sig = %hex_prefix(&client_sig, 16),
                signed_info_len = signed_info.len(),
                signed_info = %hex_prefix(&signed_info, 24),
                "pair-setup M5: Ed25519 signature verification failed"
            );
            return auth_error(6);
        }

        debug!(
            client_id,
            client_pk = %hex_prefix(&client_pk, 16),
            client_sig = %hex_prefix(&client_sig, 16),
            "pair-setup M5 client signature verified"
        );
        // Store client info
        session.client_device_id = Some(client_id);
        session.client_public_key = Some(client_pk);

        // Build M6: encrypt server identity + signature
        self.setup_m6(session, &session_key)
    }

    /// M6: Server sends encrypted TLV with server identity + Ed25519 signature.
    fn setup_m6(&self, _session: &PairingSession, session_key: &[u8]) -> Tlv {
        // Derive device_x for accessory: HKDF(session_key, "Pair-Setup-Accessory-Sign-Salt", "Pair-Setup-Accessory-Sign-Info")
        let device_x = hkdf_sha512(
            session_key,
            b"Pair-Setup-Accessory-Sign-Salt",
            b"Pair-Setup-Accessory-Sign-Info",
        );

        // Build signed info: device_x(32) || device_id || server_pk(32)
        let pk = self.identity.verifying_key();
        let mut signed_info = Vec::with_capacity(32 + self.device_id.len() + 32);
        signed_info.extend_from_slice(&device_x.0);
        signed_info.extend_from_slice(self.device_id.as_bytes());
        signed_info.extend_from_slice(&pk);

        // Sign with server identity key
        let sig = self.identity.sign(&signed_info);

        let mut inner = Tlv::default();
        inner.insert(TLV_IDENTIFIER, self.device_id.as_bytes());
        inner.insert(TLV_PUBLIC_KEY, pk);
        inner.insert(TLV_SIGNATURE, &sig[..]);
        let inner_encoded = inner.encode();
        debug!(
            device_id = %self.device_id,
            accessory_pk = %hex_prefix(&pk, 16),
            accessory_sig = %hex_prefix(&sig, 16),
            signed_info_len = signed_info.len(),
            signed_info = %hex_prefix(&signed_info, 24),
            inner_tlv = %inner.debug_summary(),
            inner_len = inner_encoded.len(),
            "pair-setup M6 inner TLV"
        );

        // Derive encryption key for M6
        let enc_key = hkdf_sha512(
            session_key,
            b"Pair-Setup-Encrypt-Salt",
            b"Pair-Setup-Encrypt-Info",
        );
        let nonce = nonce_from_label(b"PS-Msg06");

        let encrypted = match chacha_seal(&enc_key, &nonce, &[], &inner_encoded) {
            Ok(e) => {
                debug!(
                    enc_key = %hex_prefix(&enc_key.0, 16),
                    nonce = %hex_prefix(&nonce, 12),
                    encrypted_len = e.len(),
                    encrypted = %hex_prefix(&e, 24),
                    "pair-setup M6 encrypted"
                );
                e
            }
            Err(e) => {
                warn!(%e, "pair-setup M6 encryption failed");
                return auth_error(6);
            }
        };

        let mut out = Tlv::default();
        out.insert(TLV_STATE, [6]);
        out.insert(TLV_ENCRYPTED_DATA, encrypted);
        out
    }

    fn verify_m1(&self, session: &mut PairingSession, incoming: &Tlv) -> Tlv {
        let Some(client_public) = incoming.first(TLV_PUBLIC_KEY).and_then(as_32_bytes) else {
            warn!(
                tlv = %incoming.debug_summary(),
                "pair-verify M1 missing client public key"
            );
            return auth_error(2);
        };
        let agreement = AgreementKey::generate();
        let public = agreement.public_key();
        let shared_secret = agreement.shared_secret(&client_public);
        let session_key = hkdf_sha512(
            &shared_secret,
            b"Pair-Verify-Encrypt-Salt",
            b"Pair-Verify-Encrypt-Info",
        );

        let mut info = Vec::with_capacity(32 + self.device_id.len() + 32);
        info.extend_from_slice(&public);
        info.extend_from_slice(self.device_id.as_bytes());
        info.extend_from_slice(&client_public);

        let mut sub_tlv = Tlv::default();
        sub_tlv.insert(TLV_IDENTIFIER, self.device_id.as_bytes());
        sub_tlv.insert(TLV_SIGNATURE, self.identity.sign(&info));

        let encrypted = match seal(
            &session_key,
            &nonce_from_label(b"PV-Msg02"),
            &[],
            &sub_tlv.encode(),
        ) {
            Ok(encrypted) => encrypted,
            Err(e) => {
                warn!(%e, "pair-verify M2 encryption failed");
                return auth_error(2);
            }
        };
        debug!(
            client_public = %hex_prefix(&client_public, 16),
            server_public = %hex_prefix(&public, 16),
            shared_secret = %hex_prefix(&shared_secret, 16),
            session_key = %hex_prefix(&session_key.0, 16),
            encrypted_len = encrypted.len(),
            encrypted = %hex_prefix(&encrypted, 24),
            "pair-verify M1 accepted"
        );

        session.verify_agreement = Some(agreement);
        session.verify_shared_secret = Some(shared_secret);
        session.verify_session_key = Some(session_key);
        session.client_ephemeral_public_key = Some(client_public);

        let mut out = Tlv::default();
        out.insert(TLV_STATE, [2]);
        out.insert(TLV_PUBLIC_KEY, public);
        out.insert(TLV_ENCRYPTED_DATA, encrypted);
        out
    }

    fn verify_m3(&self, session: &mut PairingSession, incoming: &Tlv) -> Tlv {
        let Some(session_key) = session.verify_session_key.as_ref() else {
            warn!("pair-verify M3 received without session key");
            return auth_error(4);
        };
        let Some(encrypted) = incoming.joined(TLV_ENCRYPTED_DATA) else {
            warn!(
                tlv = %incoming.debug_summary(),
                "pair-verify M3 missing encrypted data"
            );
            return auth_error(4);
        };
        let decrypted = match open(session_key, &nonce_from_label(b"PV-Msg03"), &[], &encrypted) {
            Ok(decrypted) => decrypted,
            Err(e) => {
                warn!(
                    %e,
                    session_key = %hex_prefix(&session_key.0, 16),
                    encrypted_len = encrypted.len(),
                    encrypted = %hex_prefix(&encrypted, 24),
                    "pair-verify M3 decryption failed"
                );
                return auth_error(4);
            }
        };

        let inner = Tlv::parse(&decrypted);
        debug!(
            decrypted_len = decrypted.len(),
            decrypted = %hex_prefix(&decrypted, 24),
            inner_tlv = %inner.debug_summary(),
            "pair-verify M3 decrypted"
        );
        let Some(client_id) = inner.first(TLV_IDENTIFIER) else {
            warn!(inner_tlv = %inner.debug_summary(), "pair-verify M3 missing client id");
            return auth_error(4);
        };
        let Some(client_sig_raw) = inner.first(TLV_SIGNATURE) else {
            warn!(inner_tlv = %inner.debug_summary(), "pair-verify M3 missing client signature");
            return auth_error(4);
        };
        let Ok(client_sig) = <[u8; 64]>::try_from(client_sig_raw) else {
            warn!(
                signature_len = client_sig_raw.len(),
                signature = %hex_prefix(client_sig_raw, 16),
                "pair-verify M3 invalid client signature length"
            );
            return auth_error(4);
        };

        let Some(client_public) = session.client_ephemeral_public_key else {
            return auth_error(4);
        };
        let Some(ref agreement) = session.verify_agreement else {
            return auth_error(4);
        };

        let our_pub = agreement.public_key();

        let mut signed_message = Vec::with_capacity(32 + 32 + client_id.len());
        signed_message.extend_from_slice(&client_public);
        signed_message.extend_from_slice(&our_pub);
        signed_message.extend_from_slice(client_id);

        let client_identifier = String::from_utf8_lossy(client_id);
        let db = self.db.read();
        let verified = match db.find_client(&client_identifier) {
            Some(stored) => IdentityKey::verify(&stored.public_key, &signed_message, &client_sig),
            None => {
                warn!(identifier = %client_identifier, "client not found in pairing DB");
                false
            }
        };

        if !verified {
            warn!(
                identifier = %client_identifier,
                client_public = %hex_prefix(&client_public, 16),
                server_public = %hex_prefix(&our_pub, 16),
                client_sig = %hex_prefix(&client_sig, 16),
                signed_message_len = signed_message.len(),
                signed_message = %hex_prefix(&signed_message, 24),
                "pair-verify: client signature verification failed"
            );
            return auth_error(4);
        }

        debug!(identifier = %client_identifier, "pair-verify: client authenticated");
        session.verified = true;
        let mut out = Tlv::default();
        out.insert(TLV_STATE, [4]);
        out
    }

    fn pair_add(&self, session: &PairingSession, incoming: &Tlv) -> Tlv {
        if !session.verified {
            warn!("pair-add attempted without verified session");
            return auth_error(2);
        }
        let Some(encrypted) = incoming.joined(TLV_ENCRYPTED_DATA) else {
            return auth_error(2);
        };
        let Some(session_key) = session.verify_session_key.as_ref() else {
            return auth_error(2);
        };
        let decrypted = match open(session_key, &nonce_from_label(b"PA-Msg04"), &[], &encrypted) {
            Ok(d) => d,
            Err(_) => return auth_error(2),
        };
        let inner = Tlv::parse(&decrypted);
        let Some(identifier) = inner
            .first(TLV_IDENTIFIER)
            .map(|v| String::from_utf8_lossy(v).to_string())
        else {
            return auth_error(2);
        };
        let Some(pk) = inner.first(TLV_PUBLIC_KEY).and_then(as_32_bytes) else {
            return auth_error(2);
        };

        let mut db = self.db.write();
        db.add_client(identifier.clone(), pk);
        db.save(self.db_path.as_deref());
        info!(identifier, "client paired");
        let mut out = Tlv::default();
        out.insert(TLV_STATE, [1]);
        out
    }

    fn pair_remove(&self, incoming: &Tlv) -> Tlv {
        // pair-remove requires an already-authenticated session (key verified)
        // Simplified: use the incoming encrypted data similarly to pair-add
        let Some(encrypted) = incoming.joined(TLV_ENCRYPTED_DATA) else {
            return auth_error(2);
        };
        let inner = Tlv::parse(&encrypted);
        let Some(identifier) = inner
            .first(TLV_IDENTIFIER)
            .map(|v| String::from_utf8_lossy(v).to_string())
        else {
            return auth_error(2);
        };

        let mut db = self.db.write();
        if db.remove_client(&identifier) {
            db.save(self.db_path.as_deref());
            info!(identifier, "client unpaired");
        }
        let mut out = Tlv::default();
        out.insert(TLV_STATE, [1]);
        out
    }

    fn pair_list(&self) -> Tlv {
        let db = self.db.read();
        let mut out = Tlv::default();
        out.insert(TLV_STATE, [1]);
        for client in &db.allowed_clients {
            let mut entry = Vec::with_capacity(1 + client.identifier.len() + 1 + 32);
            entry.push(TLV_IDENTIFIER);
            entry.push(client.identifier.len() as u8);
            entry.extend_from_slice(client.identifier.as_bytes());
            entry.push(TLV_PUBLIC_KEY);
            entry.push(32);
            entry.extend_from_slice(&client.public_key);
            out.insert(0x0f, entry);
        }
        out
    }
}

fn auth_error(state: u8) -> Tlv {
    let mut out = Tlv::default();
    out.insert(TLV_STATE, [state]);
    out.insert(TLV_ERROR, [TLV_ERROR_AUTHENTICATION]);
    out
}

fn random_salt() -> [u8; 16] {
    let mut salt = [0u8; 16];
    OsRng.fill_bytes(&mut salt);
    salt
}

fn random_server_secret() -> [u8; 48] {
    let mut secret = [0u8; 48];
    OsRng.fill_bytes(&mut secret);
    secret
}

fn as_32_bytes(value: &[u8]) -> Option<[u8; 32]> {
    value.try_into().ok()
}

fn as_64_bytes(value: &[u8]) -> Option<[u8; 64]> {
    value.try_into().ok()
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

/// Chacha20-Poly1305 encrypt. Returns ciphertext || 16-byte tag.
fn chacha_seal(
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

/// Chacha20-Poly1305 decrypt. Expects ciphertext || 16-byte tag.
fn chacha_open(
    key: &DerivedKey,
    nonce: &[u8; 12],
    aad: &[u8],
    ciphertext_with_tag: &[u8],
) -> anyhow::Result<Vec<u8>> {
    let cipher = ChaCha20Poly1305::new((&key.0).into());
    cipher
        .decrypt(
            nonce.into(),
            Payload {
                msg: ciphertext_with_tag,
                aad,
            },
        )
        .map_err(|_| anyhow::anyhow!("chacha20-poly1305 decryption failed"))
}

/// Serde module for hex-encoded 32-byte arrays.
mod hex_serde {
    use serde::{Deserialize, Deserializer, Serializer};

    pub fn serialize<S>(bytes: &[u8; 32], serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let hex = hex_impl(bytes);
        serializer.serialize_str(&hex)
    }

    pub fn deserialize<'de, D>(deserializer: D) -> Result<[u8; 32], D::Error>
    where
        D: Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        let bytes = hex_decode_impl(&s).ok_or_else(|| serde::de::Error::custom("invalid hex"))?;
        if bytes.len() != 32 {
            return Err(serde::de::Error::custom("expected 32 bytes"));
        }
        let mut arr = [0u8; 32];
        arr.copy_from_slice(&bytes);
        Ok(arr)
    }

    fn hex_impl(bytes: &[u8; 32]) -> String {
        let mut s = String::with_capacity(64);
        for &b in bytes {
            s.push_str(&format!("{b:02x}"));
        }
        s
    }

    fn hex_decode_impl(s: &str) -> Option<Vec<u8>> {
        if s.len() % 2 != 0 {
            return None;
        }
        (0..s.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&s[i..i + 2], 16).ok())
            .collect()
    }
}

fn test_pairing_service() -> PairingService {
    let identity = IdentityKey::generate();
    let db = PairingDatabase {
        allowed_clients: Vec::new(),
    };
    PairingService {
        identity,
        device_id: "00:11:22:33:44:55".to_string(),
        pin_text: "3939".to_string(),
        db_path: None,
        db: parking_lot::RwLock::new(db),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::airplay::crypto::{hkdf_sha512, nonce_from_label, open, seal};
    use srp::{
        Group,
        groups::G3072,
        utils::{compute_hash, compute_m1_rfc5054},
    };

    #[test]
    fn pair_setup_m1_returns_srp_salt_and_public_key() {
        let mut input = Tlv::default();
        input.insert(TLV_METHOD, [0]);
        input.insert(TLV_STATE, [1]);
        input.insert(TLV_FLAGS, [PAIRING_FLAGS_TRANSIENT]);

        let service = test_pairing_service();
        let mut session = PairingSession::default();
        let reply = service.handle(&mut session, PairingEndpoint::Setup, &input.encode());
        let tlv = Tlv::parse(&reply.body);

        assert_eq!(reply.status_code, 200);
        assert_eq!(tlv.first(TLV_STATE), Some([2].as_slice()));
        assert_eq!(tlv.first(TLV_SALT).unwrap().len(), 16);
        assert!(tlv.joined(TLV_PUBLIC_KEY).unwrap().len() > 300);
        assert!(tlv.first(TLV_ERROR).is_none());
    }

    #[test]
    fn transient_pair_setup_m3_accepts_parent_style_srp_proof() {
        let service = test_pairing_service();
        let mut session = PairingSession::default();

        let mut m1 = Tlv::default();
        m1.insert(TLV_METHOD, [0]);
        m1.insert(TLV_STATE, [1]);
        m1.insert(TLV_FLAGS, [PAIRING_FLAGS_TRANSIENT]);
        let m2 = service.handle(&mut session, PairingEndpoint::Setup, &m1.encode());
        let m2_tlv = Tlv::parse(&m2.body);
        let salt = m2_tlv.first(TLV_SALT).unwrap();
        let server_public = m2_tlv.joined(TLV_PUBLIC_KEY).unwrap();

        let client = ClientG3072::<SrpSha512>::new_with_options(true);
        let client_secret = [0x7bu8; 48];
        let client_public = client.compute_public_ephemeral(&client_secret);
        let client_verifier = client
            .process_reply(
                &client_secret,
                PAIR_SETUP_USERNAME,
                service.pin_text.as_bytes(),
                salt,
                &server_public,
            )
            .unwrap();
        let session_key = compute_hash::<SrpSha512>(client_verifier.key());
        let proof = compute_m1_rfc5054::<SrpSha512>(
            &G3072::generator(),
            true,
            PAIR_SETUP_USERNAME,
            salt,
            &client_public,
            &server_public,
            session_key.as_slice(),
        );

        let mut m3 = Tlv::default();
        m3.insert(TLV_STATE, [3]);
        m3.insert(TLV_PUBLIC_KEY, client_public);
        m3.insert(TLV_PROOF, proof.as_slice());
        let m4 = service.handle(&mut session, PairingEndpoint::Setup, &m3.encode());
        let m4_tlv = Tlv::parse(&m4.body);

        assert_eq!(m4_tlv.first(TLV_STATE), Some([4].as_slice()));
        assert!(m4_tlv.first(TLV_ERROR).is_none());
        assert_eq!(m4_tlv.first(TLV_PROOF).unwrap().len(), 64);
        assert!(session.verified);
        assert!(session.session_key().is_some());
    }

    #[test]
    fn verify_reply_includes_ephemeral_key_and_signature() {
        let service = test_pairing_service();
        let client = AgreementKey::generate();
        let mut input = Tlv::default();
        input.insert(TLV_STATE, [1]);
        input.insert(TLV_PUBLIC_KEY, client.public_key());
        let mut session = PairingSession::default();
        let reply = service.handle(&mut session, PairingEndpoint::Verify, &input.encode());
        let tlv = Tlv::parse(&reply.body);
        assert_eq!(tlv.first(TLV_PUBLIC_KEY).unwrap().len(), 32);
        assert!(tlv.first(TLV_ENCRYPTED_DATA).unwrap().len() > 64);
        assert!(session.verify_session_key.is_some());
    }

    #[test]
    fn pair_setup_m6_includes_accessory_public_key() {
        let service = test_pairing_service();
        let session_key = [0x42u8; 64];
        let reply_tlv = service.setup_m6(&PairingSession::default(), &session_key);
        let encrypted = reply_tlv.joined(TLV_ENCRYPTED_DATA).unwrap();
        let enc_key = hkdf_sha512(
            &session_key,
            b"Pair-Setup-Encrypt-Salt",
            b"Pair-Setup-Encrypt-Info",
        );
        let plaintext = open(&enc_key, &nonce_from_label(b"PS-Msg06"), &[], &encrypted).unwrap();
        let inner = Tlv::parse(&plaintext);

        assert_eq!(
            inner.first(TLV_IDENTIFIER),
            Some(service.device_id.as_bytes())
        );
        assert_eq!(
            inner.first(TLV_PUBLIC_KEY),
            Some(service.identity.verifying_key().as_slice())
        );
        assert_eq!(inner.first(TLV_SIGNATURE).unwrap().len(), 64);
    }

    #[test]
    fn verify_m3_fails_when_client_not_in_db() {
        let service = test_pairing_service();
        let client = AgreementKey::generate();
        let client_ed25519 = IdentityKey::generate();
        let client_identifier = b"test-client";
        let mut session = PairingSession::default();

        // M1
        let mut m1 = Tlv::default();
        m1.insert(TLV_STATE, [1]);
        m1.insert(TLV_PUBLIC_KEY, client.public_key());
        let m2 = service.handle(&mut session, PairingEndpoint::Verify, &m1.encode());
        let m2_tlv = Tlv::parse(&m2.body);
        let server_public = as_32_bytes(m2_tlv.first(TLV_PUBLIC_KEY).unwrap()).unwrap();

        let shared = client.shared_secret(&server_public);
        let key = hkdf_sha512(
            &shared,
            b"Pair-Verify-Encrypt-Salt",
            b"Pair-Verify-Encrypt-Info",
        );

        // Construct the signed message: client_pub || server_pub || identifier
        let mut signed_msg = Vec::with_capacity(32 + 32 + client_identifier.len());
        signed_msg.extend_from_slice(&client.public_key());
        signed_msg.extend_from_slice(&server_public);
        signed_msg.extend_from_slice(client_identifier);
        let signature = client_ed25519.sign(&signed_msg);

        // M3 with valid signature but client not in DB
        let mut inner = Tlv::default();
        inner.insert(TLV_IDENTIFIER, client_identifier.to_vec());
        inner.insert(TLV_SIGNATURE, signature);

        let encrypted = seal(&key, &nonce_from_label(b"PV-Msg03"), &[], &inner.encode()).unwrap();
        let mut m3 = Tlv::default();
        m3.insert(TLV_STATE, [3]);
        m3.insert(TLV_ENCRYPTED_DATA, encrypted);
        let m4 = service.handle(&mut session, PairingEndpoint::Verify, &m3.encode());
        let m4_tlv = Tlv::parse(&m4.body);

        // Should fail because client is not in DB
        assert!(m4_tlv.first(TLV_ERROR).is_some());
        assert!(!session.verified);
    }

    #[test]
    fn verify_m3_succeeds_when_client_is_paired() {
        let client_ed25519 = IdentityKey::generate();
        let client_identifier = b"test-client";

        // Pre-pair the client
        let mut db = PairingDatabase {
            allowed_clients: Vec::new(),
        };
        db.add_client(
            String::from_utf8_lossy(client_identifier).to_string(),
            client_ed25519.verifying_key(),
        );

        let service = PairingService {
            identity: IdentityKey::generate(),
            device_id: "00:11:22:33:44:55".to_string(),
            pin_text: "3939".to_string(),
            db_path: None,
            db: parking_lot::RwLock::new(db),
        };

        let client = AgreementKey::generate();
        let mut session = PairingSession::default();

        // M1
        let mut m1 = Tlv::default();
        m1.insert(TLV_STATE, [1]);
        m1.insert(TLV_PUBLIC_KEY, client.public_key());
        let m2 = service.handle(&mut session, PairingEndpoint::Verify, &m1.encode());
        let m2_tlv = Tlv::parse(&m2.body);
        let server_public = as_32_bytes(m2_tlv.first(TLV_PUBLIC_KEY).unwrap()).unwrap();

        let shared = client.shared_secret(&server_public);
        let key = hkdf_sha512(
            &shared,
            b"Pair-Verify-Encrypt-Salt",
            b"Pair-Verify-Encrypt-Info",
        );

        let mut signed_msg = Vec::with_capacity(32 + 32 + client_identifier.len());
        signed_msg.extend_from_slice(&client.public_key());
        signed_msg.extend_from_slice(&server_public);
        signed_msg.extend_from_slice(client_identifier);
        let signature = client_ed25519.sign(&signed_msg);

        let mut inner = Tlv::default();
        inner.insert(TLV_IDENTIFIER, client_identifier.to_vec());
        inner.insert(TLV_SIGNATURE, signature);

        let encrypted = seal(&key, &nonce_from_label(b"PV-Msg03"), &[], &inner.encode()).unwrap();
        let mut m3 = Tlv::default();
        m3.insert(TLV_STATE, [3]);
        m3.insert(TLV_ENCRYPTED_DATA, encrypted);
        let m4 = service.handle(&mut session, PairingEndpoint::Verify, &m3.encode());
        let m4_tlv = Tlv::parse(&m4.body);

        assert_eq!(m4_tlv.first(TLV_STATE), Some([4].as_slice()));
        assert!(session.verified);
    }
}
