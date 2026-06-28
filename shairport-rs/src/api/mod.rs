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
use std::time::SystemTime;
use tokio::sync::broadcast;

use crate::{
    airplay::dacp::{DacpController, dacp_command_for_alias, is_navigation_alias},
    audio::{AudioEngine, AudioEngineStatus, AudioManager, SelectAudioDeviceRequest},
    mdns::MdnsAdvertiser,
    state::{AppState, PlayerState, RemoteControlState, StateSnapshot, TrackInfo, VolumeState},
};

#[derive(Clone)]
pub struct ApiContext {
    state: AppState,
    audio: AudioManager,
    audio_engine: AudioEngine,
    mdns: MdnsAdvertiser,
    dacp: DacpController,
}

#[derive(Debug, Deserialize)]
pub struct SetVolumeRequest {
    pub db: f64,
}

#[derive(Debug, Deserialize)]
pub struct MediaControlRequest {
    pub command: Option<String>,
    pub volume_db: Option<f64>,
}

#[derive(Debug, Serialize)]
struct CommandResponse {
    accepted: bool,
    command: String,
    message: String,
}

#[derive(Debug, Serialize)]
struct MediaInfoResponse {
    active: bool,
    player_state: PlayerState,
    track: TrackInfo,
    estimated_progress_ms: Option<u64>,
    volume: VolumeState,
    remote_control: RemoteControlState,
    controls: Vec<&'static str>,
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
        dacp: DacpController,
    ) -> Self {
        Self {
            state,
            audio,
            audio_engine,
            mdns,
            dacp,
        }
    }
}

