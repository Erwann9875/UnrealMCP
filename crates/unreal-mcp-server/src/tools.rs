use serde_json::json;

use anyhow::{bail, ensure};
use serde::Deserialize;
use unreal_mcp_protocol::{
    ActorSpawnSpec, AssetImportItem, AssetImportResult, AssetImportSpec, AssetOperation,
    AssetValidateSpec, AssetValidationResult, BlueprintComponentOperation, BlueprintOperation,
    BridgeStatus, CityBlockSpec, Command, CommandResult, DistrictSpec, ErrorMode,
    GameCheckpointSpec, GameCollectiblesSpec, GameInteractionSpec, GameObjectiveFlowSpec,
    GameObjectiveStepSpec, GamePlayerSpec, GameplayOperationResult, GeneratedBuildingSpec,
    GeneratedMeshOperation, GeneratedSignSpec, GridPlacementSpec, LandscapeCreateSpec,
    LandscapeHeightPatch, LandscapeLayerPaint, LandscapeOperation, LevelOperation,
    LightComponentSpec, LightSpec, LightingOperation, MaterialApplyResult, MaterialAssignment,
    MaterialOperation, MaterialParameter, MaterialParameterOperation, PlacementSnapResult,
    PlacementSnapSpec, ProceduralTextureOperation, RequestEnvelope, ResponseEnvelope, ResponseMode,
    RoadNetworkSpec, RuntimeAnimationOperation, RuntimeAnimationSpec, SceneAssemblyResult,
    StaticMeshCollisionSpec, StaticMeshComponentSpec, StaticMeshOperationResult, TextureCreateSpec,
    Transform,
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

    pub async fn asset_import_texture(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: AssetImportArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "asset.import_texture",
                Command::AssetImportTexture {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_asset_import("asset.import_texture", &response)?;
        Ok(asset_import_response(
            "asset.import_texture",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn asset_import_static_mesh(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: AssetImportArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "asset.import_static_mesh",
                Command::AssetImportStaticMesh {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_asset_import("asset.import_static_mesh", &response)?;
        Ok(asset_import_response(
            "asset.import_static_mesh",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn asset_bulk_import(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: AssetBulkImportArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "asset.bulk_import",
                Command::AssetBulkImport {
                    items: args
                        .items
                        .into_iter()
                        .map(AssetImportItemArgs::into_item)
                        .collect(),
                },
            )
            .await?;
        let result = expect_asset_import("asset.bulk_import", &response)?;
        Ok(asset_import_response(
            "asset.bulk_import",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn asset_validate(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: AssetValidateArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "asset.validate",
                Command::AssetValidate {
                    spec: AssetValidateSpec { paths: args.paths },
                },
            )
            .await?;
        let result = expect_asset_validation("asset.validate", &response)?;
        Ok(ToolResponse {
            tool_name: "asset.validate",
            summary: format!("Validated {} asset(s).", result.count),
            data: json!({
                "count": result.count,
                "assets": result.assets,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn mesh_create_building(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: GeneratedBuildingArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "mesh.create_building",
                Command::MeshCreateBuilding {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let operation = expect_generated_mesh("mesh.create_building", &response)?;
        Ok(generated_mesh_response(
            "mesh.create_building",
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn mesh_create_sign(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: GeneratedSignArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "mesh.create_sign",
                Command::MeshCreateSign {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let operation = expect_generated_mesh("mesh.create_sign", &response)?;
        Ok(generated_mesh_response(
            "mesh.create_sign",
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn static_mesh_set_collision(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: StaticMeshCollisionArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "static_mesh.set_collision",
                Command::StaticMeshSetCollision {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_static_mesh_operation("static_mesh.set_collision", &response)?;
        Ok(ToolResponse {
            tool_name: "static_mesh.set_collision",
            summary: format!(
                "Updated collision for {} static mesh asset(s).",
                result.count
            ),
            data: json!({
                "count": result.count,
                "meshes": result.meshes,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn road_create_network(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: RoadNetworkArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "road.create_network",
                Command::RoadCreateNetwork {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_scene_assembly("road.create_network", &response)?;
        Ok(scene_assembly_response(
            "road.create_network",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn scene_bulk_place_on_grid(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: GridPlacementArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "scene.bulk_place_on_grid",
                Command::SceneBulkPlaceOnGrid {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_scene_assembly("scene.bulk_place_on_grid", &response)?;
        Ok(scene_assembly_response(
            "scene.bulk_place_on_grid",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn scene_create_city_block(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: CityBlockArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "scene.create_city_block",
                Command::SceneCreateCityBlock {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_scene_assembly("scene.create_city_block", &response)?;
        Ok(scene_assembly_response(
            "scene.create_city_block",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn scene_create_district(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: DistrictArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "scene.create_district",
                Command::SceneCreateDistrict {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_scene_assembly("scene.create_district", &response)?;
        Ok(scene_assembly_response(
            "scene.create_district",
            response.elapsed_ms,
            result,
        ))
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

    pub async fn landscape_create(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: LandscapeCreateArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "landscape.create",
                Command::LandscapeCreate {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let operation = expect_landscape_operation("landscape.create", &response)?;
        Ok(landscape_operation_response(
            "landscape.create",
            format!("Created landscape {}.", operation.name),
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn landscape_set_heightfield(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: LandscapeHeightfieldArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "landscape.set_heightfield",
                Command::LandscapeSetHeightfield {
                    patch: args.into_patch(),
                },
            )
            .await?;
        let operation = expect_landscape_operation("landscape.set_heightfield", &response)?;
        Ok(landscape_operation_response(
            "landscape.set_heightfield",
            format!("Updated landscape heightfield for {}.", operation.name),
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn landscape_paint_layers(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: LandscapePaintLayersArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "landscape.paint_layers",
                Command::LandscapePaintLayers {
                    paint: args.into_paint(),
                },
            )
            .await?;
        let operation = expect_landscape_operation("landscape.paint_layers", &response)?;
        Ok(landscape_operation_response(
            "landscape.paint_layers",
            format!("Painted landscape {}.", operation.name),
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn placement_bulk_snap_to_ground(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: PlacementBulkSnapArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "placement.bulk_snap_to_ground",
                Command::PlacementBulkSnapToGround {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let snapped = expect_placement_snap("placement.bulk_snap_to_ground", &response)?;
        Ok(ToolResponse {
            tool_name: "placement.bulk_snap_to_ground",
            summary: format!("Snapped {} actor(s) to ground.", snapped.count),
            data: json!({
                "count": snapped.count,
                "actors": snapped.actors,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn blueprint_create_actor(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: BlueprintCreateActorArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "blueprint.create_actor",
                Command::BlueprintCreateActor {
                    path: args.path,
                    parent_class: args.parent_class,
                },
            )
            .await?;
        let operation = expect_blueprint_operation("blueprint.create_actor", &response)?;
        Ok(blueprint_operation_response(
            "blueprint.create_actor",
            format!("Created Blueprint {}.", operation.path),
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn blueprint_add_static_mesh_component(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: StaticMeshComponentArgs = serde_json::from_value(arguments)?;
        let blueprint = args.blueprint.clone();
        let response = self
            .send_single(
                "blueprint.add_static_mesh_component",
                Command::BlueprintAddStaticMeshComponent {
                    blueprint,
                    component: args.into_spec(),
                },
            )
            .await?;
        let operation =
            expect_blueprint_component_operation("blueprint.add_static_mesh_component", &response)?;
        Ok(blueprint_component_response(
            "blueprint.add_static_mesh_component",
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn blueprint_add_light_component(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: LightComponentArgs = serde_json::from_value(arguments)?;
        let blueprint = args.blueprint.clone();
        let response = self
            .send_single(
                "blueprint.add_light_component",
                Command::BlueprintAddLightComponent {
                    blueprint,
                    component: args.into_spec(),
                },
            )
            .await?;
        let operation =
            expect_blueprint_component_operation("blueprint.add_light_component", &response)?;
        Ok(blueprint_component_response(
            "blueprint.add_light_component",
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn blueprint_compile(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: BlueprintCompileArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "blueprint.compile",
                Command::BlueprintCompile {
                    path: args.path,
                    save: args.save,
                },
            )
            .await?;
        let operation = expect_blueprint_operation("blueprint.compile", &response)?;
        Ok(blueprint_operation_response(
            "blueprint.compile",
            format!("Compiled Blueprint {}.", operation.path),
            response.elapsed_ms,
            operation,
        ))
    }

    pub async fn runtime_create_led_animation(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: RuntimeAnimationArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "runtime.create_led_animation",
                Command::RuntimeCreateLedAnimation {
                    spec: args.into_spec("EmissiveColor"),
                },
            )
            .await?;
        runtime_animation_create_response("runtime.create_led_animation", response)
    }

    pub async fn runtime_create_moving_light_animation(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: RuntimeAnimationArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "runtime.create_moving_light_animation",
                Command::RuntimeCreateMovingLightAnimation {
                    spec: args.into_spec("Intensity"),
                },
            )
            .await?;
        runtime_animation_create_response("runtime.create_moving_light_animation", response)
    }

    pub async fn runtime_create_material_parameter_animation(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: RuntimeAnimationArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "runtime.create_material_parameter_animation",
                Command::RuntimeCreateMaterialParameterAnimation {
                    spec: args.into_spec("GlowAmount"),
                },
            )
            .await?;
        runtime_animation_create_response("runtime.create_material_parameter_animation", response)
    }

    pub async fn runtime_attach_animation_to_actor(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: RuntimeAttachAnimationArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "runtime.attach_animation_to_actor",
                Command::RuntimeAttachAnimationToActor {
                    names: args.names,
                    tags: args.tags,
                    blueprint: args.blueprint,
                    animations: args.animations,
                },
            )
            .await?;
        let operation =
            expect_runtime_animation_operation("runtime.attach_animation_to_actor", &response)?;
        Ok(ToolResponse {
            tool_name: "runtime.attach_animation_to_actor",
            summary: format!(
                "Attached runtime animation to {} target(s).",
                operation.count
            ),
            data: json!({
                "path": operation.path,
                "attached": operation.attached,
                "count": operation.count,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }

    pub async fn game_create_player(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: GamePlayerArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "game.create_player",
                Command::GameCreatePlayer {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_gameplay_operation("game.create_player", &response)?;
        Ok(gameplay_operation_response(
            "game.create_player",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn game_create_checkpoint(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: GameCheckpointArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "game.create_checkpoint",
                Command::GameCreateCheckpoint {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_gameplay_operation("game.create_checkpoint", &response)?;
        Ok(gameplay_operation_response(
            "game.create_checkpoint",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn game_create_interaction(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: GameInteractionArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "game.create_interaction",
                Command::GameCreateInteraction {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_gameplay_operation("game.create_interaction", &response)?;
        Ok(gameplay_operation_response(
            "game.create_interaction",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn game_create_collectibles(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: GameCollectiblesArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "game.create_collectibles",
                Command::GameCreateCollectibles {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_gameplay_operation("game.create_collectibles", &response)?;
        Ok(gameplay_operation_response(
            "game.create_collectibles",
            response.elapsed_ms,
            result,
        ))
    }

    pub async fn game_create_objective_flow(
        &self,
        arguments: serde_json::Value,
    ) -> anyhow::Result<ToolResponse> {
        let args: GameObjectiveFlowArgs = serde_json::from_value(arguments)?;
        let response = self
            .send_single(
                "game.create_objective_flow",
                Command::GameCreateObjectiveFlow {
                    spec: args.into_spec(),
                },
            )
            .await?;
        let result = expect_gameplay_operation("game.create_objective_flow", &response)?;
        Ok(gameplay_operation_response(
            "game.create_objective_flow",
            response.elapsed_ms,
            result,
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

fn expect_asset_import(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<AssetImportResult> {
    match response.results.as_slice() {
        [CommandResult::AssetImport(result)] => Ok(result.clone()),
        [] => bail!("unexpected {command_name} response: missing asset import result"),
        [result] => bail!(
            "unexpected {command_name} response: expected asset import result, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one asset import result, got {} results",
            results.len()
        ),
    }
}

fn expect_asset_validation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<AssetValidationResult> {
    match response.results.as_slice() {
        [CommandResult::AssetValidation(result)] => Ok(result.clone()),
        [] => bail!("unexpected {command_name} response: missing asset validation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected asset validation result, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one asset validation result, got {} results",
            results.len()
        ),
    }
}

fn expect_generated_mesh(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<GeneratedMeshOperation> {
    match response.results.as_slice() {
        [CommandResult::GeneratedMesh(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing generated mesh result"),
        [result] => bail!(
            "unexpected {command_name} response: expected generated mesh result, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one generated mesh result, got {} results",
            results.len()
        ),
    }
}

fn expect_static_mesh_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<StaticMeshOperationResult> {
    match response.results.as_slice() {
        [CommandResult::StaticMeshOperation(result)] => Ok(result.clone()),
        [] => bail!("unexpected {command_name} response: missing static mesh operation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected static mesh operation result, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one static mesh operation result, got {} results",
            results.len()
        ),
    }
}

fn expect_scene_assembly(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<SceneAssemblyResult> {
    match response.results.as_slice() {
        [CommandResult::SceneAssembly(result)] => Ok(result.clone()),
        [] => bail!("unexpected {command_name} response: missing scene assembly result"),
        [result] => bail!(
            "unexpected {command_name} response: expected scene assembly result, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one scene assembly result, got {} results",
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

fn expect_blueprint_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<BlueprintOperation> {
    match response.results.as_slice() {
        [CommandResult::BlueprintOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing blueprint operation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected blueprint operation, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one blueprint operation result, got {} results",
            results.len()
        ),
    }
}

fn expect_blueprint_component_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<BlueprintComponentOperation> {
    match response.results.as_slice() {
        [CommandResult::BlueprintComponentOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing blueprint component result"),
        [result] => bail!(
            "unexpected {command_name} response: expected blueprint component result, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one blueprint component result, got {} results",
            results.len()
        ),
    }
}

fn expect_runtime_animation_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<RuntimeAnimationOperation> {
    match response.results.as_slice() {
        [CommandResult::RuntimeAnimationOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing runtime animation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected runtime animation result, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one runtime animation result, got {} results",
            results.len()
        ),
    }
}

fn expect_gameplay_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<GameplayOperationResult> {
    match response.results.as_slice() {
        [CommandResult::GameplayOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing gameplay operation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected gameplay operation, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one gameplay operation result, got {} results",
            results.len()
        ),
    }
}

fn expect_landscape_operation(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<LandscapeOperation> {
    match response.results.as_slice() {
        [CommandResult::LandscapeOperation(operation)] => Ok(operation.clone()),
        [] => bail!("unexpected {command_name} response: missing landscape operation result"),
        [result] => bail!(
            "unexpected {command_name} response: expected landscape operation, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one landscape operation result, got {} results",
            results.len()
        ),
    }
}

fn expect_placement_snap(
    command_name: &str,
    response: &ResponseEnvelope,
) -> anyhow::Result<PlacementSnapResult> {
    match response.results.as_slice() {
        [CommandResult::PlacementSnap(result)] => Ok(result.clone()),
        [] => bail!("unexpected {command_name} response: missing placement snap result"),
        [result] => bail!(
            "unexpected {command_name} response: expected placement snap result, got {result:?}"
        ),
        results => bail!(
            "unexpected {command_name} response: expected exactly one placement snap result, got {} results",
            results.len()
        ),
    }
}

fn asset_import_response(
    tool_name: &'static str,
    elapsed_ms: u32,
    result: AssetImportResult,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary: format!("Imported {} asset(s).", result.count),
        data: json!({
            "count": result.count,
            "assets": result.assets,
            "elapsed_ms": elapsed_ms
        }),
    }
}

fn generated_mesh_response(
    tool_name: &'static str,
    elapsed_ms: u32,
    operation: GeneratedMeshOperation,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary: format!("Created generated mesh {}.", operation.path),
        data: json!({
            "path": operation.path,
            "created": operation.created,
            "vertex_count": operation.vertex_count,
            "triangle_count": operation.triangle_count,
            "elapsed_ms": elapsed_ms
        }),
    }
}

fn scene_assembly_response(
    tool_name: &'static str,
    elapsed_ms: u32,
    result: SceneAssemblyResult,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary: format!("Assembled scene with {} actor(s).", result.count),
        data: json!({
            "count": result.count,
            "road_count": result.road_count,
            "sidewalk_count": result.sidewalk_count,
            "building_count": result.building_count,
            "prop_count": result.prop_count,
            "spawned": result.spawned,
            "elapsed_ms": elapsed_ms
        }),
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

fn landscape_operation_response(
    tool_name: &'static str,
    summary: String,
    elapsed_ms: u32,
    operation: LandscapeOperation,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary,
        data: json!({
            "name": operation.name,
            "path": operation.path,
            "component_count": operation.component_count,
            "vertex_count": operation.vertex_count,
            "changed": operation.changed,
            "elapsed_ms": elapsed_ms
        }),
    }
}

fn blueprint_operation_response(
    tool_name: &'static str,
    summary: String,
    elapsed_ms: u32,
    operation: BlueprintOperation,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary,
        data: json!({
            "path": operation.path,
            "created": operation.created,
            "compiled": operation.compiled,
            "elapsed_ms": elapsed_ms
        }),
    }
}

fn blueprint_component_response(
    tool_name: &'static str,
    elapsed_ms: u32,
    operation: BlueprintComponentOperation,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary: format!("Updated {} Blueprint component(s).", operation.count),
        data: json!({
            "blueprint": operation.blueprint,
            "components": operation.components,
            "count": operation.count,
            "elapsed_ms": elapsed_ms
        }),
    }
}

fn runtime_animation_create_response(
    tool_name: &'static str,
    response: ResponseEnvelope,
) -> anyhow::Result<ToolResponse> {
    let operation = expect_runtime_animation_operation(tool_name, &response)?;
    Ok(ToolResponse {
        tool_name,
        summary: format!(
            "Created runtime animation {}.",
            operation.path.as_deref().unwrap_or("<none>")
        ),
        data: json!({
            "path": operation.path,
            "attached": operation.attached,
            "count": operation.count,
            "elapsed_ms": response.elapsed_ms
        }),
    })
}

fn gameplay_operation_response(
    tool_name: &'static str,
    elapsed_ms: u32,
    result: GameplayOperationResult,
) -> ToolResponse {
    ToolResponse {
        tool_name,
        summary: format!("Created gameplay actors: {} actor(s).", result.count),
        data: json!({
            "count": result.count,
            "player_count": result.player_count,
            "checkpoint_count": result.checkpoint_count,
            "interaction_count": result.interaction_count,
            "collectible_count": result.collectible_count,
            "objective_count": result.objective_count,
            "spawned": result.spawned,
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
struct AssetImportArgs {
    source_file: String,
    destination_path: String,
    #[serde(default)]
    replace_existing: bool,
    #[serde(default)]
    save: bool,
    srgb: Option<bool>,
    generate_collision: Option<bool>,
}

impl AssetImportArgs {
    fn into_spec(self) -> AssetImportSpec {
        AssetImportSpec {
            source_file: self.source_file,
            destination_path: self.destination_path,
            replace_existing: self.replace_existing,
            save: self.save,
            srgb: self.srgb,
            generate_collision: self.generate_collision,
        }
    }
}

#[derive(Debug, Default, Deserialize)]
struct AssetBulkImportArgs {
    items: Vec<AssetImportItemArgs>,
}

#[derive(Debug, Deserialize)]
struct AssetImportItemArgs {
    kind: String,
    source_file: String,
    destination_path: String,
    #[serde(default)]
    replace_existing: bool,
    #[serde(default)]
    save: bool,
    srgb: Option<bool>,
    generate_collision: Option<bool>,
}

impl AssetImportItemArgs {
    fn into_item(self) -> AssetImportItem {
        AssetImportItem {
            kind: self.kind,
            source_file: self.source_file,
            destination_path: self.destination_path,
            replace_existing: self.replace_existing,
            save: self.save,
            srgb: self.srgb,
            generate_collision: self.generate_collision,
        }
    }
}

#[derive(Debug, Default, Deserialize)]
struct AssetValidateArgs {
    paths: Vec<String>,
}

#[derive(Debug, Deserialize)]
struct GeneratedBuildingArgs {
    path: String,
    #[serde(default = "default_building_width")]
    width: f64,
    #[serde(default = "default_building_depth")]
    depth: f64,
    #[serde(default = "default_building_height")]
    height: f64,
    #[serde(default = "default_building_floors")]
    floors: u32,
    window_rows: Option<u32>,
    #[serde(default = "default_building_window_columns")]
    window_columns: u32,
    material: Option<String>,
}

impl GeneratedBuildingArgs {
    fn into_spec(self) -> GeneratedBuildingSpec {
        GeneratedBuildingSpec {
            path: self.path,
            width: self.width,
            depth: self.depth,
            height: self.height,
            floors: self.floors,
            window_rows: self.window_rows.unwrap_or(self.floors),
            window_columns: self.window_columns,
            material: self.material,
        }
    }
}

#[derive(Debug, Deserialize)]
struct GeneratedSignArgs {
    path: String,
    #[serde(default = "default_sign_width")]
    width: f64,
    #[serde(default = "default_sign_height")]
    height: f64,
    #[serde(default = "default_sign_depth")]
    depth: f64,
    text: Option<String>,
    material: Option<String>,
}

impl GeneratedSignArgs {
    fn into_spec(self) -> GeneratedSignSpec {
        GeneratedSignSpec {
            path: self.path,
            width: self.width,
            height: self.height,
            depth: self.depth,
            text: self.text,
            material: self.material,
        }
    }
}

#[derive(Debug, Deserialize)]
struct StaticMeshCollisionArgs {
    paths: Vec<String>,
    #[serde(default = "default_collision_trace")]
    collision_trace: String,
    #[serde(default = "default_true")]
    simple_collision: bool,
    #[serde(default = "default_true")]
    rebuild: bool,
}

impl StaticMeshCollisionArgs {
    fn into_spec(self) -> StaticMeshCollisionSpec {
        StaticMeshCollisionSpec {
            paths: self.paths,
            collision_trace: self.collision_trace,
            simple_collision: self.simple_collision,
            rebuild: self.rebuild,
        }
    }
}

#[derive(Debug, Deserialize)]
struct RoadNetworkArgs {
    name_prefix: String,
    scene: Option<String>,
    group: Option<String>,
    #[serde(default)]
    origin: [f64; 3],
    #[serde(default = "default_scene_rows")]
    rows: u32,
    #[serde(default = "default_scene_columns")]
    columns: u32,
    #[serde(default = "default_block_size")]
    block_size: [f64; 2],
    #[serde(default = "default_road_width")]
    road_width: f64,
    #[serde(default = "default_road_thickness")]
    road_thickness: f64,
    road_mesh: Option<String>,
}

impl RoadNetworkArgs {
    fn into_spec(self) -> RoadNetworkSpec {
        RoadNetworkSpec {
            name_prefix: self.name_prefix,
            scene: self.scene,
            group: self.group,
            origin: self.origin,
            rows: self.rows,
            columns: self.columns,
            block_size: self.block_size,
            road_width: self.road_width,
            road_thickness: self.road_thickness,
            road_mesh: self.road_mesh,
        }
    }
}

#[derive(Debug, Deserialize)]
struct GridPlacementArgs {
    name_prefix: String,
    mesh: String,
    scene: Option<String>,
    group: Option<String>,
    #[serde(default)]
    origin: [f64; 3],
    #[serde(default = "default_scene_rows")]
    rows: u32,
    #[serde(default = "default_scene_columns")]
    columns: u32,
    #[serde(default = "default_grid_spacing")]
    spacing: [f64; 2],
    #[serde(default)]
    rotation: [f64; 3],
    #[serde(default = "default_scale")]
    scale: [f64; 3],
    #[serde(default)]
    yaw_variation: f64,
    #[serde(default)]
    scale_variation: f64,
    #[serde(default)]
    seed: u32,
}

impl GridPlacementArgs {
    fn into_spec(self) -> GridPlacementSpec {
        GridPlacementSpec {
            name_prefix: self.name_prefix,
            mesh: self.mesh,
            scene: self.scene,
            group: self.group,
            origin: self.origin,
            rows: self.rows,
            columns: self.columns,
            spacing: self.spacing,
            rotation: self.rotation,
            scale: self.scale,
            yaw_variation: self.yaw_variation,
            scale_variation: self.scale_variation,
            seed: self.seed,
        }
    }
}

#[derive(Debug, Deserialize)]
struct CityBlockArgs {
    name_prefix: String,
    scene: Option<String>,
    group: Option<String>,
    #[serde(default)]
    origin: [f64; 3],
    #[serde(default = "default_block_size")]
    size: [f64; 2],
    #[serde(default = "default_road_width")]
    road_width: f64,
    #[serde(default = "default_sidewalk_width")]
    sidewalk_width: f64,
    road_mesh: Option<String>,
    sidewalk_mesh: Option<String>,
    building_mesh: String,
    #[serde(default = "default_city_building_rows")]
    building_rows: u32,
    #[serde(default = "default_city_building_columns")]
    building_columns: u32,
    #[serde(default = "default_city_building_scale")]
    building_scale: [f64; 3],
    #[serde(default)]
    seed: u32,
}

impl CityBlockArgs {
    fn into_spec(self) -> CityBlockSpec {
        CityBlockSpec {
            name_prefix: self.name_prefix,
            scene: self.scene,
            group: self.group,
            origin: self.origin,
            size: self.size,
            road_width: self.road_width,
            sidewalk_width: self.sidewalk_width,
            road_mesh: self.road_mesh,
            sidewalk_mesh: self.sidewalk_mesh,
            building_mesh: self.building_mesh,
            building_rows: self.building_rows,
            building_columns: self.building_columns,
            building_scale: self.building_scale,
            seed: self.seed,
        }
    }
}

#[derive(Debug, Deserialize)]
struct DistrictArgs {
    name_prefix: String,
    #[serde(default = "default_district_preset")]
    preset: String,
    scene: Option<String>,
    group: Option<String>,
    #[serde(default)]
    origin: [f64; 3],
    #[serde(default = "default_district_blocks")]
    blocks: [u32; 2],
    #[serde(default = "default_block_size")]
    block_size: [f64; 2],
    #[serde(default = "default_road_width")]
    road_width: f64,
    road_mesh: Option<String>,
    building_mesh: String,
    #[serde(default)]
    seed: u32,
}

impl DistrictArgs {
    fn into_spec(self) -> DistrictSpec {
        DistrictSpec {
            name_prefix: self.name_prefix,
            preset: self.preset,
            scene: self.scene,
            group: self.group,
            origin: self.origin,
            blocks: self.blocks,
            block_size: self.block_size,
            road_width: self.road_width,
            road_mesh: self.road_mesh,
            building_mesh: self.building_mesh,
            seed: self.seed,
        }
    }
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
struct LandscapeCreateArgs {
    name: String,
    #[serde(default = "default_landscape_component_count")]
    component_count: [u32; 2],
    #[serde(default = "default_landscape_section_size")]
    section_size: u32,
    #[serde(default = "default_landscape_sections_per_component")]
    sections_per_component: u32,
    #[serde(default)]
    location: [f64; 3],
    #[serde(default = "default_landscape_scale")]
    scale: [f64; 3],
    material: Option<String>,
    #[serde(default)]
    tags: Vec<String>,
}

impl LandscapeCreateArgs {
    fn into_spec(self) -> LandscapeCreateSpec {
        LandscapeCreateSpec {
            name: self.name,
            component_count: self.component_count,
            section_size: self.section_size,
            sections_per_component: self.sections_per_component,
            location: self.location,
            scale: self.scale,
            material: self.material,
            tags: self.tags,
        }
    }
}

#[derive(Debug, Deserialize)]
struct LandscapeHeightfieldArgs {
    name: String,
    #[serde(default)]
    width: u32,
    #[serde(default)]
    height: u32,
    #[serde(default)]
    base_height: f64,
    #[serde(default = "default_landscape_amplitude")]
    amplitude: f64,
    #[serde(default = "default_landscape_frequency")]
    frequency: f64,
    #[serde(default)]
    seed: u32,
    #[serde(default)]
    city_pad_radius: f64,
    #[serde(default = "default_city_pad_falloff")]
    city_pad_falloff: f64,
    #[serde(default)]
    samples: Vec<f64>,
}

impl LandscapeHeightfieldArgs {
    fn into_patch(self) -> LandscapeHeightPatch {
        LandscapeHeightPatch {
            name: self.name,
            width: self.width,
            height: self.height,
            base_height: self.base_height,
            amplitude: self.amplitude,
            frequency: self.frequency,
            seed: self.seed,
            city_pad_radius: self.city_pad_radius,
            city_pad_falloff: self.city_pad_falloff,
            samples: self.samples,
        }
    }
}

#[derive(Debug, Default, Deserialize)]
struct LandscapePaintLayersArgs {
    name: String,
    material: Option<String>,
    #[serde(default)]
    layers: Vec<String>,
}

impl LandscapePaintLayersArgs {
    fn into_paint(self) -> LandscapeLayerPaint {
        LandscapeLayerPaint {
            name: self.name,
            material: self.material,
            layers: self.layers,
        }
    }
}

#[derive(Debug, Default, Deserialize)]
struct PlacementBulkSnapArgs {
    #[serde(default)]
    names: Vec<String>,
    #[serde(default)]
    tags: Vec<String>,
    #[serde(default)]
    include_generated: bool,
    #[serde(default = "default_trace_distance")]
    trace_distance: f64,
    #[serde(default)]
    offset_z: f64,
}

impl PlacementBulkSnapArgs {
    fn into_spec(self) -> PlacementSnapSpec {
        PlacementSnapSpec {
            names: self.names,
            tags: self.tags,
            include_generated: self.include_generated,
            trace_distance: self.trace_distance,
            offset_z: self.offset_z,
        }
    }
}

#[derive(Debug, Deserialize)]
struct BlueprintCreateActorArgs {
    path: String,
    #[serde(default = "default_blueprint_parent_class")]
    parent_class: String,
}

#[derive(Debug, Deserialize)]
struct BlueprintCompileArgs {
    path: String,
    #[serde(default)]
    save: bool,
}

#[derive(Debug, Deserialize)]
struct StaticMeshComponentArgs {
    blueprint: String,
    name: String,
    mesh: String,
    material: Option<String>,
    #[serde(default)]
    location: [f64; 3],
    #[serde(default)]
    rotation: [f64; 3],
    #[serde(default = "default_scale")]
    scale: [f64; 3],
}

impl StaticMeshComponentArgs {
    fn into_spec(self) -> StaticMeshComponentSpec {
        StaticMeshComponentSpec {
            name: self.name,
            mesh: self.mesh,
            material: self.material,
            transform: Transform {
                location: self.location,
                rotation: self.rotation,
                scale: self.scale,
            },
        }
    }
}

#[derive(Debug, Deserialize)]
struct LightComponentArgs {
    blueprint: String,
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
}

impl LightComponentArgs {
    fn into_spec(self) -> LightComponentSpec {
        LightComponentSpec {
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
        }
    }
}

#[derive(Debug, Deserialize)]
struct RuntimeAnimationArgs {
    path: String,
    target_component: Option<String>,
    parameter_name: Option<String>,
    #[serde(default = "default_led_color_a")]
    color_a: [f64; 4],
    #[serde(default = "default_led_color_b")]
    color_b: [f64; 4],
    #[serde(default)]
    from_scalar: f64,
    #[serde(default = "default_runtime_to_scalar")]
    to_scalar: f64,
    #[serde(default = "default_runtime_speed")]
    speed: f64,
    #[serde(default = "default_runtime_amplitude")]
    amplitude: f64,
    #[serde(default = "default_runtime_axis")]
    axis: [f64; 3],
    #[serde(default = "default_light_intensity")]
    base_intensity: f64,
    #[serde(default)]
    phase_offset: f64,
}

impl RuntimeAnimationArgs {
    fn into_spec(self, default_parameter: &str) -> RuntimeAnimationSpec {
        RuntimeAnimationSpec {
            path: self.path,
            target_component: self.target_component,
            parameter_name: self
                .parameter_name
                .unwrap_or_else(|| default_parameter.to_string()),
            color_a: self.color_a,
            color_b: self.color_b,
            from_scalar: self.from_scalar,
            to_scalar: self.to_scalar,
            speed: self.speed,
            amplitude: self.amplitude,
            axis: self.axis,
            base_intensity: self.base_intensity,
            phase_offset: self.phase_offset,
        }
    }
}

#[derive(Debug, Default, Deserialize)]
struct RuntimeAttachAnimationArgs {
    #[serde(default)]
    names: Vec<String>,
    #[serde(default)]
    tags: Vec<String>,
    blueprint: Option<String>,
    animations: Vec<String>,
}

#[derive(Debug, Deserialize)]
struct GamePlayerArgs {
    name: String,
    scene: Option<String>,
    group: Option<String>,
    #[serde(default)]
    location: [f64; 3],
    #[serde(default)]
    rotation: [f64; 3],
    spawn_tag: Option<String>,
    #[serde(default)]
    create_camera: bool,
    camera_name: Option<String>,
    #[serde(default = "default_game_camera_location")]
    camera_location: [f64; 3],
    #[serde(default = "default_game_camera_rotation")]
    camera_rotation: [f64; 3],
}

impl GamePlayerArgs {
    fn into_spec(self) -> GamePlayerSpec {
        GamePlayerSpec {
            name: self.name,
            scene: self.scene,
            group: self.group,
            location: self.location,
            rotation: self.rotation,
            spawn_tag: self.spawn_tag,
            create_camera: self.create_camera,
            camera_name: self.camera_name,
            camera_location: self.camera_location,
            camera_rotation: self.camera_rotation,
        }
    }
}

#[derive(Debug, Deserialize)]
struct GameCheckpointArgs {
    name: String,
    scene: Option<String>,
    group: Option<String>,
    checkpoint_id: Option<String>,
    #[serde(default)]
    order: u32,
    #[serde(default)]
    location: [f64; 3],
    #[serde(default)]
    rotation: [f64; 3],
    #[serde(default = "default_game_checkpoint_scale")]
    scale: [f64; 3],
}

impl GameCheckpointArgs {
    fn into_spec(self) -> GameCheckpointSpec {
        let checkpoint_id = self.checkpoint_id.unwrap_or_else(|| self.name.clone());
        GameCheckpointSpec {
            name: self.name,
            scene: self.scene,
            group: self.group,
            checkpoint_id,
            order: self.order,
            location: self.location,
            rotation: self.rotation,
            scale: self.scale,
        }
    }
}

#[derive(Debug, Deserialize)]
struct GameInteractionArgs {
    name: String,
    kind: String,
    scene: Option<String>,
    group: Option<String>,
    interaction_id: Option<String>,
    target: Option<String>,
    action: Option<String>,
    prompt: Option<String>,
    #[serde(default)]
    location: [f64; 3],
    #[serde(default)]
    rotation: [f64; 3],
    #[serde(default = "default_game_interaction_scale")]
    scale: [f64; 3],
}

impl GameInteractionArgs {
    fn into_spec(self) -> GameInteractionSpec {
        GameInteractionSpec {
            name: self.name,
            kind: self.kind,
            scene: self.scene,
            group: self.group,
            interaction_id: self.interaction_id,
            target: self.target,
            action: self.action,
            prompt: self.prompt,
            location: self.location,
            rotation: self.rotation,
            scale: self.scale,
        }
    }
}

#[derive(Debug, Deserialize)]
struct GameCollectiblesArgs {
    name_prefix: String,
    mesh: String,
    scene: Option<String>,
    group: Option<String>,
    #[serde(default)]
    origin: [f64; 3],
    #[serde(default = "default_scene_rows")]
    rows: u32,
    #[serde(default = "default_scene_columns")]
    columns: u32,
    #[serde(default = "default_game_collectible_spacing")]
    spacing: [f64; 2],
    #[serde(default = "default_game_collectible_value")]
    value: i32,
    #[serde(default)]
    rotation: [f64; 3],
    #[serde(default = "default_game_collectible_scale")]
    scale: [f64; 3],
    animation: Option<String>,
}

impl GameCollectiblesArgs {
    fn into_spec(self) -> GameCollectiblesSpec {
        GameCollectiblesSpec {
            name_prefix: self.name_prefix,
            mesh: self.mesh,
            scene: self.scene,
            group: self.group,
            origin: self.origin,
            rows: self.rows,
            columns: self.columns,
            spacing: self.spacing,
            value: self.value,
            rotation: self.rotation,
            scale: self.scale,
            animation: self.animation,
        }
    }
}

#[derive(Debug, Deserialize)]
struct GameObjectiveFlowArgs {
    name_prefix: String,
    scene: Option<String>,
    group: Option<String>,
    #[serde(default)]
    steps: Vec<GameObjectiveStepArgs>,
}

impl GameObjectiveFlowArgs {
    fn into_spec(self) -> GameObjectiveFlowSpec {
        GameObjectiveFlowSpec {
            name_prefix: self.name_prefix,
            scene: self.scene,
            group: self.group,
            steps: self
                .steps
                .into_iter()
                .map(GameObjectiveStepArgs::into_spec)
                .collect(),
        }
    }
}

#[derive(Debug, Deserialize)]
struct GameObjectiveStepArgs {
    id: String,
    label: Option<String>,
    #[serde(default = "default_game_objective_kind")]
    kind: String,
    #[serde(default)]
    location: [f64; 3],
    #[serde(default)]
    rotation: [f64; 3],
    #[serde(default = "default_game_objective_scale")]
    scale: [f64; 3],
}

impl GameObjectiveStepArgs {
    fn into_spec(self) -> GameObjectiveStepSpec {
        let label = self.label.unwrap_or_else(|| self.id.clone());
        GameObjectiveStepSpec {
            id: self.id,
            label,
            kind: self.kind,
            location: self.location,
            rotation: self.rotation,
            scale: self.scale,
        }
    }
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

fn default_building_width() -> f64 {
    800.0
}

fn default_building_depth() -> f64 {
    600.0
}

fn default_building_height() -> f64 {
    2400.0
}

fn default_building_floors() -> u32 {
    12
}

fn default_building_window_columns() -> u32 {
    6
}

fn default_sign_width() -> f64 {
    900.0
}

fn default_sign_height() -> f64 {
    240.0
}

fn default_sign_depth() -> f64 {
    30.0
}

fn default_collision_trace() -> String {
    "project_default".to_string()
}

fn default_scene_rows() -> u32 {
    2
}

fn default_scene_columns() -> u32 {
    2
}

fn default_block_size() -> [f64; 2] {
    [2400.0, 1800.0]
}

fn default_road_width() -> f64 {
    320.0
}

fn default_road_thickness() -> f64 {
    20.0
}

fn default_grid_spacing() -> [f64; 2] {
    [600.0, 600.0]
}

fn default_sidewalk_width() -> f64 {
    180.0
}

fn default_city_building_rows() -> u32 {
    2
}

fn default_city_building_columns() -> u32 {
    2
}

fn default_city_building_scale() -> [f64; 3] {
    [3.0, 3.0, 8.0]
}

fn default_district_preset() -> String {
    "downtown".to_string()
}

fn default_district_blocks() -> [u32; 2] {
    [2, 2]
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

fn default_landscape_component_count() -> [u32; 2] {
    [4, 4]
}

fn default_landscape_section_size() -> u32 {
    63
}

fn default_landscape_sections_per_component() -> u32 {
    1
}

fn default_landscape_scale() -> [f64; 3] {
    [100.0, 100.0, 100.0]
}

fn default_landscape_amplitude() -> f64 {
    0.0
}

fn default_landscape_frequency() -> f64 {
    1.0
}

fn default_city_pad_falloff() -> f64 {
    1000.0
}

fn default_trace_distance() -> f64 {
    50000.0
}

fn default_blueprint_parent_class() -> String {
    "Actor".to_string()
}

fn default_led_color_a() -> [f64; 4] {
    [0.0, 0.0, 0.0, 1.0]
}

fn default_led_color_b() -> [f64; 4] {
    [0.0, 0.85, 1.0, 1.0]
}

fn default_runtime_to_scalar() -> f64 {
    10.0
}

fn default_runtime_speed() -> f64 {
    1.0
}

fn default_runtime_amplitude() -> f64 {
    100.0
}

fn default_runtime_axis() -> [f64; 3] {
    [0.0, 0.0, 1.0]
}

fn default_game_camera_location() -> [f64; 3] {
    [-400.0, 0.0, 260.0]
}

fn default_game_camera_rotation() -> [f64; 3] {
    [-10.0, 0.0, 0.0]
}

fn default_game_checkpoint_scale() -> [f64; 3] {
    [2.0, 2.0, 0.25]
}

fn default_game_interaction_scale() -> [f64; 3] {
    [0.6, 0.6, 0.6]
}

fn default_game_collectible_spacing() -> [f64; 2] {
    [180.0, 180.0]
}

fn default_game_collectible_value() -> i32 {
    1
}

fn default_game_collectible_scale() -> [f64; 3] {
    [0.25, 0.25, 0.25]
}

fn default_game_objective_kind() -> String {
    "location".to_string()
}

fn default_game_objective_scale() -> [f64; 3] {
    [1.0, 1.0, 1.0]
}
