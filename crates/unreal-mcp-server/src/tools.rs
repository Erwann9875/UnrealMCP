use serde_json::json;

use anyhow::{bail, ensure};
use serde::Deserialize;
use unreal_mcp_protocol::{
    ActorSpawnSpec, AssetOperation, BridgeStatus, Command, CommandResult, ErrorMode,
    LevelOperation, LightSpec, LightingOperation, MaterialApplyResult, MaterialAssignment,
    MaterialOperation, MaterialParameter, MaterialParameterOperation, ProceduralTextureOperation,
    RequestEnvelope, ResponseEnvelope, ResponseMode, TextureCreateSpec, Transform,
};

use crate::BridgeClient;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ToolResponse {
    pub tool_name: &'static str,
    pub summary: String,
    pub data: serde_json::Value,
}

#[derive(Debug, Clone)]
pub struct ConnectionTools {
    bridge: BridgeClient,
}

impl ConnectionTools {
    pub fn new(bridge: BridgeClient) -> Self {
        Self { bridge }
    }

    pub async fn ping(&self) -> anyhow::Result<ToolResponse> {
        let response = self
            .bridge
            .send(RequestEnvelope::new(
                1,
                ResponseMode::Summary,
                ErrorMode::Stop,
                vec![Command::Ping],
            ))
            .await?;

        ensure_bridge_success("ping", &response)?;

        let bridge_version = match response.results.as_slice() {
            [CommandResult::Pong { bridge_version }] => bridge_version.clone(),
            [] => bail!("unexpected ping response: missing pong result"),
            [result] => bail!("unexpected ping response: expected pong, got {result:?}"),
            results => bail!(
                "unexpected ping response: expected exactly one pong result, got {} results",
                results.len()
            ),
        };

        Ok(ToolResponse {
            tool_name: "connection.ping",
            summary: format!("Unreal bridge responded with version {bridge_version}."),
            data: json!({
                "ok": response.ok,
                "bridge_version": bridge_version,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn status(&self) -> anyhow::Result<ToolResponse> {
        let response = self
            .bridge
            .send(RequestEnvelope::new(
                2,
                ResponseMode::Summary,
                ErrorMode::Stop,
                vec![Command::Status],
            ))
            .await?;

        ensure_bridge_success("status", &response)?;

        let status = match response.results.as_slice() {
            [CommandResult::Status(status)] => status.clone(),
            [] => bail!("unexpected status response: missing status result"),
            [result] => bail!("unexpected status response: expected status, got {result:?}"),
            results => bail!(
                "unexpected status response: expected exactly one status result, got {} results",
                results.len()
            ),
        };

        Ok(status_response(response.elapsed_ms, status))
    }

    pub async fn capabilities(&self) -> anyhow::Result<ToolResponse> {
        let response = self
            .bridge
            .send(RequestEnvelope::new(
                3,
                ResponseMode::Summary,
                ErrorMode::Stop,
                vec![Command::Capabilities],
            ))
            .await?;

        ensure_bridge_success("capabilities", &response)?;

        let commands = match response.results.as_slice() {
            [CommandResult::Capabilities { commands }] => commands.clone(),
            [] => bail!("unexpected capabilities response: missing capabilities result"),
            [result] => bail!(
                "unexpected capabilities response: expected capabilities, got {result:?}"
            ),
            results => bail!(
                "unexpected capabilities response: expected exactly one capabilities result, got {} results",
                results.len()
            ),
        };

        Ok(ToolResponse {
            tool_name: "connection.capabilities",
            summary: format!("Unreal bridge reported {} commands.", commands.len()),
            data: json!({
                "commands": commands,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn level_create(&self, arguments: serde_json::Value) -> anyhow::Result<ToolResponse> {
        let args: LevelCreateArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "level.create",
                Command::LevelCreate {
                    path: args.path,
                    open: args.open,
                    save: args.save,
                },
            )
            .await?;
        let operation = expect_level_operation("level.create", &response)?;
        Ok(level_operation_response(
            "level.create",
            format!("Created level {}.", operation.path),
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn level_open(&self, arguments: serde_json::Value) -> anyhow::Result<ToolResponse> {
        let args: LevelPathArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single("level.open", Command::LevelOpen { path: args.path })
            .await?;
        let operation = expect_level_operation("level.open", &response)?;
        Ok(level_operation_response(
            "level.open",
            format!("Opened level {}.", operation.path),
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn level_save(&self, arguments: serde_json::Value) -> anyhow::Result<ToolResponse> {
        let args: LevelSaveArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single("level.save", Command::LevelSave { path: args.path })
            .await?;
        let operation = expect_level_operation("level.save", &response)?;
        Ok(level_operation_response(
            "level.save",
            format!("Saved level {}.", operation.path),
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn level_list(&self) -> anyhow::Result<ToolResponse> {
        let response = self.send_single("level.list", Command::LevelList).await?;
        let levels = match response.results.as_slice() {
            [CommandResult::LevelList(levels)] => levels.clone(),
            [] => bail!("unexpected level.list response: missing level list result"),
            [result] => bail!("unexpected level.list response: expected level list, got {result:?}"),
            results => bail!(
                "unexpected level.list response: expected exactly one level list result, got {} results",
                results.len()
            ),
        };

        Ok(ToolResponse {
            tool_name: "level.list",
            summary: format!("Unreal reported {} level(s).", levels.levels.len()),
            data: json!({
                "levels": levels.levels,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn world_bulk_spawn(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: WorldBulkSpawnArgs = serde_json::from_value(arguments)?;
        let actors = args
            .actors
            .into_iter()
            .map(ActorSpawnArgs::into_spec)
            .collect();
        let response = self
            .send_single("world.bulk_spawn", Command::WorldBulkSpawn { actors })
            .await?;
        let (spawned, count) = match response.results.as_slice() {
            [CommandResult::WorldBulkSpawn { spawned, count }] => (spawned.clone(), *count),
            [] => bail!("unexpected world.bulk_spawn response: missing spawn result"),
            [result] => bail!("unexpected world.bulk_spawn response: expected spawn result, got {result:?}"),
            results => bail!(
                "unexpected world.bulk_spawn response: expected exactly one spawn result, got {} results",
                results.len()
            ),
        };

        Ok(ToolResponse {
            tool_name: "world.bulk_spawn",
            summary: format!("Spawned {count} actor(s)."),
            data: json!({
                "count": count,
                "spawned": spawned,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn world_bulk_delete(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: WorldBulkDeleteArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "world.bulk_delete",
                Command::WorldBulkDelete {
                    names: args.names,
                    tags: args.tags,
                },
            )
            .await?;
        let (deleted, count) = match response.results.as_slice() {
            [CommandResult::WorldBulkDelete { deleted, count }] => (deleted.clone(), *count),
            [] => bail!("unexpected world.bulk_delete response: missing delete result"),
            [result] => bail!("unexpected world.bulk_delete response: expected delete result, got {result:?}"),
            results => bail!(
                "unexpected world.bulk_delete response: expected exactly one delete result, got {} results",
                results.len()
            ),
        };

        Ok(ToolResponse {
            tool_name: "world.bulk_delete",
            summary: format!("Deleted {count} actor(s)."),
            data: json!({
                "count": count,
                "deleted": deleted,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn world_query(&self, arguments: serde_json::Value) -> anyhow::Result<ToolResponse> {
        let args: WorldQueryArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "world.query",
                Command::WorldQuery {
                    names: args.names,
                    tags: args.tags,
                    include_generated: args.include_generated,
                    limit: args.limit,
                },
            )
            .await?;
        let query = match response.results.as_slice() {
            [CommandResult::WorldQuery(query)] => query.clone(),
            [] => bail!("unexpected world.query response: missing query result"),
            [result] => bail!("unexpected world.query response: expected query result, got {result:?}"),
            results => bail!(
                "unexpected world.query response: expected exactly one query result, got {} results",
                results.len()
            ),
        };

        Ok(ToolResponse {
            tool_name: "world.query",
            summary: format!("World query returned {} actor(s).", query.total_count),
            data: json!({
                "actors": query.actors,
                "total_count": query.total_count,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn world_snapshot(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: WorldSnapshotArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "world.snapshot",
                Command::WorldSnapshot {
                    path: args.path,
                    tags: args.tags,
                },
            )
            .await?;
        let (path, total_count) = match response.results.as_slice() {
            [CommandResult::WorldSnapshot { path, total_count }] => (path.clone(), *total_count),
            [] => bail!("unexpected world.snapshot response: missing snapshot result"),
            [result] => bail!("unexpected world.snapshot response: expected snapshot result, got {result:?}"),
            results => bail!(
                "unexpected world.snapshot response: expected exactly one snapshot result, got {} results",
                results.len()
            ),
        };

        Ok(ToolResponse {
            tool_name: "world.snapshot",
            summary: format!("Wrote world snapshot for {total_count} actor(s)."),
            data: json!({
                "path": path,
                "total_count": total_count,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn asset_create_folder(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: AssetCreateFolderArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "asset.create_folder",
                Command::AssetCreateFolder {
                    path: args.path.clone(),
                },
            )
            .await?;
        let operation = expect_asset_operation("asset.create_folder", &response)?;
        Ok(ToolResponse {
            tool_name: "asset.create_folder",
            summary: format!("Created folder {}.", operation.path),
            data: json!({
                "path": operation.path,
                "created": operation.created,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn material_create(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: MaterialCreateArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "material.create",
                Command::MaterialCreate {
                    path: args.path,
                    base_color: args.base_color,
                    metallic: args.metallic,
                    roughness: args.roughness,
                    specular: args.specular,
                    emissive_color: args.emissive_color,
                },
            )
            .await?;
        let operation = expect_material_operation("material.create", &response)?;
        Ok(material_operation_response(
            "material.create",
            format!("Created material {}.", operation.path),
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn material_create_instance(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: MaterialInstanceArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "material.create_instance",
                Command::MaterialCreateInstance {
                    path: args.path,
                    parent: args.parent,
                    scalar_parameters: args.scalar_parameters.into_iter().map(Into::into).collect(),
                    vector_parameters: args.vector_parameters.into_iter().map(Into::into).collect(),
                    texture_parameters: args
                        .texture_parameters
                        .into_iter()
                        .map(Into::into)
                        .collect(),
                },
            )
            .await?;
        let operation = expect_material_operation("material.create_instance", &response)?;
        Ok(material_operation_response(
            "material.create_instance",
            format!("Created material instance {}.", operation.path),
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn material_create_procedural_texture(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: TextureCreateArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "material.create_procedural_texture",
                Command::MaterialCreateProceduralTexture {
                    spec: TextureCreateSpec {
                        path: args.path,
                        pattern: args.pattern,
                        width: args.width,
                        height: args.height,
                        color_a: args.color_a,
                        color_b: args.color_b,
                        checker_size: args.checker_size,
                    },
                },
            )
            .await?;
        let texture = expect_texture_operation("material.create_procedural_texture", &response)?;
        Ok(ToolResponse {
            tool_name: "material.create_procedural_texture",
            summary: format!("Created procedural texture {}.", texture.path),
            data: json!({
                "path": texture.path,
                "width": texture.width,
                "height": texture.height,
                "created": texture.created,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn material_set_parameters(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: MaterialSetParametersArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "material.set_parameters",
                Command::MaterialSetParameters {
                    path: args.path,
                    scalar_parameters: args.scalar_parameters.into_iter().map(Into::into).collect(),
                    vector_parameters: args.vector_parameters.into_iter().map(Into::into).collect(),
                    texture_parameters: args
                        .texture_parameters
                        .into_iter()
                        .map(Into::into)
                        .collect(),
                },
            )
            .await?;
        let operation = expect_parameter_operation("material.set_parameters", &response)?;
        Ok(parameter_operation_response(
            "material.set_parameters",
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn material_bulk_apply(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: MaterialBulkApplyArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "material.bulk_apply",
                Command::MaterialBulkApply {
                    assignments: args
                        .assignments
                        .into_iter()
                        .map(MaterialAssignmentArgs::into_assignment)
                        .collect(),
                },
            )
            .await?;
        let result = expect_material_apply("material.bulk_apply", &response)?;
        Ok(material_apply_response(
            "material.bulk_apply",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn world_bulk_set_materials(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: WorldBulkSetMaterialsArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "world.bulk_set_materials",
                Command::WorldBulkSetMaterials {
                    names: args.names,
                    tags: args.tags,
                    material: args.material,
                    slot: args.slot,
                },
            )
            .await?;
        let result = expect_material_apply("world.bulk_set_materials", &response)?;
        Ok(material_apply_response(
            "world.bulk_set_materials",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn lighting_set_night_scene(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: LightingNightSceneArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "lighting.set_night_scene",
                Command::LightingSetNightScene {
                    moon_rotation: args.moon_rotation,
                    moon_intensity: args.moon_intensity,
                    moon_color: args.moon_color,
                    sky_intensity: args.sky_intensity,
                    fog_density: args.fog_density,
                    exposure_compensation: args.exposure_compensation,
                },
            )
            .await?;
        let operation = expect_lighting_operation("lighting.set_night_scene", &response)?;
        Ok(lighting_operation_response(
            "lighting.set_night_scene",
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn lighting_set_sky(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: LightingSkyArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "lighting.set_sky",
                Command::LightingSetSky {
                    sky_intensity: args.sky_intensity,
                    lower_hemisphere_color: args.lower_hemisphere_color,
                },
            )
            .await?;
        let operation = expect_lighting_operation("lighting.set_sky", &response)?;
        Ok(lighting_operation_response(
            "lighting.set_sky",
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn lighting_set_fog(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: LightingFogArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "lighting.set_fog",
                Command::LightingSetFog {
                    density: args.density,
                    height_falloff: args.height_falloff,
                    color: args.color,
                    start_distance: args.start_distance,
                },
            )
            .await?;
        let operation = expect_lighting_operation("lighting.set_fog", &response)?;
        Ok(lighting_operation_response(
            "lighting.set_fog",
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn lighting_set_post_process(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: LightingPostProcessArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "lighting.set_post_process",
                Command::LightingSetPostProcess {
                    exposure_compensation: args.exposure_compensation,
                    min_brightness: args.min_brightness,
                    max_brightness: args.max_brightness,
                    bloom_intensity: args.bloom_intensity,
                },
            )
            .await?;
        let operation = expect_lighting_operation("lighting.set_post_process", &response)?;
        Ok(lighting_operation_response(
            "lighting.set_post_process",
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn lighting_bulk_set_lights(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: LightingBulkSetLightsArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "lighting.bulk_set_lights",
                Command::LightingBulkSetLights {
                    lights: args.lights.into_iter().map(LightArgs::into_spec).collect(),
                },
            )
            .await?;
        let (lights, count) = match response.results.as_slice() {
            [CommandResult::LightingBulkSetLights { lights, count }] => (lights.clone(), *count),
            [] => bail!("unexpected lighting.bulk_set_lights response: missing lights result"),
            [result] => bail!(
                "unexpected lighting.bulk_set_lights response: expected lights result, got {result:?}"
            ),
            results => bail!(
                "unexpected lighting.bulk_set_lights response: expected exactly one lights result, got {} results",
                results.len()
            ),
        };

        Ok(ToolResponse {
            tool_name: "lighting.bulk_set_lights",
            summary: format!("Configured {count} light(s)."),
            data: json!({
                "count": count,
                "lights": lights,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn lighting_set_time_of_day(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: LightingTimeOfDayArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "lighting.set_time_of_day",
                Command::LightingSetTimeOfDay {
                    sun_rotation: args.sun_rotation,
                    sun_intensity: args.sun_intensity,
                    sun_color: args.sun_color,
                },
            )
            .await?;
        let operation = expect_lighting_operation("lighting.set_time_of_day", &response)?;
        Ok(lighting_operation_response(
            "lighting.set_time_of_day",
            response.elapsed_ms,
            operation,
        ))
    }

    async fn send_single(
        &self,
        command_name: &str,
        command: Command,
    ) -> anyhow::Result<ResponseEnvelope> {
        let response = self
            .bridge
            .send(RequestEnvelope::new(
                100,
                ResponseMode::Summary,
                ErrorMode::Stop,
                vec![command],
            ))
            .await?;
        ensure_bridge_success(command_name, &response)?;
        Ok(response)
    }
}

fn ensure_bridge_success(command_name: &str, response: &ResponseEnvelope) -> anyhow::Result<()> {
    ensure!(response.ok, "bridge {command_name} response was not ok");
    ensure!(
        response.errors.is_empty(),
        "bridge {command_name} response included errors: {:?}",
        response.errors
    );
    Ok(())
}

fn status_response(elapsed_ms: u32, status: BridgeStatus) -> ToolResponse {
    let state = if status.connected {
        "connected"
    } else {
        "disconnected"
    };

    ToolResponse {
        tool_name: "connection.status",
        summary: format!("Unreal bridge is {state}."),
        data: json!({
            "connected": status.connected,
            "bridge_version": status.bridge_version,
            "unreal_version": status.unreal_version,
            "elapsed_ms": elapsed_ms
        }),
    }
}

fn expect_level_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<LevelOperation> {
    match response.results.as_slice() {
        [CommandResult::LevelOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing level operation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected level operation, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one level operation result, got {} results",
            results.len()
        ),
    }
}

fn level_operation_response(
    tool_name: &'static str,
    summary: String,
    elapsed_ms: u32,
    operation: LevelOperation,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary,
        data: json!({
            "path": operation.path,
            "opened": operation.opened,
            "saved": operation.saved,
            "elapsed_ms": elapsed_ms
        }),
    }
}

fn expect_asset_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<AssetOperation> {
    match response.results.as_slice() {
        [CommandResult::AssetOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing asset operation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected asset operation, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one asset operation result, got {} results",
            results.len()
        ),
    }
}

fn expect_material_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<MaterialOperation> {
    match response.results.as_slice() {
        [CommandResult::MaterialOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing material operation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected material operation, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one material operation result, got {} results",
            results.len()
        ),
    }
}

fn expect_texture_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<ProceduralTextureOperation> {
    match response.results.as_slice() {
        [CommandResult::ProceduralTextureOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing texture operation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected texture operation, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one texture operation result, got {} results",
            results.len()
        ),
    }
}

fn expect_parameter_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<MaterialParameterOperation> {
    match response.results.as_slice() {
        [CommandResult::MaterialParameterOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing parameter operation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected parameter operation, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one parameter operation result, got {} results",
            results.len()
        ),
    }
}

fn expect_material_apply(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<MaterialApplyResult> {
    match response.results.as_slice() {
        [CommandResult::MaterialApply(result)] => Ok(result.clone()),
        [] => bail!("unexpected {command_name} response: missing material apply result"),
        [result] => bail!(
            "unexpected {command_name} response: expected material apply result, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one material apply result, got {} results",
            results.len()
        ),
    }
}

fn expect_lighting_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<LightingOperation> {
    match response.results.as_slice() {
        [CommandResult::LightingOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing lighting operation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected lighting operation, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one lighting operation result, got {} results",
            results.len()
        ),
    }
}

fn material_operation_response(
    tool_name: &'static str,
    summary: String,
    elapsed_ms: u32,
    operation: MaterialOperation,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary,
        data: json!({
            "path": operation.path,
            "parent": operation.parent,
            "created": operation.created,
            "elapsed_ms": elapsed_ms
        }),
    }
}

fn parameter_operation_response(
    tool_name: &'static str,
    elapsed_ms: u32,
    operation: MaterialParameterOperation,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary: format!("Updated material parameters for {}.", operation.path),
        data: json!({
            "path": operation.path,
            "scalar_count": operation.scalar_count,
            "vector_count": operation.vector_count,
            "texture_count": operation.texture_count,
            "elapsed_ms": elapsed_ms
        }),
    }
}

fn material_apply_response(
    tool_name: &'static str,
    elapsed_ms: u32,
    result: MaterialApplyResult,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary: format!("Applied material to {} actor(s).", result.count),
        data: json!({
            "count": result.count,
            "applied": result.applied,
            "elapsed_ms": elapsed_ms
        }),
    }
}

fn lighting_operation_response(
    tool_name: &'static str,
    elapsed_ms: u32,
    operation: LightingOperation,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary: format!("Updated {} lighting actor(s).", operation.count),
        data: json!({
            "count": operation.count,
            "changed": operation.changed,
            "elapsed_ms": elapsed_ms
        }),
    }
}

#[derive(Debug, Deserialize)]
struct LevelCreateArgs {
    path: String,
    #[serde(default = "default_true")]
    open: bool,
    #[serde(default)]
    save: bool,
}

#[derive(Debug, Deserialize)]
struct LevelPathArgs {
    path: String,
}

#[derive(Debug, Deserialize)]
struct LevelSaveArgs {
    path: Option<String>,
}

#[derive(Debug, Deserialize)]
struct WorldBulkSpawnArgs {
    actors: Vec<ActorSpawnArgs>,
}

#[derive(Debug, Deserialize)]
struct ActorSpawnArgs {
    name: String,
    mesh: String,
    #[serde(default)]
    location: [f64; 3],
    #[serde(default)]
    rotation: [f64; 3],
    #[serde(default = "default_scale")]
    scale: [f64; 3],
    scene: Option<String>,
    group: Option<String>,
}

impl ActorSpawnArgs {
    fn into_spec(self) -> ActorSpawnSpec {
        ActorSpawnSpec {
            name: self.name,
            mesh: self.mesh,
            transform: Transform {
                location: self.location,
                rotation: self.rotation,
                scale: self.scale,
            },
            scene: self.scene,
            group: self.group,
        }
    }
}

#[derive(Debug, Default, Deserialize)]
struct WorldBulkDeleteArgs {
    #[serde(default)]
    names: Vec<String>,
    #[serde(default)]
    tags: Vec<String>,
}

#[derive(Debug, Default, Deserialize)]
struct WorldQueryArgs {
    #[serde(default)]
    names: Vec<String>,
    #[serde(default)]
    tags: Vec<String>,
    #[serde(default)]
    include_generated: bool,
    limit: Option<u32>,
}

#[derive(Debug, Default, Deserialize)]
struct WorldSnapshotArgs {
    path: Option<String>,
    #[serde(default)]
    tags: Vec<String>,
}

#[derive(Debug, Deserialize)]
struct AssetCreateFolderArgs {
    path: String,
}

#[derive(Debug, Deserialize)]
struct MaterialCreateArgs {
    path: String,
    #[serde(default = "default_base_color")]
    base_color: [f64; 4],
    #[serde(default)]
    metallic: f64,
    #[serde(default = "default_roughness")]
    roughness: f64,
    #[serde(default = "default_specular")]
    specular: f64,
    #[serde(default = "default_emissive_color")]
    emissive_color: [f64; 4],
}

#[derive(Debug, Default, Deserialize)]
struct MaterialInstanceArgs {
    path: String,
    parent: String,
    #[serde(default)]
    scalar_parameters: Vec<ScalarParameterArg>,
    #[serde(default)]
    vector_parameters: Vec<VectorParameterArg>,
    #[serde(default)]
    texture_parameters: Vec<TextureParameterArg>,
}

#[derive(Debug, Deserialize)]
struct TextureCreateArgs {
    path: String,
    #[serde(default = "default_pattern")]
    pattern: String,
    #[serde(default = "default_texture_size")]
    width: u32,
    #[serde(default = "default_texture_size")]
    height: u32,
    #[serde(default = "default_texture_color_a")]
    color_a: [f64; 4],
    #[serde(default = "default_texture_color_b")]
    color_b: [f64; 4],
    #[serde(default = "default_checker_size")]
    checker_size: u32,
}

#[derive(Debug, Default, Deserialize)]
struct MaterialSetParametersArgs {
    path: String,
    #[serde(default)]
    scalar_parameters: Vec<ScalarParameterArg>,
    #[serde(default)]
    vector_parameters: Vec<VectorParameterArg>,
    #[serde(default)]
    texture_parameters: Vec<TextureParameterArg>,
}

#[derive(Debug, Default, Deserialize)]
struct MaterialBulkApplyArgs {
    assignments: Vec<MaterialAssignmentArgs>,
}

#[derive(Debug, Deserialize)]
struct MaterialAssignmentArgs {
    material: String,
    #[serde(default)]
    names: Vec<String>,
    #[serde(default)]
    tags: Vec<String>,
    #[serde(default)]
    slot: u32,
}

impl MaterialAssignmentArgs {
    fn into_assignment(self) -> MaterialAssignment {
        MaterialAssignment {
            material: self.material,
            names: self.names,
            tags: self.tags,
            slot: self.slot,
        }
    }
}

#[derive(Debug, Default, Deserialize)]
struct WorldBulkSetMaterialsArgs {
    #[serde(default)]
    names: Vec<String>,
    #[serde(default)]
    tags: Vec<String>,
    material: String,
    #[serde(default)]
    slot: u32,
}

#[derive(Debug, Deserialize)]
struct LightingNightSceneArgs {
    #[serde(default = "default_moon_rotation")]
    moon_rotation: [f64; 3],
    #[serde(default = "default_moon_intensity")]
    moon_intensity: f64,
    #[serde(default = "default_moon_color")]
    moon_color: [f64; 4],
    #[serde(default = "default_sky_intensity")]
    sky_intensity: f64,
    #[serde(default = "default_fog_density")]
    fog_density: f64,
    #[serde(default = "default_exposure_compensation")]
    exposure_compensation: f64,
}

#[derive(Debug, Deserialize)]
struct LightingSkyArgs {
    #[serde(default = "default_sky_intensity")]
    sky_intensity: f64,
    #[serde(default = "default_lower_hemisphere_color")]
    lower_hemisphere_color: [f64; 4],
}

#[derive(Debug, Deserialize)]
struct LightingFogArgs {
    #[serde(default = "default_fog_density")]
    density: f64,
    #[serde(default = "default_fog_height_falloff")]
    height_falloff: f64,
    #[serde(default = "default_fog_color")]
    color: [f64; 4],
    #[serde(default)]
    start_distance: f64,
}

#[derive(Debug, Deserialize)]
struct LightingPostProcessArgs {
    #[serde(default = "default_exposure_compensation")]
    exposure_compensation: f64,
    #[serde(default = "default_min_brightness")]
    min_brightness: f64,
    #[serde(default = "default_max_brightness")]
    max_brightness: f64,
    #[serde(default = "default_bloom_intensity")]
    bloom_intensity: f64,
}

#[derive(Debug, Deserialize)]
struct LightingBulkSetLightsArgs {
    lights: Vec<LightArgs>,
}

#[derive(Debug, Deserialize)]
struct LightArgs {
    name: String,
    #[serde(default = "default_light_kind")]
    kind: String,
    #[serde(default)]
    location: [f64; 3],
    #[serde(default)]
    rotation: [f64; 3],
    #[serde(default = "default_scale")]
    scale: [f64; 3],
    #[serde(default = "default_light_color")]
    color: [f64; 4],
    #[serde(default = "default_light_intensity")]
    intensity: f64,
    #[serde(default = "default_attenuation_radius")]
    attenuation_radius: f64,
    #[serde(default = "default_source_radius")]
    source_radius: f64,
    #[serde(default = "default_source_width")]
    source_width: f64,
    #[serde(default = "default_source_height")]
    source_height: f64,
    #[serde(default)]
    tags: Vec<String>,
}

impl LightArgs {
    fn into_spec(self) -> LightSpec {
        LightSpec {
            name: self.name,
            kind: self.kind,
            transform: Transform {
                location: self.location,
                rotation: self.rotation,
                scale: self.scale,
            },
            color: self.color,
            intensity: self.intensity,
            attenuation_radius: self.attenuation_radius,
            source_radius: self.source_radius,
            source_width: self.source_width,
            source_height: self.source_height,
            tags: self.tags,
        }
    }
}

#[derive(Debug, Deserialize)]
struct LightingTimeOfDayArgs {
    #[serde(default = "default_sun_rotation")]
    sun_rotation: [f64; 3],
    #[serde(default = "default_sun_intensity")]
    sun_intensity: f64,
    #[serde(default = "default_sun_color")]
    sun_color: [f64; 4],
}

#[derive(Debug, Deserialize)]
struct ScalarParameterArg {
    name: String,
    value: f64,
}

impl From<ScalarParameterArg> for MaterialParameter {
    fn from(value: ScalarParameterArg) -> Self {
        MaterialParameter::scalar(value.name, value.value)
    }
}

#[derive(Debug, Deserialize)]
struct VectorParameterArg {
    name: String,
    value: [f64; 4],
}

impl From<VectorParameterArg> for MaterialParameter {
    fn from(value: VectorParameterArg) -> Self {
        MaterialParameter::vector(value.name, value.value)
    }
}

#[derive(Debug, Deserialize)]
struct TextureParameterArg {
    name: String,
    value: String,
}

impl From<TextureParameterArg> for MaterialParameter {
    fn from(value: TextureParameterArg) -> Self {
        MaterialParameter::texture(value.name, value.value)
    }
}

fn default_true() -> bool {
    true
}

fn default_scale() -> [f64; 3] {
    [1.0, 1.0, 1.0]
}

fn default_base_color() -> [f64; 4] {
    [0.8, 0.8, 0.8, 1.0]
}

fn default_roughness() -> f64 {
    0.5
}

fn default_specular() -> f64 {
    0.5
}

fn default_emissive_color() -> [f64; 4] {
    [0.0, 0.0, 0.0, 1.0]
}

fn default_pattern() -> String {
    "solid".to_string()
}

fn default_texture_size() -> u32 {
    64
}

fn default_texture_color_a() -> [f64; 4] {
    [1.0, 1.0, 1.0, 1.0]
}

fn default_texture_color_b() -> [f64; 4] {
    [0.0, 0.0, 0.0, 1.0]
}

fn default_checker_size() -> u32 {
    8
}

fn default_moon_rotation() -> [f64; 3] {
    [-35.0, -25.0, 0.0]
}

fn default_moon_intensity() -> f64 {
    0.12
}

fn default_moon_color() -> [f64; 4] {
    [0.55, 0.65, 1.0, 1.0]
}

fn default_sky_intensity() -> f64 {
    0.05
}

fn default_fog_density() -> f64 {
    0.01
}

fn default_exposure_compensation() -> f64 {
    -0.5
}

fn default_lower_hemisphere_color() -> [f64; 4] {
    [0.01, 0.012, 0.018, 1.0]
}

fn default_fog_height_falloff() -> f64 {
    0.2
}

fn default_fog_color() -> [f64; 4] {
    [0.08, 0.1, 0.16, 1.0]
}

fn default_min_brightness() -> f64 {
    0.2
}

fn default_max_brightness() -> f64 {
    1.0
}

fn default_bloom_intensity() -> f64 {
    0.6
}

fn default_light_kind() -> String {
    "point".to_string()
}

fn default_light_color() -> [f64; 4] {
    [1.0, 0.82, 0.55, 1.0]
}

fn default_light_intensity() -> f64 {
    5000.0
}

fn default_attenuation_radius() -> f64 {
    1000.0
}

fn default_source_radius() -> f64 {
    24.0
}

fn default_source_width() -> f64 {
    64.0
}

fn default_source_height() -> f64 {
    32.0
}

fn default_sun_rotation() -> [f64; 3] {
    [-10.0, 110.0, 0.0]
}

fn default_sun_intensity() -> f64 {
    1.0
}

fn default_sun_color() -> [f64; 4] {
    [1.0, 0.93, 0.82, 1.0]
}
