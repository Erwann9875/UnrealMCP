use serde_json::json;

use anyhow::{bail, ensure};
use serde::Deserialize;
use unreal_mcp_protocol::{
    ActorSpawnSpec, BridgeStatus, Command, CommandResult, ErrorMode, LevelOperation,
    RequestEnvelope, ResponseEnvelope, ResponseMode, Transform,
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
        let actors = args.actors.into_iter().map(ActorSpawnArgs::into_spec).collect();
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

fn default_true() -> bool {
    true
}

fn default_scale() -> [f64; 3] {
    [1.0, 1.0, 1.0]
}
