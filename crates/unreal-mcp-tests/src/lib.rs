use anyhow::Context;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::task::JoinHandle;

use unreal_mcp_protocol::{
    decode_msgpack_request, encode_msgpack_response, ActorQuery, AssetOperation,
    BlueprintComponentOperation, BlueprintOperation, BridgeStatus, Command, CommandResult,
    LevelInfo, LevelList, LevelOperation, LightSummary, LightingOperation, MaterialAppliedActor,
    MaterialApplyResult, MaterialOperation, MaterialParameterOperation, ProceduralTextureOperation,
    ResponseEnvelope, RuntimeAnimationOperation, SpawnedActor, Transform, WorldQueryResult,
};

pub struct FakeBridge {
    address: String,
    listener_task: JoinHandle<()>,
}

impl FakeBridge {
    pub fn address(&self) -> &str {
        &self.address
    }
}

impl Drop for FakeBridge {
    fn drop(&mut self) {
        self.listener_task.abort();
    }
}

pub async fn start_fake_bridge() -> anyhow::Result<FakeBridge> {
    let listener = TcpListener::bind("127.0.0.1:0")
        .await
        .context("bind fake bridge")?;
    let address = listener.local_addr()?.to_string();

    let listener_task = tokio::spawn(async move {
        loop {
            let Ok((stream, _)) = listener.accept().await else {
                break;
            };
            tokio::spawn(async move {
                if let Err(error) = handle_connection(stream).await {
                    eprintln!("fake bridge connection failed: {error:#}");
                }
            });
        }
    });

    Ok(FakeBridge {
        address,
        listener_task,
    })
}

