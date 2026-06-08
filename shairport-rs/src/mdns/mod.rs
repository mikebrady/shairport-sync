use std::{
    process::{Child, Command},
    sync::Arc,
};

use anyhow::{Context, bail};
use mdns_sd::{IfKind, ServiceDaemon, ServiceInfo};
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
    published_services: Arc<Mutex<Vec<AirplayService>>>,
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
            published_services: Arc::new(Mutex::new(Vec::new())),
        }
    }

    pub async fn publish(&self, services: Vec<AirplayService>) -> anyhow::Result<()> {
        // Store for later republish
        *self.published_services.lock() = services.clone();
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

    /// Republish with the same services (e.g. after TXT record changes).
    /// For the builtin backend this re-registers; for external backends it spawns new processes.
    pub async fn republish(&self) -> anyhow::Result<()> {
        let services = self.published_services.lock().clone();
        if services.is_empty() {
            return Ok(());
        }
        self.publish(services).await
    }

    async fn publish_builtin(&self, services: Vec<AirplayService>) -> anyhow::Result<()> {
        let daemon = ServiceDaemon::new().context("failed to create built-in mDNS daemon")?;
        if let Some(interface) = self.config.interface.as_deref() {
            daemon
                .enable_interface(interface)
                .with_context(|| format!("failed to enable mDNS interface {interface}"))?;
        } else {
            daemon
                .enable_interface(IfKind::IPv4)
                .context("failed to enable IPv4 mDNS interfaces")?;
        }
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
            .map(ServiceInfo::enable_addr_auto)
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
        self.stop_external_children();
        for service in services {
            let mut cmd = Command::new(command);
            if command.eq_ignore_ascii_case("dns-sd") || command.ends_with("dns-sd") {
                cmd.arg("-R")
                    .arg(&service.instance_name)
                    .arg(trim_local_domain(&service.service_type))
                    .arg("local")
                    .arg(service.port.to_string())
                    .args(service.txt.iter());
            } else if command.eq_ignore_ascii_case("avahi-publish-service")
                || command.ends_with("avahi-publish-service")
            {
                cmd.arg(&service.instance_name)
                    .arg(trim_local_domain(&service.service_type))
                    .arg(service.port.to_string())
                    .args(service.txt.iter());
            } else {
                cmd.arg(&service.instance_name)
                    .arg(&service.service_type)
                    .arg(service.port.to_string())
                    .args(service.txt.iter());
            }

            let child = cmd
                .spawn()
                .with_context(|| format!("failed to spawn {command}"))?;
            self.external_children.lock().push(child);
        }
        Ok(())
    }

    fn stop_external_children(&self) {
        let mut children = self.external_children.lock();
        for child in children.iter_mut() {
            match child.try_wait() {
                Ok(Some(_)) => {}
                Ok(None) => {
                    let _ = child.kill();
                    let _ = child.wait();
                }
                Err(_) => {
                    let _ = child.kill();
                    let _ = child.wait();
                }
            }
        }
        children.clear();
    }
}

impl Drop for MdnsAdvertiser {
    fn drop(&mut self) {
        if Arc::strong_count(&self.external_children) == 1 {
            self.stop_external_children();
        }
    }
}

fn trim_local_domain(service_type: &str) -> &str {
    service_type
        .strip_suffix(".local.")
        .or_else(|| service_type.strip_suffix(".local"))
        .unwrap_or(service_type)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn trims_local_domain_for_external_publishers() {
        assert_eq!(trim_local_domain("_airplay._tcp.local."), "_airplay._tcp");
        assert_eq!(trim_local_domain("_raop._tcp.local"), "_raop._tcp");
        assert_eq!(trim_local_domain("_raop._tcp"), "_raop._tcp");
    }
}
