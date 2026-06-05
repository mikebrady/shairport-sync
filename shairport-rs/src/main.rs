#![allow(dead_code)]

mod airplay;
mod api;
mod audio;
mod codec;
mod config;
mod decoder;
mod mdns;
mod player;
mod ptp;
mod state;
mod web;

use std::{net::SocketAddr, path::PathBuf};

use anyhow::Context;
use axum::Router;
use clap::Parser;
use tokio::net::TcpListener;
use tower_http::trace::TraceLayer;
use tracing::{info, warn};

use crate::{
    api::ApiContext,
    config::Config,
    mdns::{MdnsAdvertiser, MdnsBackend},
    state::AppState,
};

#[derive(Debug, Parser)]
#[command(version, about)]
struct Args {
    #[arg(short, long, env = "SHAIRPORT_RS_CONFIG")]
    config: Option<PathBuf>,

    #[arg(long, env = "SHAIRPORT_RS_DEBUG")]
    debug: bool,
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let args = Args::parse();
    let env_filter = std::env::var("RUST_LOG").unwrap_or_else(|_| {
        if args.debug {
            "shairport_rs=debug,tower_http=debug".to_string()
        } else {
            "info".to_string()
        }
    });
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::new(env_filter))
        .init();

    if args.debug {
        info!("debug logging enabled");
    }

    let config = Config::load(args.config.as_deref())?;
    let app_state = AppState::new(config.clone());

    let audio_manager = audio::AudioManager::new(config.audio.clone());
    let audio_engine = audio::AudioEngine::new(48_000 * 2 * 4);
    let player = player::SharedPlayer::new();
    app_state.update_audio_devices(audio_manager.list_devices());
    let audio_output = match audio_manager.start_output(audio_engine.clone()) {
        Ok(output) => Some(output),
        Err(err) => {
            warn!(%err, "audio output stream not started");
            app_state.set_diagnostic("audio_output_error", err.to_string());
            None
        }
    };

    let ptp_handle = if config.ptp.enabled {
        Some(ptp::spawn_ptp_service(config.ptp.clone(), app_state.clone()).await?)
    } else {
        None
    };

    let rtsp_handle = if config.airplay.enabled {
        Some(airplay::rtsp::spawn_rtsp_server(config.airplay.clone(), app_state.clone(), player.clone()).await?)
    } else {
        None
    };
    let rtp_handles = if config.airplay.enabled {
        airplay::rtp::spawn_rtp_receivers(
            config.airplay.clone(),
            app_state.clone(),
            audio_engine.clone(),
            player.clone(),
        )
        .await?
    } else {
        Vec::new()
    };

    let buffered_audio_handle = if config.airplay.airplay2_enabled {
        Some(
            airplay::buffered_audio::spawn_buffered_audio_receiver(
                config.airplay.clone(),
                app_state.clone(),
                audio_engine.clone(),
            )
            .await?,
        )
    } else {
        None
    };

    let mdns_backend = MdnsBackend::from_config(&config.mdns);
    let mdns_advertiser = MdnsAdvertiser::new(mdns_backend, config.mdns.clone());
    let services = airplay::txt_records::airplay_services(&config);
    let service_types: Vec<String> = services
        .iter()
        .map(|s| s.service_type.trim_end_matches(".local.").to_string())
        .collect();
    if let Err(err) = mdns_advertiser.publish(services).await {
        warn!(%err, "mDNS publication failed");
        app_state.set_mdns_error(err.to_string());
    } else {
        app_state.set_mdns_running(config.mdns.backend.to_string(), service_types);
    }

    let api_context = ApiContext::new(
        app_state.clone(),
        audio_manager,
        audio_engine,
        mdns_advertiser,
    );
    let router = Router::new()
        .merge(api::router(api_context))
        .merge(web::router())
        .layer(TraceLayer::new_for_http());

    let bind: SocketAddr = config
        .server
        .bind
        .parse()
        .with_context(|| format!("invalid server bind address {}", config.server.bind))?;
    let listener = TcpListener::bind(bind).await?;
    info!(%bind, "shairport-rs listening");

    axum::serve(listener, router)
        .with_graceful_shutdown(shutdown_signal())
        .await?;

    if let Some(handle) = ptp_handle {
        handle.abort();
    }
    if let Some(handle) = rtsp_handle {
        handle.abort();
    }
    for handle in rtp_handles {
        handle.abort();
    }
    if let Some(handle) = buffered_audio_handle {
        handle.abort();
    }
    drop(audio_output);
    Ok(())
}

async fn shutdown_signal() {
    let ctrl_c = async {
        tokio::signal::ctrl_c()
            .await
            .expect("failed to install Ctrl-C handler");
    };

    #[cfg(unix)]
    let terminate = async {
        tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
            .expect("failed to install SIGTERM handler")
            .recv()
            .await;
    };

    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();

    tokio::select! {
        _ = ctrl_c => {},
        _ = terminate => {},
    }
}