async fn handle_connection(mut stream: TcpStream) -> anyhow::Result<()> {
    let mut length_buf = [0_u8; 4];
    stream.read_exact(&mut length_buf).await?;
    let length = u32::from_be_bytes(length_buf) as usize;
    let mut body = vec![0_u8; length];
    stream.read_exact(&mut body).await?;

    let request = decode_msgpack_request(&body)?;
    let results = request
        .commands
        .into_iter()
        .map(|command| match command {
            Command::Ping => CommandResult::Pong {
                bridge_version: "fake-0.1.0".to_string(),
            },
            Command::Status => CommandResult::Status(BridgeStatus {
                connected: true,
                bridge_version: Some("fake-0.1.0".to_string()),
                unreal_version: Some("fake-unreal".to_string()),
            }),
            Command::Capabilities => CommandResult::Capabilities {
                commands: vec![
                    "connection.ping".to_string(),
                    "connection.status".to_string(),
                    "connection.capabilities".to_string(),
                    "level.create".to_string(),
                    "level.open".to_string(),
                    "level.save".to_string(),
                    "level.list".to_string(),
                    "world.bulk_spawn".to_string(),
                    "world.bulk_delete".to_string(),
                    "world.query".to_string(),
                    "world.snapshot".to_string(),
                    "asset.create_folder".to_string(),
                    "material.create".to_string(),
                    "material.create_instance".to_string(),
                    "material.create_procedural_texture".to_string(),
                    "material.set_parameters".to_string(),
                    "material.bulk_apply".to_string(),
                    "world.bulk_set_materials".to_string(),
                    "lighting.set_night_scene".to_string(),
                    "lighting.set_sky".to_string(),
                    "lighting.set_fog".to_string(),
                    "lighting.set_post_process".to_string(),
                    "lighting.bulk_set_lights".to_string(),
                    "lighting.set_time_of_day".to_string(),
                    "blueprint.create_actor".to_string(),
                    "blueprint.add_static_mesh_component".to_string(),
                    "blueprint.add_light_component".to_string(),
                    "blueprint.compile".to_string(),
                    "runtime.create_led_animation".to_string(),
                    "runtime.create_moving_light_animation".to_string(),
                    "runtime.create_material_parameter_animation".to_string(),
                    "runtime.attach_animation_to_actor".to_string(),
                ],
            },
            Command::LevelCreate { path, open, save } => {
                CommandResult::LevelOperation(LevelOperation {
                    path,
                    opened: open,
                    saved: save,
                })
            }
            Command::LevelOpen { path } => CommandResult::LevelOperation(LevelOperation {
                path,
                opened: true,
                saved: false,
            }),
            Command::LevelSave { path } => CommandResult::LevelOperation(LevelOperation {
                path: path.unwrap_or_else(|| "/Game/MCP/Generated/L_Fake".to_string()),
                opened: true,
                saved: true,
            }),
            Command::LevelList => CommandResult::LevelList(LevelList {
                levels: vec![LevelInfo {
                    path: "/Game/MCP/Generated/L_Fake".to_string(),
                    name: "L_Fake".to_string(),
                }],
            }),
            Command::WorldBulkSpawn { actors } => {
                let spawned = actors
                    .into_iter()
                    .map(|actor| SpawnedActor {
                        path: format!("PersistentLevel.{}", actor.name),
                        name: actor.name,
                    })
                    .collect::<Vec<_>>();
                CommandResult::WorldBulkSpawn {
                    count: spawned.len(),
                    spawned,
                }
            }
            Command::WorldBulkDelete { names, tags } => {
                let deleted = if names.is_empty() {
                    tags.into_iter()
                        .map(|tag| format!("deleted_by_tag:{tag}"))
                        .collect::<Vec<_>>()
                } else {
                    names
                };
                CommandResult::WorldBulkDelete {
                    count: deleted.len(),
                    deleted,
                }
            }
            Command::WorldQuery { .. } => CommandResult::WorldQuery(WorldQueryResult {
                actors: vec![ActorQuery {
                    name: "MCP_Test_Cube".to_string(),
                    path: "PersistentLevel.MCP_Test_Cube".to_string(),
                    class_name: "StaticMeshActor".to_string(),
                    transform: Transform {
                        location: [0.0, 0.0, 50.0],
                        rotation: [0.0, 0.0, 0.0],
                        scale: [1.0, 1.0, 1.0],
                    },
                    tags: vec!["mcp.generated".to_string()],
                }],
                total_count: 1,
            }),
            Command::WorldSnapshot { path, .. } => CommandResult::WorldSnapshot {
                path: path.unwrap_or_else(|| "Saved/UnrealMCP/snapshots/world.json".to_string()),
                total_count: 1,
            },
            Command::AssetCreateFolder { path } => CommandResult::AssetOperation(AssetOperation {
                path,
                created: true,
            }),
            Command::MaterialCreate { path, .. } => {
                CommandResult::MaterialOperation(MaterialOperation {
                    path,
                    parent: None,
                    created: true,
                })
            }
            Command::MaterialCreateInstance { path, parent, .. } => {
                CommandResult::MaterialOperation(MaterialOperation {
                    path,
                    parent: Some(parent),
                    created: true,
                })
            }
            Command::MaterialCreateProceduralTexture { spec } => {
                CommandResult::ProceduralTextureOperation(ProceduralTextureOperation {
                    path: spec.path,
                    width: spec.width,
                    height: spec.height,
                    created: true,
                })
            }
            Command::MaterialSetParameters {
                path,
                scalar_parameters,
                vector_parameters,
                texture_parameters,
            } => CommandResult::MaterialParameterOperation(MaterialParameterOperation {
                path,
                scalar_count: scalar_parameters.len(),
                vector_count: vector_parameters.len(),
                texture_count: texture_parameters.len(),
            }),
            Command::MaterialBulkApply { assignments } => {
                let applied = assignments
                    .into_iter()
                    .map(|assignment| MaterialAppliedActor {
                        name: "MCP_Test_Cube".to_string(),
                        path: "PersistentLevel.MCP_Test_Cube".to_string(),
                        material: assignment.material,
                        slot: assignment.slot,
                    })
                    .collect::<Vec<_>>();
                CommandResult::MaterialApply(MaterialApplyResult {
                    count: applied.len(),
                    applied,
                })
            }
            Command::WorldBulkSetMaterials { material, slot, .. } => {
                CommandResult::MaterialApply(MaterialApplyResult {
                    applied: vec![MaterialAppliedActor {
                        name: "MCP_Test_Cube".to_string(),
                        path: "PersistentLevel.MCP_Test_Cube".to_string(),
                        material,
                        slot,
                    }],
                    count: 1,
                })
            }
            Command::LightingSetNightScene { .. } => {
                CommandResult::LightingOperation(LightingOperation {
                    changed: vec![
                        "MCP_MoonLight".to_string(),
                        "MCP_SkyLight".to_string(),
                        "MCP_SkyAtmosphere".to_string(),
                        "MCP_NightFog".to_string(),
                        "MCP_PostProcess".to_string(),
                    ],
                    count: 5,
                })
            }
            Command::LightingSetSky { .. } => CommandResult::LightingOperation(LightingOperation {
                changed: vec!["MCP_SkyLight".to_string(), "MCP_SkyAtmosphere".to_string()],
                count: 2,
            }),
            Command::LightingSetFog { .. } => CommandResult::LightingOperation(LightingOperation {
                changed: vec!["MCP_NightFog".to_string()],
                count: 1,
            }),
            Command::LightingSetPostProcess { .. } => {
                CommandResult::LightingOperation(LightingOperation {
                    changed: vec!["MCP_PostProcess".to_string()],
                    count: 1,
                })
            }
            Command::LightingBulkSetLights { lights } => {
                let lights = lights
                    .into_iter()
                    .map(|light| LightSummary {
                        path: format!("PersistentLevel.{}", light.name),
                        name: light.name,
                        kind: light.kind,
                    })
                    .collect::<Vec<_>>();
                CommandResult::LightingBulkSetLights {
                    count: lights.len(),
                    lights,
                }
            }
            Command::LightingSetTimeOfDay { .. } => {
                CommandResult::LightingOperation(LightingOperation {
                    changed: vec!["MCP_SunLight".to_string()],
                    count: 1,
                })
            }
            Command::BlueprintCreateActor { path, .. } => {
                CommandResult::BlueprintOperation(BlueprintOperation {
                    path,
                    created: true,
                    compiled: false,
                })
            }
            Command::BlueprintAddStaticMeshComponent {
                blueprint,
                component,
            } => CommandResult::BlueprintComponentOperation(BlueprintComponentOperation {
                blueprint,
                components: vec![component.name],
                count: 1,
            }),
            Command::BlueprintAddLightComponent {
                blueprint,
                component,
            } => CommandResult::BlueprintComponentOperation(BlueprintComponentOperation {
                blueprint,
                components: vec![component.name],
                count: 1,
            }),
            Command::BlueprintCompile { path, .. } => {
                CommandResult::BlueprintOperation(BlueprintOperation {
                    path,
                    created: false,
                    compiled: true,
                })
            }
            Command::RuntimeCreateLedAnimation { spec }
            | Command::RuntimeCreateMovingLightAnimation { spec }
            | Command::RuntimeCreateMaterialParameterAnimation { spec } => {
                CommandResult::RuntimeAnimationOperation(RuntimeAnimationOperation {
                    path: Some(spec.path),
                    attached: vec![],
                    count: 0,
                })
            }
            Command::RuntimeAttachAnimationToActor {
                names, blueprint, ..
            } => {
                let mut attached = if names.is_empty() {
                    vec!["MCP_Test_Cube".to_string()]
                } else {
                    names
                };
                if let Some(blueprint) = blueprint {
                    attached.push(blueprint);
                }
                CommandResult::RuntimeAnimationOperation(RuntimeAnimationOperation {
                    path: None,
                    count: attached.len(),
                    attached,
                })
            }
        })
        .collect();

    let response = ResponseEnvelope::success(request.request_id, 1, results);
    let response_body = encode_msgpack_response(&response)?;
    stream
        .write_all(&(response_body.len() as u32).to_be_bytes())
        .await?;
    stream.write_all(&response_body).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::start_fake_bridge;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpStream;
    use tokio::time::{sleep, timeout};
    use unreal_mcp_protocol::{
        decode_msgpack_response, encode_msgpack_request, Command, CommandResult, ErrorMode,
        RequestEnvelope, ResponseMode,
    };

    #[tokio::test]
    async fn dropping_fake_bridge_stops_serving_requests() {
        let fake_bridge = start_fake_bridge().await.expect("fake bridge should start");
        let address = fake_bridge.address().to_string();

        ping_fake_bridge(&address)
            .await
            .expect("fake bridge should serve requests while alive");

        drop(fake_bridge);
        tokio::task::yield_now().await;

        let stopped = timeout(Duration::from_millis(250), async {
            loop {
                let ping = timeout(Duration::from_millis(25), ping_fake_bridge(&address)).await;
                if !matches!(ping, Ok(Ok(()))) {
                    break;
                }
                sleep(Duration::from_millis(10)).await;
            }
        })
        .await;

        assert!(
            stopped.is_ok(),
            "fake bridge should stop serving requests after drop"
        );
    }

    async fn ping_fake_bridge(address: &str) -> anyhow::Result<()> {
        let mut stream = TcpStream::connect(address).await?;
        let request =
            RequestEnvelope::new(42, ResponseMode::Full, ErrorMode::Stop, vec![Command::Ping]);
        let request_body = encode_msgpack_request(&request)?;

        stream
            .write_all(&(request_body.len() as u32).to_be_bytes())
            .await?;
        stream.write_all(&request_body).await?;

        let mut length_buf = [0_u8; 4];
        stream.read_exact(&mut length_buf).await?;
        let length = u32::from_be_bytes(length_buf) as usize;
        let mut response_body = vec![0_u8; length];
        stream.read_exact(&mut response_body).await?;

        let response = decode_msgpack_response(&response_body)?;
        anyhow::ensure!(response.ok, "fake bridge response was not ok");
        anyhow::ensure!(
            matches!(
                response.results.as_slice(),
                [CommandResult::Pong { bridge_version }] if bridge_version == "fake-0.1.0"
            ),
            "fake bridge response did not contain pong"
        );

        Ok(())
    }
}