pub fn router(context: ApiContext) -> Router {
    Router::new()
        .route("/api/v1/state", get(get_state))
        .route("/api/v1/media", get(get_media_info))
        .route("/api/v1/media/control", post(media_control))
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

async fn get_media_info(State(context): State<ApiContext>) -> Json<MediaInfoResponse> {
    Json(media_info_from_snapshot(context.state.snapshot()))
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
    context.audio_engine.set_volume_db(request.db);
    Json(context.state.snapshot())
}

async fn media_control(
    State(context): State<ApiContext>,
    Json(request): Json<MediaControlRequest>,
) -> Json<CommandResponse> {
    let mut messages = Vec::new();
    if let Some(volume_db) = request.volume_db {
        context.state.set_volume(volume_db);
        context.audio_engine.set_volume_db(volume_db);
        messages.push(format!("volume set to {volume_db:.1} dB"));
    }

    if let Some(command) = request.command {
        let mut response = apply_remote_command(&context, command).await;
        if !messages.is_empty() {
            response.message = format!("{}; {}", messages.join("; "), response.message);
        }
        return Json(response);
    }

    Json(CommandResponse {
        accepted: !messages.is_empty(),
        command: "media-control".to_string(),
        message: if messages.is_empty() {
            "no media control requested".to_string()
        } else {
            messages.join("; ")
        },
    })
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
    State(context): State<ApiContext>,
    Path(command): Path<String>,
) -> Json<CommandResponse> {
    Json(apply_remote_command(&context, command).await)
}

async fn apply_remote_command(context: &ApiContext, command: String) -> CommandResponse {
    let local_applied = apply_local_remote_command(&context, &command);
    if let Some(dacp_command) = dacp_command_for_alias(&command) {
        match context.dacp.send(dacp_command).await {
            Ok(result) => {
                if is_navigation_alias(&command) {
                    context.audio_engine.set_playback_enabled(false);
                    context.state.clear_track_for_transition();
                }
                return CommandResponse {
                    accepted: true,
                    command,
                    message: format!(
                        "command sent to source via DACP at {} ({})",
                        result.endpoint, result.status_line
                    ),
                };
            }
            Err(err) if local_applied && !is_navigation_alias(&command) => {
                return CommandResponse {
                    accepted: true,
                    command,
                    message: format!("command applied locally; source control failed: {err}"),
                };
            }
            Err(err) => {
                return CommandResponse {
                    accepted: false,
                    command,
                    message: err.to_string(),
                };
            }
        }
    }

    let accepted = local_applied;
    CommandResponse {
        accepted,
        command: command.clone(),
        message: if accepted {
            "command applied locally".to_string()
        } else {
            "unsupported command".to_string()
        },
    }
}

fn media_info_from_snapshot(snapshot: StateSnapshot) -> MediaInfoResponse {
    let estimated_progress_ms = estimated_progress_ms(&snapshot);
    MediaInfoResponse {
        active: snapshot.active,
        player_state: snapshot.player_state,
        track: snapshot.track,
        estimated_progress_ms,
        volume: snapshot.volume,
        remote_control: snapshot.remote_control,
        controls: vec![
            "previous",
            "playpause",
            "play",
            "pause",
            "stop",
            "next",
            "volume",
        ],
    }
}

fn estimated_progress_ms(snapshot: &StateSnapshot) -> Option<u64> {
    let base = snapshot.track.progress_ms?;
    if snapshot.player_state != PlayerState::Playing {
        return Some(base);
    }
    let updated_at = snapshot.track.progress_updated_at?;
    let elapsed_ms = SystemTime::now()
        .duration_since(updated_at)
        .ok()
        .and_then(|duration| u64::try_from(duration.as_millis()).ok())
        .unwrap_or(0);
    let estimated = base.saturating_add(elapsed_ms);
    Some(match snapshot.track.duration_ms {
        Some(duration) => estimated.min(duration),
        None => estimated,
    })
}

fn apply_local_remote_command(context: &ApiContext, command: &str) -> bool {
    match command.to_ascii_lowercase().as_str() {
        "play" | "resume" => {
            enable_local_playback_when_track_ready(context);
            context
                .state
                .set_player_state(crate::state::PlayerState::Playing);
            true
        }
        "pause" => {
            context.audio_engine.set_playback_enabled(false);
            context
                .state
                .set_player_state(crate::state::PlayerState::Paused);
            true
        }
        "stop" => {
            context.audio_engine.set_playback_enabled(false);
            context
                .state
                .set_player_state(crate::state::PlayerState::Stopped);
            true
        }
        "playpause" | "toggle" => match context.state.snapshot().player_state {
            crate::state::PlayerState::Playing => {
                context.audio_engine.set_playback_enabled(false);
                context
                    .state
                    .set_player_state(crate::state::PlayerState::Paused);
                true
            }
            _ => {
                enable_local_playback_when_track_ready(context);
                context
                    .state
                    .set_player_state(crate::state::PlayerState::Playing);
                true
            }
        },
        _ => false,
    }
}

fn enable_local_playback_when_track_ready(context: &ApiContext) -> bool {
    if context.state.is_waiting_for_track_title() {
        context
            .state
            .set_diagnostic("audio_waiting_for_track_title", "true");
        context.audio_engine.clear_output_samples();
        return false;
    }
    context
        .state
        .set_diagnostic("audio_waiting_for_track_title", "false");
    context.audio_engine.set_playback_enabled(true);
    true
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
    use http_body_util::BodyExt;
    use serde_json::Value;
    use tower::ServiceExt;

    use super::*;
    use crate::{
        airplay::dacp::DacpController, audio::AudioManager, config::Config, mdns::MdnsBackend,
    };

    #[tokio::test]
    async fn state_endpoint_returns_ok() {
        let config = Config::default();
        let state = AppState::new(config.clone());
        let dacp = DacpController::disabled(state.clone());
        let app = router(ApiContext::new(
            state,
            AudioManager::new(config.audio.clone()),
            AudioEngine::new(16),
            MdnsAdvertiser::new(MdnsBackend::Off, config.mdns),
            dacp,
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

    #[tokio::test]
    async fn remote_next_without_dacp_session_is_not_accepted() {
        let config = Config::default();
        let state = AppState::new(config.clone());
        let dacp = DacpController::disabled(state.clone());
        let app = router(ApiContext::new(
            state,
            AudioManager::new(config.audio.clone()),
            AudioEngine::new(16),
            MdnsAdvertiser::new(MdnsBackend::Off, config.mdns),
            dacp,
        ));

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/remote/next")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let payload: Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(payload["accepted"], false);
        assert!(
            payload["message"]
                .as_str()
                .unwrap()
                .contains("remote control unavailable")
        );
    }

    #[tokio::test]
    async fn media_endpoint_returns_now_playing_shape() {
        let config = Config::default();
        let state = AppState::new(config.clone());
        state.set_track_metadata(
            Some("Song".to_string()),
            Some("Artist".to_string()),
            Some("Album".to_string()),
        );
        let dacp = DacpController::disabled(state.clone());
        let app = router(ApiContext::new(
            state,
            AudioManager::new(config.audio.clone()),
            AudioEngine::new(16),
            MdnsAdvertiser::new(MdnsBackend::Off, config.mdns),
            dacp,
        ));

        let response = app
            .oneshot(
                Request::builder()
                    .uri("/api/v1/media")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let payload: Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(payload["track"]["title"], "Song");
        assert_eq!(payload["track"]["artist"], "Artist");
        assert!(
            payload["controls"]
                .as_array()
                .unwrap()
                .contains(&Value::from("next"))
        );
    }

    #[tokio::test]
    async fn media_control_sets_volume() {
        let config = Config::default();
        let state = AppState::new(config.clone());
        let dacp = DacpController::disabled(state.clone());
        let app = router(ApiContext::new(
            state,
            AudioManager::new(config.audio.clone()),
            AudioEngine::new(16),
            MdnsAdvertiser::new(MdnsBackend::Off, config.mdns),
            dacp,
        ));

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/media/control")
                    .header("content-type", "application/json")
                    .body(Body::from(r#"{"volume_db":-12.5}"#))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let payload: Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(payload["accepted"], true);
        assert!(payload["message"].as_str().unwrap().contains("-12.5 dB"));
    }
}
