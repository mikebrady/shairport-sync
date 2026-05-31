use serde::{Deserialize, Serialize};

use crate::airplay::{
    crypto::{AgreementKey, DerivedKey, IdentityKey, hkdf_sha512, nonce_from_label, open, seal},
    tlv::Tlv,
};

pub const TLV_IDENTIFIER: u8 = 1;
pub const TLV_PUBLIC_KEY: u8 = 3;
pub const TLV_ENCRYPTED_DATA: u8 = 5;
pub const TLV_STATE: u8 = 6;
pub const TLV_ERROR: u8 = 7;
pub const TLV_SIGNATURE: u8 = 10;
pub const TLV_ERROR_AUTHENTICATION: u8 = 2;

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
    verify_agreement: Option<AgreementKey>,
    verify_shared_secret: Option<[u8; 32]>,
    verify_session_key: Option<DerivedKey>,
    client_ephemeral_public_key: Option<[u8; 32]>,
    pub verified: bool,
}

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
            PairingEndpoint::Setup => self.setup_reply(requested_state),
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

    fn setup_reply(&self, requested_state: u8) -> Tlv {
        let mut out = Tlv::default();
        out.insert(
            TLV_STATE,
            requested_state.saturating_add(1).min(6).to_be_bytes(),
        );
        out.insert(TLV_PUBLIC_KEY, self.identity.verifying_key());
        out.insert(TLV_ERROR, [TLV_ERROR_AUTHENTICATION]);
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
        let Some(encrypted) = incoming.first(TLV_ENCRYPTED_DATA) else {
            return auth_error(4);
        };
        let decrypted = match open(session_key, &nonce_from_label(b"PV-Msg03"), &[], encrypted) {
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

fn as_32_bytes(value: &[u8]) -> Option<[u8; 32]> {
    value.try_into().ok()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::airplay::crypto::{hkdf_sha512, nonce_from_label, seal};

    #[test]
    fn pairing_placeholder_returns_next_state_and_auth_error() {
        let mut input = Tlv::default();
        input.insert(TLV_STATE, [1]);

        let service = PairingService::default();
        let mut session = PairingSession::default();
        let reply = service.handle(&mut session, PairingEndpoint::Setup, &input.encode());
        let tlv = Tlv::parse(&reply.body);

        assert_eq!(reply.status_code, 200);
        assert_eq!(tlv.first(TLV_STATE), Some([2].as_slice()));
        assert_eq!(
            tlv.first(TLV_ERROR),
            Some([TLV_ERROR_AUTHENTICATION].as_slice())
        );
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
