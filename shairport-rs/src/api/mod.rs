use axum::{
    Json, Router,
    extract::{
        Path, State,
        ws::{Message, WebSocket, WebSocketUpgrade},
    },
    response::Response,
    routing::{get, post},
};
use serde::{Deserialize, Serialize};
use tokio::sync::broadcast;

use crate::{
    audio::{AudioEngine, AudioEngineStatus, AudioManager, SelectAudioDeviceRequest},
    mdns::MdnsAdvertiser,
    state::{AppState, StateSnapshot},
};

#[derive(Clone)]
pub struct ApiContext {
    state: AppState,
    audio: AudioManager,
    audio_engine: AudioEngine,
    mdns: MdnsAdvertiser,
}

#[derive(Debug, Deserialize)]
pub struct SetVolumeRequest {
    pub db: f64,
}

#[derive(Debug, Serialize)]
struct CommandResponse {
    accepted: bool,
    command: String,
    message: String,
}

#[derive(Debug, Serialize)]
struct ArtworkResponse {
    artwork_url: Option<String>,
}

impl ApiContext {
    pub fn new(
        state: AppState,
        audio: AudioManager,
        audio_engine: AudioEngine,
        mdns: MdnsAdvertiser,
    ) -> Self {
        Self {
            state,
            audio,
            audio_engine,
            mdns,
        }
    }
}

pub fn router(context: ApiContext) -> Router {
    Router::new()
        .route("/api/v1/state", get(get_state))
        .route("/api/v1/artwork", get(get_artwork))
        .route("/api/v1/audio/devices", get(get_audio_devices))
        .route("/api/v1/audio/status", get(get_audio_status))
        .route("/api/v1/audio/device", post(select_audio_device))
        .route("/api/v1/mdns/status", get(get_mdns_status))
        .route("/api/v1/volume", post(set_volume))
        .route("/api/v1/session/drop", post(drop_session))
        .route("/api/v1/remote/{command}", post(remote_command))
        .route("/api/v1/events", get(events))
        .with_state(context)
}

async fn get_audio_status(State(context): State<ApiContext>) -> Json<AudioEngineStatus> {
    Json(context.audio_engine.status())
}

async fn get_state(State(context): State<ApiContext>) -> Json<StateSnapshot> {
    Json(context.state.snapshot())
}

async fn get_artwork(State(context): State<ApiContext>) -> Json<ArtworkResponse> {
    Json(ArtworkResponse {
        artwork_url: context.state.snapshot().track.artwork_url,
    })
}

async fn get_audio_devices(
    State(context): State<ApiContext>,
) -> Json<Vec<crate::audio::AudioDevice>> {
    let devices = context.audio.list_devices();
    context.state.update_audio_devices(devices.clone());
    Json(devices)
}

async fn select_audio_device(
    State(context): State<ApiContext>,
    Json(request): Json<SelectAudioDeviceRequest>,
) -> Json<StateSnapshot> {
    context.state.select_audio_device(request.device_id);
    Json(context.state.snapshot())
}

async fn get_mdns_status(State(context): State<ApiContext>) -> Json<crate::state::MdnsState> {
    let _ = &context.mdns;
    Json(context.state.snapshot().mdns)
}

async fn set_volume(
    State(context): State<ApiContext>,
    Json(request): Json<SetVolumeRequest>,
) -> Json<StateSnapshot> {
    context.state.set_volume(request.db);
    Json(context.state.snapshot())
}

async fn drop_session(State(context): State<ApiContext>) -> Json<CommandResponse> {
    let _ = context;
    Json(CommandResponse {
        accepted: false,
        command: "drop-session".to_string(),
        message: "AirPlay session control is not implemented yet".to_string(),
    })
}

async fn remote_command(
    State(_context): State<ApiContext>,
    Path(command): Path<String>,
) -> Json<CommandResponse> {
    Json(CommandResponse {
        accepted: false,
        command,
        message: "AirPlay remote control is not implemented yet".to_string(),
    })
}

async fn events(State(context): State<ApiContext>, ws: WebSocketUpgrade) -> Response {
    ws.on_upgrade(move |socket| event_socket(socket, context.state.subscribe()))
}

async fn event_socket(mut socket: WebSocket, mut receiver: broadcast::Receiver<StateSnapshot>) {
    while let Ok(snapshot) = receiver.recv().await {
        match serde_json::to_string(&snapshot) {
            Ok(payload) => {
                if socket.send(Message::Text(payload.into())).await.is_err() {
                    break;
                }
            }
            Err(_) => {
                let _ = socket
                    .send(Message::Text(
                        "{\"error\":\"state serialization failed\"}".into(),
                    ))
                    .await;
                break;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use axum::{
        body::Body,
        http::{Request, StatusCode},
    };
    use tower::ServiceExt;

    use super::*;
    use crate::{audio::AudioManager, config::Config, mdns::MdnsBackend};

    #[tokio::test]
    async fn state_endpoint_returns_ok() {
        let config = Config::default();
        let state = AppState::new(config.clone());
        let app = router(ApiContext::new(
            state,
            AudioManager::new(config.audio.clone()),
            AudioEngine::new(16),
            MdnsAdvertiser::new(MdnsBackend::Off, config.mdns),
        ));

        let response = app
            .oneshot(
                Request::builder()
                    .uri("/api/v1/state")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
    }
}
