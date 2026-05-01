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
    AssetImportTexture {
        spec: AssetImportSpec,
    },
    AssetImportStaticMesh {
        spec: AssetImportSpec,
    },
    AssetBulkImport {
        items: Vec<AssetImportItem>,
    },
    AssetValidate {
        spec: AssetValidateSpec,
    },
    MeshCreateBuilding {
        spec: GeneratedBuildingSpec,
    },
    MeshCreateSign {
        spec: GeneratedSignSpec,
    },
    StaticMeshSetCollision {
        spec: StaticMeshCollisionSpec,
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
    LightingSetNightScene {
        moon_rotation: [f64; 3],
        moon_intensity: f64,
        moon_color: [f64; 4],
        sky_intensity: f64,
        fog_density: f64,
        exposure_compensation: f64,
    },
    LightingSetSky {
        sky_intensity: f64,
        lower_hemisphere_color: [f64; 4],
    },
    LightingSetFog {
        density: f64,
        height_falloff: f64,
        color: [f64; 4],
        start_distance: f64,
    },
    LightingSetPostProcess {
        exposure_compensation: f64,
        min_brightness: f64,
        max_brightness: f64,
        bloom_intensity: f64,
    },
    LightingBulkSetLights {
        lights: Vec<LightSpec>,
    },
    LightingSetTimeOfDay {
        sun_rotation: [f64; 3],
        sun_intensity: f64,
        sun_color: [f64; 4],
    },
    LandscapeCreate {
        spec: LandscapeCreateSpec,
    },
    LandscapeSetHeightfield {
        patch: LandscapeHeightPatch,
    },
    LandscapePaintLayers {
        paint: LandscapeLayerPaint,
    },
    PlacementBulkSnapToGround {
        spec: PlacementSnapSpec,
    },
    BlueprintCreateActor {
        path: String,
        parent_class: String,
    },
    BlueprintAddStaticMeshComponent {
        blueprint: String,
        component: StaticMeshComponentSpec,
    },
    BlueprintAddLightComponent {
        blueprint: String,
        component: LightComponentSpec,
    },
    BlueprintCompile {
        path: String,
        save: bool,
    },
    RuntimeCreateLedAnimation {
        spec: RuntimeAnimationSpec,
    },
    RuntimeCreateMovingLightAnimation {
        spec: RuntimeAnimationSpec,
    },
    RuntimeCreateMaterialParameterAnimation {
        spec: RuntimeAnimationSpec,
    },
    RuntimeAttachAnimationToActor {
        names: Vec<String>,
        tags: Vec<String>,
        blueprint: Option<String>,
        animations: Vec<String>,
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

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct LightSpec {
    pub name: String,
    pub kind: String,
    pub transform: Transform,
    pub color: [f64; 4],
    pub intensity: f64,
    pub attenuation_radius: f64,
    pub source_radius: f64,
    pub source_width: f64,
    pub source_height: f64,
    pub tags: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct StaticMeshComponentSpec {
    pub name: String,
    pub mesh: String,
    pub material: Option<String>,
    pub transform: Transform,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct LightComponentSpec {
    pub name: String,
    pub kind: String,
    pub transform: Transform,
    pub color: [f64; 4],
    pub intensity: f64,
    pub attenuation_radius: f64,
    pub source_radius: f64,
    pub source_width: f64,
    pub source_height: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct RuntimeAnimationSpec {
    pub path: String,
    pub target_component: Option<String>,
    pub parameter_name: String,
    pub color_a: [f64; 4],
    pub color_b: [f64; 4],
    pub from_scalar: f64,
    pub to_scalar: f64,
    pub speed: f64,
    pub amplitude: f64,
    pub axis: [f64; 3],
    pub base_intensity: f64,
    pub phase_offset: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct LandscapeCreateSpec {
    pub name: String,
    pub component_count: [u32; 2],
    pub section_size: u32,
    pub sections_per_component: u32,
    pub location: [f64; 3],
    pub scale: [f64; 3],
    pub material: Option<String>,
    pub tags: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct LandscapeHeightPatch {
    pub name: String,
    pub width: u32,
    pub height: u32,
    pub base_height: f64,
    pub amplitude: f64,
    pub frequency: f64,
    pub seed: u32,
    pub city_pad_radius: f64,
    pub city_pad_falloff: f64,
    pub samples: Vec<f64>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct LandscapeLayerPaint {
    pub name: String,
    pub material: Option<String>,
    pub layers: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct PlacementSnapSpec {
    pub names: Vec<String>,
    pub tags: Vec<String>,
    pub include_generated: bool,
    pub trace_distance: f64,
    pub offset_z: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct AssetImportSpec {
    pub source_file: String,
    pub destination_path: String,
    pub replace_existing: bool,
    pub save: bool,
    pub srgb: Option<bool>,
    pub generate_collision: Option<bool>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct AssetImportItem {
    pub kind: String,
    pub source_file: String,
    pub destination_path: String,
    pub replace_existing: bool,
    pub save: bool,
    pub srgb: Option<bool>,
    pub generate_collision: Option<bool>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AssetValidateSpec {
    pub paths: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct GeneratedBuildingSpec {
    pub path: String,
    pub width: f64,
    pub depth: f64,
    pub height: f64,
    pub floors: u32,
    pub window_rows: u32,
    pub window_columns: u32,
    pub material: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct GeneratedSignSpec {
    pub path: String,
    pub width: f64,
    pub height: f64,
    pub depth: f64,
    pub text: Option<String>,
    pub material: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct StaticMeshCollisionSpec {
    pub paths: Vec<String>,
    pub collision_trace: String,
    pub simple_collision: bool,
    pub rebuild: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AssetOperation {
    pub path: String,
    pub created: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AssetImportOperation {
    pub source_file: String,
    pub path: String,
    pub class_name: String,
    pub imported: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AssetImportResult {
    pub assets: Vec<AssetImportOperation>,
    pub count: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AssetValidation {
    pub path: String,
    pub exists: bool,
    pub class_name: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AssetValidationResult {
    pub assets: Vec<AssetValidation>,
    pub count: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct GeneratedMeshOperation {
    pub path: String,
    pub created: bool,
    pub vertex_count: usize,
    pub triangle_count: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct StaticMeshOperation {
    pub path: String,
    pub changed: bool,
    pub collision_trace: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct StaticMeshOperationResult {
    pub meshes: Vec<StaticMeshOperation>,
    pub count: usize,
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

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LightingOperation {
    pub changed: Vec<String>,
    pub count: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LightSummary {
    pub name: String,
    pub path: String,
    pub kind: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BlueprintOperation {
    pub path: String,
    pub created: bool,
    pub compiled: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BlueprintComponentOperation {
    pub blueprint: String,
    pub components: Vec<String>,
    pub count: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RuntimeAnimationOperation {
    pub path: Option<String>,
    pub attached: Vec<String>,
    pub count: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LandscapeOperation {
    pub name: String,
    pub path: String,
    pub component_count: [u32; 2],
    pub vertex_count: [u32; 2],
    pub changed: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct PlacementSnapActor {
    pub name: String,
    pub path: String,
    pub old_location: [f64; 3],
    pub new_location: [f64; 3],
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct PlacementSnapResult {
    pub actors: Vec<PlacementSnapActor>,
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
    AssetImport(AssetImportResult),
    AssetValidation(AssetValidationResult),
    GeneratedMesh(GeneratedMeshOperation),
    StaticMeshOperation(StaticMeshOperationResult),
    MaterialOperation(MaterialOperation),
    ProceduralTextureOperation(ProceduralTextureOperation),
    MaterialParameterOperation(MaterialParameterOperation),
    MaterialApply(MaterialApplyResult),
    LightingOperation(LightingOperation),
    LightingBulkSetLights {
        lights: Vec<LightSummary>,
        count: usize,
    },
    LandscapeOperation(LandscapeOperation),
    PlacementSnap(PlacementSnapResult),
    BlueprintOperation(BlueprintOperation),
    BlueprintComponentOperation(BlueprintComponentOperation),
    RuntimeAnimationOperation(RuntimeAnimationOperation),
}
