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
    AssetCreateFolder {
        path: String,
    },
    MaterialCreate {
        path: String,
        base_color: [f64; 4],
        metallic: f64,
        roughness: f64,
        specular: f64,
        emissive_color: [f64; 4],
    },
    MaterialCreateInstance {
        path: String,
        parent: String,
        scalar_parameters: Vec<MaterialParameter>,
        vector_parameters: Vec<MaterialParameter>,
        texture_parameters: Vec<MaterialParameter>,
    },
    MaterialCreateProceduralTexture {
        spec: TextureCreateSpec,
    },
    MaterialSetParameters {
        path: String,
        scalar_parameters: Vec<MaterialParameter>,
        vector_parameters: Vec<MaterialParameter>,
        texture_parameters: Vec<MaterialParameter>,
    },
    MaterialBulkApply {
        assignments: Vec<MaterialAssignment>,
    },
    WorldBulkSetMaterials {
        names: Vec<String>,
        tags: Vec<String>,
        material: String,
        slot: u32,
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
pub struct MaterialParameter {
    pub name: String,
    pub value: MaterialParameterValue,
}

impl MaterialParameter {
    pub fn scalar(name: impl Into<String>, value: f64) -> Self {
        Self {
            name: name.into(),
            value: MaterialParameterValue::Scalar(value),
        }
    }

    pub fn vector(name: impl Into<String>, value: [f64; 4]) -> Self {
        Self {
            name: name.into(),
            value: MaterialParameterValue::Vector(value),
        }
    }

    pub fn texture(name: impl Into<String>, value: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            value: MaterialParameterValue::Texture(value.into()),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case", tag = "type", content = "value")]
pub enum MaterialParameterValue {
    Scalar(f64),
    Vector([f64; 4]),
    Texture(String),
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct TextureCreateSpec {
    pub path: String,
    pub pattern: String,
    pub width: u32,
    pub height: u32,
    pub color_a: [f64; 4],
    pub color_b: [f64; 4],
    pub checker_size: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MaterialAssignment {
    pub material: String,
    pub names: Vec<String>,
    pub tags: Vec<String>,
    pub slot: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AssetOperation {
    pub path: String,
    pub created: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MaterialOperation {
    pub path: String,
    pub parent: Option<String>,
    pub created: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ProceduralTextureOperation {
    pub path: String,
    pub width: u32,
    pub height: u32,
    pub created: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MaterialParameterOperation {
    pub path: String,
    pub scalar_count: usize,
    pub vector_count: usize,
    pub texture_count: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MaterialAppliedActor {
    pub name: String,
    pub path: String,
    pub material: String,
    pub slot: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MaterialApplyResult {
    pub applied: Vec<MaterialAppliedActor>,
    pub count: usize,
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
    AssetOperation(AssetOperation),
    MaterialOperation(MaterialOperation),
    ProceduralTextureOperation(ProceduralTextureOperation),
    MaterialParameterOperation(MaterialParameterOperation),
    MaterialApply(MaterialApplyResult),
}
