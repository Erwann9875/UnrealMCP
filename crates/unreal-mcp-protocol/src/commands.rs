use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case", tag = "type", content = "data")]
pub enum Command {
    Ping,
    Status,
    Capabilities,
    LevelCreate {
        path: String,
        open: bool,
        save: bool,
    },
    LevelOpen {
        path: String,
    },
    LevelSave {
        path: Option<String>,
    },
    LevelList,
    WorldBulkSpawn {
        actors: Vec<ActorSpawnSpec>,
    },
    WorldBulkDelete {
        names: Vec<String>,
        tags: Vec<String>,
    },
    WorldQuery {
        names: Vec<String>,
        tags: Vec<String>,
        include_generated: bool,
        limit: Option<u32>,
    },
    WorldSnapshot {
        path: Option<String>,
        tags: Vec<String>,
    },
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Transform {
    pub location: [f64; 3],
    pub rotation: [f64; 3],
    pub scale: [f64; 3],
}

impl Default for Transform {
    fn default() -> Self {
        Self {
            location: [0.0, 0.0, 0.0],
            rotation: [0.0, 0.0, 0.0],
            scale: [1.0, 1.0, 1.0],
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ActorSpawnSpec {
    pub name: String,
    pub mesh: String,
    pub transform: Transform,
    pub scene: Option<String>,
    pub group: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BridgeStatus {
    pub connected: bool,
    pub bridge_version: Option<String>,
    pub unreal_version: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LevelInfo {
    pub path: String,
    pub name: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LevelList {
    pub levels: Vec<LevelInfo>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LevelOperation {
    pub path: String,
    pub opened: bool,
    pub saved: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SpawnedActor {
    pub name: String,
    pub path: String,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ActorQuery {
    pub name: String,
    pub path: String,
    pub class_name: String,
    pub transform: Transform,
    pub tags: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct WorldQueryResult {
    pub actors: Vec<ActorQuery>,
    pub total_count: usize,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case", tag = "type", content = "data")]
pub enum CommandResult {
    Pong {
        bridge_version: String,
    },
    Status(BridgeStatus),
    Capabilities {
        commands: Vec<String>,
    },
    LevelList(LevelList),
    LevelOperation(LevelOperation),
    WorldBulkSpawn {
        spawned: Vec<SpawnedActor>,
        count: usize,
    },
    WorldBulkDelete {
        deleted: Vec<String>,
        count: usize,
    },
    WorldQuery(WorldQueryResult),
    WorldSnapshot {
        path: String,
        total_count: usize,
    },
}
