use std::{
    process::{Child, Command},
    sync::Arc,
};

use anyhow::{Context, bail};
use mdns_sd::{ServiceDaemon, ServiceInfo};
use parking_lot::Mutex;

use crate::{
    airplay::txt_records::AirplayService,
    config::{MdnsBackendName, MdnsConfig},
};

#[derive(Clone, Debug)]
pub enum MdnsBackend {
    Builtin,
    Avahi,
    DnsSd,
    External,
    Off,
}

#[derive(Clone)]
pub struct MdnsAdvertiser {
    backend: MdnsBackend,
    config: MdnsConfig,
    builtin_daemons: Arc<Mutex<Vec<ServiceDaemon>>>,
    external_children: Arc<Mutex<Vec<Child>>>,
}

impl MdnsBackend {
    pub fn from_config(config: &MdnsConfig) -> Self {
        match config.backend {
            MdnsBackendName::Builtin => Self::Builtin,
            MdnsBackendName::Avahi => Self::Avahi,
            MdnsBackendName::DnsSd => Self::DnsSd,
            MdnsBackendName::External => Self::External,
            MdnsBackendName::Off => Self::Off,
        }
    }
}

impl MdnsAdvertiser {
    pub fn new(backend: MdnsBackend, config: MdnsConfig) -> Self {
        Self {
            backend,
            config,
            builtin_daemons: Arc::new(Mutex::new(Vec::new())),
            external_children: Arc::new(Mutex::new(Vec::new())),
        }
    }

    pub async fn publish(&self, services: Vec<AirplayService>) -> anyhow::Result<()> {
        match self.backend {
            MdnsBackend::Builtin => self.publish_builtin(services).await,
            MdnsBackend::Avahi => {
                self.publish_external_program("avahi-publish-service", services)
                    .await
            }
            MdnsBackend::DnsSd => self.publish_external_program("dns-sd", services).await,
            MdnsBackend::External => self.publish_configured_external(services).await,
            MdnsBackend::Off => Ok(()),
        }
    }

    async fn publish_builtin(&self, services: Vec<AirplayService>) -> anyhow::Result<()> {
        let daemon = ServiceDaemon::new().context("failed to create built-in mDNS daemon")?;
        for service in services {
            let txt: Vec<(&str, &str)> = service
                .txt
                .iter()
                .filter_map(|entry| entry.split_once('='))
                .collect();
            let info = ServiceInfo::new(
                &service.service_type,
                &service.instance_name,
                &format!("{}.local.", self.config.hostname),
                "",
                service.port,
                &txt[..],
            )
            .with_context(|| {
                format!("failed to build service info for {}", service.service_type)
            })?;
            daemon
                .register(info)
                .with_context(|| format!("failed to register {}", service.service_type))?;
        }
        self.builtin_daemons.lock().push(daemon);
        Ok(())
    }

    async fn publish_configured_external(
        &self,
        services: Vec<AirplayService>,
    ) -> anyhow::Result<()> {
        let Some(command) = self.config.external_command.as_deref() else {
            bail!("mdns.backend=external requires mdns.external_command");
        };
        self.publish_external_program(command, services).await
    }

    async fn publish_external_program(
        &self,
        command: &str,
        services: Vec<AirplayService>,
    ) -> anyhow::Result<()> {
        for service in services {
            let child = Command::new(command)
                .arg(&service.instance_name)
                .arg(&service.service_type)
                .arg(service.port.to_string())
                .args(service.txt.iter())
                .spawn()
                .with_context(|| format!("failed to spawn {command}"))?;
            self.external_children.lock().push(child);
        }
        Ok(())
    }
}
