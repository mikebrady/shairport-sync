use rand_core::{OsRng, RngCore};
use serde::{Deserialize, Serialize};
use sha2_011::Sha512 as SrpSha512;
use srp::{ClientG3072, ServerG3072, ServerVerifier};
use tracing::{debug, warn};

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
const TRANSIENT_PAIRING_PIN: &[u8] = b"3939";
const PAIR_SETUP_USERNAME: &[u8] = b"Pair-Setup";

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub enum PairingEndpoint {
    Setup,
    Verify,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct PairingReply {
    pub status_code: u16,
    pub body: Vec<u8>,
}

#[derive(Default)]
pub struct PairingSession {
    setup: Option<PairSetupState>,
    setup_shared_secret: Option<Vec<u8>>,
    verify_agreement: Option<AgreementKey>,
    verify_shared_secret: Option<[u8; 32]>,
    verify_session_key: Option<DerivedKey>,
    client_ephemeral_public_key: Option<[u8; 32]>,
    pub verified: bool,
}

type SrpServerVerifier = ServerVerifier<SrpSha512>;

struct PairSetupState {
    salt: [u8; 16],
    verifier: Vec<u8>,
    server_secret: [u8; 48],
    server_public: Vec<u8>,
    srp_verifier: Option<SrpServerVerifier>,
    is_transient: bool,
}

impl std::fmt::Debug for PairSetupState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PairSetupState")
            .field("is_transient", &self.is_transient)
            .field("server_public_len", &self.server_public.len())
            .field("verifier_len", &self.verifier.len())
            .finish_non_exhaustive()
    }
}

impl Clone for PairSetupState {
    fn clone(&self) -> Self {
        Self {
            salt: self.salt,
            verifier: self.verifier.clone(),
            server_secret: self.server_secret,
            server_public: self.server_public.clone(),
            srp_verifier: None,
            is_transient: self.is_transient,
        }
    }
}

impl PartialEq for PairSetupState {
    fn eq(&self, other: &Self) -> bool {
        self.salt == other.salt
            && self.verifier == other.verifier
            && self.server_secret == other.server_secret
            && self.server_public == other.server_public
            && self.is_transient == other.is_transient
    }
}

impl Eq for PairSetupState {}

impl PairingSession {
    pub fn shared_secret(&self) -> Option<&[u8; 32]> {
        self.verify_shared_secret.as_ref().filter(|_| self.verified)
    }
}

pub struct PairingService {
    identity: IdentityKey,
    device_id: String,
}

impl PairingService {
    pub fn new(device_id: impl Into<String>) -> Self {
        let device_id = device_id.into();
        let mut seed = [0u8; 32];
        let id = device_id.as_bytes();
        let len = id.len().min(seed.len());
        seed[..len].copy_from_slice(&id[..len]);
        Self {
            identity: IdentityKey::from_seed(seed),
            device_id,
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

        let out = match endpoint {
            PairingEndpoint::Setup => match requested_state {
                1 => self.setup_m1(session, &incoming),
                3 => self.setup_m3(session, &incoming),
                _ => auth_error(requested_state.saturating_add(1).min(6)),
            },
            PairingEndpoint::Verify => match requested_state {
                1 => self.verify_m1(session, &incoming),
                3 => self.verify_m3(session, &incoming),
                _ => auth_error(requested_state.saturating_add(1).min(4)),
            },
        };

        PairingReply {
            status_code: 200,
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
        let server = ServerG3072::<SrpSha512>::new_with_options(true);
        let client = ClientG3072::<SrpSha512>::new();
        let salt = random_salt();
        let server_secret = random_server_secret();
        let verifier = client.compute_verifier(PAIR_SETUP_USERNAME, TRANSIENT_PAIRING_PIN, &salt);
        let server_public = server.compute_public_ephemeral(&server_secret, &verifier);

        session.setup = Some(PairSetupState {
            salt,
            verifier,
            server_secret,
            server_public: server_public.clone(),
            srp_verifier: None,
            is_transient,
        });

        debug!(is_transient, "pair-setup M1 accepted");
        let mut out = Tlv::default();
        out.insert(TLV_STATE, [2]);
        out.insert(TLV_SALT, salt);
        out.insert(TLV_PUBLIC_KEY, server_public);
        out
    }

    fn setup_m3(&self, session: &mut PairingSession, incoming: &Tlv) -> Tlv {
        let Some(setup) = session.setup.as_mut() else {
            return auth_error(4);
        };
        let Some(client_public) = incoming.joined(TLV_PUBLIC_KEY) else {
            return auth_error(4);
        };
        let Some(client_proof) = incoming.joined(TLV_PROOF) else {
            return auth_error(4);
        };

        let server = ServerG3072::<SrpSha512>::new_with_options(true);
        let verifier = match server.process_reply(
            PAIR_SETUP_USERNAME,
            &setup.salt,
            &setup.server_secret,
            &setup.verifier,
            &client_public,
        ) {
            Ok(verifier) => verifier,
            Err(err) => {
                warn!(%err, "pair-setup SRP reply rejected");
                return auth_error(4);
            }
        };
        let shared_secret = match verifier.verify_client(&client_proof) {
            Ok(shared_secret) => shared_secret.to_vec(),
            Err(err) => {
                warn!(%err, "pair-setup client proof rejected");
                return auth_error(4);
            }
        };
        if setup.is_transient {
            session.setup_shared_secret = Some(shared_secret);
            session.verified = true;
        }

        let mut out = Tlv::default();
        out.insert(TLV_STATE, [4]);
        let proof = verifier.proof().to_vec();
        out.insert(TLV_PROOF, proof);
        setup.srp_verifier = Some(verifier);
        out
    }

    fn verify_m1(&self, session: &mut PairingSession, incoming: &Tlv) -> Tlv {
        let Some(client_public) = incoming.first(TLV_PUBLIC_KEY).and_then(as_32_bytes) else {
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
            Err(_) => return auth_error(2),
        };

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
            return auth_error(4);
        };
        let Some(encrypted) = incoming.joined(TLV_ENCRYPTED_DATA) else {
            return auth_error(4);
        };
        let decrypted = match open(session_key, &nonce_from_label(b"PV-Msg03"), &[], &encrypted) {
            Ok(decrypted) => decrypted,
            Err(_) => return auth_error(4),
        };

        let inner = Tlv::parse(&decrypted);
        if inner.first(TLV_IDENTIFIER).is_none() || inner.first(TLV_SIGNATURE).is_none() {
            return auth_error(4);
        }

        session.verified = true;
        let mut out = Tlv::default();
        out.insert(TLV_STATE, [4]);
        out
    }
}

impl Default for PairingService {
    fn default() -> Self {
        Self::new("00:11:22:33:44:55")
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::airplay::crypto::{hkdf_sha512, nonce_from_label, seal};

    #[test]
    fn pair_setup_m1_returns_srp_salt_and_public_key() {
        let mut input = Tlv::default();
        input.insert(TLV_METHOD, [0]);
        input.insert(TLV_STATE, [1]);
        input.insert(TLV_FLAGS, [PAIRING_FLAGS_TRANSIENT]);

        let service = PairingService::default();
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
    fn verify_reply_includes_ephemeral_key_and_signature() {
        let service = PairingService::default();
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
    fn verify_m3_completes_after_encrypted_client_data() {
        let service = PairingService::default();
        let client = AgreementKey::generate();
        let mut session = PairingSession::default();

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
        let mut inner = Tlv::default();
        inner.insert(TLV_IDENTIFIER, b"client");
        inner.insert(TLV_SIGNATURE, [0u8; 64]);

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
