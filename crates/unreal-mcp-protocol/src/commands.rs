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
    RoadCreateNetwork {
        spec: RoadNetworkSpec,
    },
    SceneBulkPlaceOnGrid {
        spec: GridPlacementSpec,
    },
    SceneCreateCityBlock {
        spec: CityBlockSpec,
    },
    SceneCreateDistrict {
        spec: DistrictSpec,
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
    GameCreatePlayer {
        spec: GamePlayerSpec,
    },
    GameCreateCheckpoint {
        spec: GameCheckpointSpec,
    },
    GameCreateInteraction {
        spec: GameInteractionSpec,
    },
    GameCreateCollectibles {
        spec: GameCollectiblesSpec,
    },
    GameCreateObjectiveFlow {
        spec: GameObjectiveFlowSpec,
    },
    GameplayCreateSystem {
        spec: GameplayCreateSystemSpec,
    },
    GameplayBindCollectibles {
        spec: GameplayBindSpec,
    },
    GameplayBindCheckpoints {
        spec: GameplayBindSpec,
    },
    GameplayBindInteractions {
        spec: GameplayBindSpec,
    },
    GameplayBindObjectiveFlow {
        spec: GameplayBindSpec,
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
pub struct GamePlayerSpec {
    pub name: String,
    pub scene: Option<String>,
    pub group: Option<String>,
    pub location: [f64; 3],
    pub rotation: [f64; 3],
    pub spawn_tag: Option<String>,
    pub create_camera: bool,
    pub camera_name: Option<String>,
    pub camera_location: [f64; 3],
    pub camera_rotation: [f64; 3],
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct GameCheckpointSpec {
    pub name: String,
    pub scene: Option<String>,
    pub group: Option<String>,
    pub checkpoint_id: String,
    pub order: u32,
    pub location: [f64; 3],
    pub rotation: [f64; 3],
    pub scale: [f64; 3],
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct GameInteractionSpec {
    pub name: String,
    pub kind: String,
    pub scene: Option<String>,
    pub group: Option<String>,
    pub interaction_id: Option<String>,
    pub target: Option<String>,
    pub action: Option<String>,
    pub prompt: Option<String>,
    pub location: [f64; 3],
    pub rotation: [f64; 3],
    pub scale: [f64; 3],
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct GameCollectiblesSpec {
    pub name_prefix: String,
    pub mesh: String,
    pub scene: Option<String>,
    pub group: Option<String>,
    pub origin: [f64; 3],
    pub rows: u32,
    pub columns: u32,
    pub spacing: [f64; 2],
    pub value: i32,
    pub rotation: [f64; 3],
    pub scale: [f64; 3],
    pub animation: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct GameObjectiveStepSpec {
    pub id: String,
    pub label: String,
    pub kind: String,
    pub location: [f64; 3],
    pub rotation: [f64; 3],
    pub scale: [f64; 3],
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct GameObjectiveFlowSpec {
    pub name_prefix: String,
    pub scene: Option<String>,
    pub group: Option<String>,
    pub steps: Vec<GameObjectiveStepSpec>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct GameplayCreateSystemSpec {
    pub name: String,
    pub scene: Option<String>,
    pub group: Option<String>,
    pub location: [f64; 3],
    pub tags: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct GameplayBindSpec {
    pub names: Vec<String>,
    pub tags: Vec<String>,
    pub manager_name: String,
    pub include_generated: bool,
    pub value: i32,
    pub destroy_on_collect: bool,
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

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct RoadNetworkSpec {
    pub name_prefix: String,
    pub scene: Option<String>,
    pub group: Option<String>,
    pub origin: [f64; 3],
    pub rows: u32,
    pub columns: u32,
    pub block_size: [f64; 2],
    pub road_width: f64,
    pub road_thickness: f64,
    pub road_mesh: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct GridPlacementSpec {
    pub name_prefix: String,
    pub mesh: String,
    pub scene: Option<String>,
    pub group: Option<String>,
    pub origin: [f64; 3],
    pub rows: u32,
    pub columns: u32,
    pub spacing: [f64; 2],
    pub rotation: [f64; 3],
    pub scale: [f64; 3],
    pub yaw_variation: f64,
    pub scale_variation: f64,
    pub seed: u32,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct CityBlockSpec {
    pub name_prefix: String,
    pub scene: Option<String>,
    pub group: Option<String>,
    pub origin: [f64; 3],
    pub size: [f64; 2],
    pub road_width: f64,
    pub sidewalk_width: f64,
    pub road_mesh: Option<String>,
    pub sidewalk_mesh: Option<String>,
    pub building_mesh: String,
    pub building_rows: u32,
    pub building_columns: u32,
    pub building_scale: [f64; 3],
    pub seed: u32,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DistrictSpec {
    pub name_prefix: String,
    pub preset: String,
    pub scene: Option<String>,
    pub group: Option<String>,
    pub origin: [f64; 3],
    pub blocks: [u32; 2],
    pub block_size: [f64; 2],
    pub road_width: f64,
    pub road_mesh: Option<String>,
    pub building_mesh: String,
    pub seed: u32,
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
    pub message: Option<String>,
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
pub struct SceneAssemblyResult {
    pub spawned: Vec<SpawnedActor>,
    pub count: usize,
    pub road_count: usize,
    pub sidewalk_count: usize,
    pub building_count: usize,
    pub prop_count: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct GameplayOperationResult {
    pub spawned: Vec<SpawnedActor>,
    pub count: usize,
    pub player_count: usize,
    pub checkpoint_count: usize,
    pub interaction_count: usize,
    pub collectible_count: usize,
    pub objective_count: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct GameplayBindingSummary {
    pub name: String,
    pub path: String,
    pub component: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct GameplayRuntimeOperationResult {
    pub manager: Option<SpawnedActor>,
    pub bindings: Vec<GameplayBindingSummary>,
    pub count: usize,
    pub collectible_count: usize,
    pub checkpoint_count: usize,
    pub interaction_count: usize,
    pub objective_count: usize,
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
    SceneAssembly(SceneAssemblyResult),
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
    GameplayOperation(GameplayOperationResult),
    GameplayRuntimeOperation(GameplayRuntimeOperationResult),
}
