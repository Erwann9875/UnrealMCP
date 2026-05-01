use unreal_mcp_protocol::{
    ActorQuery, ActorSpawnSpec, AssetImportItem, AssetImportOperation, AssetImportResult,
    AssetImportSpec, AssetOperation, AssetValidateSpec, AssetValidation, AssetValidationResult,
    BlueprintComponentOperation, BlueprintOperation, CityBlockSpec, Command, CommandResult,
    DistrictSpec, ErrorMode, GameCheckpointSpec, GameCollectiblesSpec, GameInteractionSpec,
    GameObjectiveFlowSpec, GameObjectiveStepSpec, GamePlayerSpec, GameplayOperationResult,
    GameplayBindingSummary, GameplayBindSpec, GameplayCreateSystemSpec,
    GameplayRuntimeOperationResult,
    GeneratedBuildingSpec, GeneratedMeshOperation, GeneratedSignSpec, GridPlacementSpec,
    IndexedError, LandscapeCreateSpec, LandscapeHeightPatch, LandscapeLayerPaint,
    LandscapeOperation, LevelInfo, LevelList, LevelOperation, LightComponentSpec, LightSpec,
    LightSummary, LightingOperation, MaterialAppliedActor, MaterialApplyResult, MaterialAssignment,
    MaterialOperation, MaterialParameter, MaterialParameterOperation, PlacementSnapActor,
    PlacementSnapResult, PlacementSnapSpec, ProceduralTextureOperation, ProtocolError,
    RequestEnvelope, ResponseEnvelope, ResponseMode, RoadNetworkSpec, RuntimeAnimationOperation,
    RuntimeAnimationSpec, SceneAssemblyResult, SpawnedActor, StaticMeshCollisionSpec,
    StaticMeshComponentSpec, StaticMeshOperation, StaticMeshOperationResult, TextureCreateSpec,
    Transform, WorldQueryResult,
};

#[test]
fn request_envelope_keeps_bulk_commands_ordered() {
    let envelope = RequestEnvelope::new(
        7,
        ResponseMode::Handles,
        ErrorMode::Continue,
        vec![Command::Ping, Command::Capabilities],
    );

    assert_eq!(envelope.protocol_version, 1);
    assert_eq!(envelope.request_id, 7);
    assert_eq!(envelope.commands.len(), 2);
    assert!(matches!(envelope.commands[0], Command::Ping));
    assert!(matches!(envelope.commands[1], Command::Capabilities));
}

#[test]
fn response_envelope_can_return_compact_success() {
    let response = ResponseEnvelope::success(
        7,
        3,
        vec![CommandResult::Pong {
            bridge_version: "0.1.0".to_string(),
        }],
    );

    assert!(response.ok);
    assert_eq!(response.request_id, 7);
    assert_eq!(response.elapsed_ms, 3);
    assert_eq!(response.errors.len(), 0);
    assert_eq!(response.results.len(), 1);
}

use unreal_mcp_protocol::{
    decode_json_request, decode_json_response, decode_msgpack_request, decode_msgpack_response,
    encode_json_request, encode_json_response, encode_msgpack_request, encode_msgpack_response,
};

#[test]
fn msgpack_request_roundtrip_preserves_envelope() {
    let request = RequestEnvelope::new(
        42,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![Command::Ping],
    );

    let bytes = encode_msgpack_request(&request).expect("encode request");
    let decoded = decode_msgpack_request(&bytes).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn json_debug_request_roundtrip_preserves_envelope() {
    let request = RequestEnvelope::new(
        43,
        ResponseMode::Full,
        ErrorMode::Continue,
        vec![Command::Status],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn level_create_request_roundtrip_preserves_payload() {
    let request = RequestEnvelope::new(
        50,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![Command::LevelCreate {
            path: "/Game/MCP/Generated/L_Test".to_string(),
            open: true,
            save: false,
        }],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn world_bulk_spawn_request_roundtrip_preserves_payload() {
    let request = RequestEnvelope::new(
        51,
        ResponseMode::Handles,
        ErrorMode::Continue,
        vec![Command::WorldBulkSpawn {
            actors: vec![ActorSpawnSpec {
                name: "MCP_Test_Cube".to_string(),
                mesh: "/Engine/BasicShapes/Cube.Cube".to_string(),
                transform: Transform {
                    location: [100.0, 200.0, 300.0],
                    rotation: [0.0, 45.0, 0.0],
                    scale: [2.0, 2.0, 1.0],
                },
                scene: Some("test_scene".to_string()),
                group: Some("buildings".to_string()),
            }],
        }],
    );

    let bytes = encode_msgpack_request(&request).expect("encode request");
    let decoded = decode_msgpack_request(&bytes).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn world_query_response_roundtrip_preserves_payload() {
    let response = ResponseEnvelope::success(
        52,
        4,
        vec![CommandResult::WorldQuery(WorldQueryResult {
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
        })],
    );

    let text = encode_json_response(&response).expect("encode response");
    let decoded = decode_json_response(&text).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn material_requests_roundtrip_preserve_payloads() {
    let request = RequestEnvelope::new(
        54,
        ResponseMode::Handles,
        ErrorMode::Stop,
        vec![
            Command::AssetCreateFolder {
                path: "/Game/MCP/Materials".to_string(),
            },
            Command::MaterialCreate {
                path: "/Game/MCP/Materials/M_TestConcrete".to_string(),
                base_color: [0.45, 0.45, 0.42, 1.0],
                metallic: 0.0,
                roughness: 0.8,
                specular: 0.4,
                emissive_color: [0.0, 0.0, 0.0, 1.0],
            },
            Command::MaterialCreateProceduralTexture {
                spec: TextureCreateSpec {
                    path: "/Game/MCP/Materials/T_Checker".to_string(),
                    pattern: "checker".to_string(),
                    width: 64,
                    height: 64,
                    color_a: [0.1, 0.1, 0.1, 1.0],
                    color_b: [0.8, 0.8, 0.8, 1.0],
                    checker_size: 8,
                },
            },
            Command::MaterialBulkApply {
                assignments: vec![MaterialAssignment {
                    material: "/Game/MCP/Materials/M_TestConcrete".to_string(),
                    names: vec![],
                    tags: vec!["mcp.group:buildings".to_string()],
                    slot: 0,
                }],
            },
            Command::WorldBulkSetMaterials {
                names: vec!["Tower_A".to_string()],
                tags: vec![],
                material: "/Game/MCP/Materials/M_TestConcrete".to_string(),
                slot: 0,
            },
        ],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn material_results_roundtrip_preserve_payloads() {
    let response = ResponseEnvelope::success(
        55,
        7,
        vec![
            CommandResult::AssetOperation(AssetOperation {
                path: "/Game/MCP/Materials".to_string(),
                created: true,
            }),
            CommandResult::MaterialOperation(MaterialOperation {
                path: "/Game/MCP/Materials/M_TestConcrete".to_string(),
                parent: None,
                created: true,
            }),
            CommandResult::ProceduralTextureOperation(ProceduralTextureOperation {
                path: "/Game/MCP/Materials/T_Checker".to_string(),
                width: 64,
                height: 64,
                created: true,
            }),
            CommandResult::MaterialParameterOperation(MaterialParameterOperation {
                path: "/Game/MCP/Materials/MI_Test".to_string(),
                scalar_count: 1,
                vector_count: 1,
                texture_count: 0,
            }),
            CommandResult::MaterialApply(MaterialApplyResult {
                applied: vec![MaterialAppliedActor {
                    name: "Tower_A".to_string(),
                    path: "PersistentLevel.Tower_A".to_string(),
                    material: "/Game/MCP/Materials/M_TestConcrete".to_string(),
                    slot: 0,
                }],
                count: 1,
            }),
        ],
    );

    let bytes = encode_msgpack_response(&response).expect("encode response");
    let decoded = decode_msgpack_response(&bytes).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn material_instance_and_parameter_requests_roundtrip() {
    let request = RequestEnvelope::new(
        56,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![
            Command::MaterialCreateInstance {
                path: "/Game/MCP/Materials/MI_Test".to_string(),
                parent: "/Game/MCP/Materials/M_TestConcrete".to_string(),
                scalar_parameters: vec![MaterialParameter::scalar("Roughness", 0.35)],
                vector_parameters: vec![MaterialParameter::vector("Tint", [0.2, 0.4, 1.0, 1.0])],
                texture_parameters: vec![MaterialParameter::texture(
                    "Albedo",
                    "/Game/MCP/Materials/T_Checker",
                )],
            },
            Command::MaterialSetParameters {
                path: "/Game/MCP/Materials/MI_Test".to_string(),
                scalar_parameters: vec![MaterialParameter::scalar("Intensity", 10.0)],
                vector_parameters: vec![],
                texture_parameters: vec![],
            },
        ],
    );

    let bytes = encode_msgpack_request(&request).expect("encode request");
    let decoded = decode_msgpack_request(&bytes).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn lighting_requests_roundtrip_preserve_payloads() {
    let request = RequestEnvelope::new(
        57,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![
            Command::LightingSetNightScene {
                moon_rotation: [-35.0, -25.0, 0.0],
                moon_intensity: 0.12,
                moon_color: [0.55, 0.65, 1.0, 1.0],
                sky_intensity: 0.05,
                fog_density: 0.01,
                exposure_compensation: -0.5,
            },
            Command::LightingSetSky {
                sky_intensity: 0.08,
                lower_hemisphere_color: [0.01, 0.012, 0.018, 1.0],
            },
            Command::LightingSetFog {
                density: 0.015,
                height_falloff: 0.2,
                color: [0.08, 0.1, 0.16, 1.0],
                start_distance: 100.0,
            },
            Command::LightingSetPostProcess {
                exposure_compensation: -0.25,
                min_brightness: 0.2,
                max_brightness: 1.0,
                bloom_intensity: 0.6,
            },
            Command::LightingBulkSetLights {
                lights: vec![LightSpec {
                    name: "MCP_StreetLight_001".to_string(),
                    kind: "point".to_string(),
                    transform: Transform {
                        location: [100.0, 200.0, 360.0],
                        rotation: [0.0, 0.0, 0.0],
                        scale: [1.0, 1.0, 1.0],
                    },
                    color: [1.0, 0.82, 0.55, 1.0],
                    intensity: 5000.0,
                    attenuation_radius: 1000.0,
                    source_radius: 24.0,
                    source_width: 64.0,
                    source_height: 32.0,
                    tags: vec!["mcp.scene:lighting_smoke".to_string()],
                }],
            },
            Command::LightingSetTimeOfDay {
                sun_rotation: [-10.0, 110.0, 0.0],
                sun_intensity: 1.0,
                sun_color: [1.0, 0.93, 0.82, 1.0],
            },
        ],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn lighting_results_roundtrip_preserve_payloads() {
    let response = ResponseEnvelope::success(
        58,
        9,
        vec![
            CommandResult::LightingOperation(LightingOperation {
                changed: vec![
                    "MCP_MoonLight".to_string(),
                    "MCP_SkyLight".to_string(),
                    "MCP_NightFog".to_string(),
                ],
                count: 3,
            }),
            CommandResult::LightingBulkSetLights {
                lights: vec![LightSummary {
                    name: "MCP_StreetLight_001".to_string(),
                    path: "PersistentLevel.MCP_StreetLight_001".to_string(),
                    kind: "point".to_string(),
                }],
                count: 1,
            },
        ],
    );

    let bytes = encode_msgpack_response(&response).expect("encode response");
    let decoded = decode_msgpack_response(&bytes).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn runtime_blueprint_requests_roundtrip_preserve_payloads() {
    let request = RequestEnvelope::new(
        59,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![
            Command::BlueprintCreateActor {
                path: "/Game/MCP/Blueprints/BP_LED_Tower".to_string(),
                parent_class: "Actor".to_string(),
            },
            Command::BlueprintAddStaticMeshComponent {
                blueprint: "/Game/MCP/Blueprints/BP_LED_Tower".to_string(),
                component: StaticMeshComponentSpec {
                    name: "BuildingMesh".to_string(),
                    mesh: "/Engine/BasicShapes/Cube.Cube".to_string(),
                    material: Some("/Game/MCP/Materials/MI_LED".to_string()),
                    transform: Transform {
                        location: [0.0, 0.0, 200.0],
                        rotation: [0.0, 0.0, 0.0],
                        scale: [4.0, 4.0, 8.0],
                    },
                },
            },
            Command::BlueprintAddLightComponent {
                blueprint: "/Game/MCP/Blueprints/BP_LED_Tower".to_string(),
                component: LightComponentSpec {
                    name: "WindowGlow".to_string(),
                    kind: "rect".to_string(),
                    transform: Transform {
                        location: [0.0, -210.0, 500.0],
                        rotation: [0.0, 180.0, 0.0],
                        scale: [1.0, 1.0, 1.0],
                    },
                    color: [0.0, 0.85, 1.0, 1.0],
                    intensity: 8000.0,
                    attenuation_radius: 1200.0,
                    source_radius: 12.0,
                    source_width: 320.0,
                    source_height: 160.0,
                },
            },
            Command::BlueprintCompile {
                path: "/Game/MCP/Blueprints/BP_LED_Tower".to_string(),
                save: false,
            },
        ],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn runtime_animation_requests_roundtrip_preserve_payloads() {
    let led = RuntimeAnimationSpec {
        path: "/Game/MCP/Runtime/DA_LED_Pulse".to_string(),
        target_component: Some("BuildingMesh".to_string()),
        parameter_name: "EmissiveColor".to_string(),
        color_a: [0.0, 0.0, 0.0, 1.0],
        color_b: [0.0, 0.85, 1.0, 1.0],
        from_scalar: 0.0,
        to_scalar: 10.0,
        speed: 2.0,
        amplitude: 100.0,
        axis: [0.0, 0.0, 1.0],
        base_intensity: 5000.0,
        phase_offset: 0.25,
    };
    let moving_light = RuntimeAnimationSpec {
        path: "/Game/MCP/Runtime/DA_WindowGlow_Move".to_string(),
        target_component: Some("WindowGlow".to_string()),
        parameter_name: "Intensity".to_string(),
        color_a: [1.0, 0.82, 0.55, 1.0],
        color_b: [0.0, 0.85, 1.0, 1.0],
        from_scalar: 0.0,
        to_scalar: 1.0,
        speed: 1.5,
        amplitude: 75.0,
        axis: [0.0, 0.0, 1.0],
        base_intensity: 8000.0,
        phase_offset: 0.0,
    };

    let request = RequestEnvelope::new(
        60,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![
            Command::RuntimeCreateLedAnimation { spec: led },
            Command::RuntimeCreateMovingLightAnimation { spec: moving_light },
            Command::RuntimeCreateMaterialParameterAnimation {
                spec: RuntimeAnimationSpec {
                    path: "/Game/MCP/Runtime/DA_Material_Pulse".to_string(),
                    target_component: None,
                    parameter_name: "GlowAmount".to_string(),
                    color_a: [0.0, 0.0, 0.0, 1.0],
                    color_b: [1.0, 1.0, 1.0, 1.0],
                    from_scalar: 0.0,
                    to_scalar: 5.0,
                    speed: 3.0,
                    amplitude: 100.0,
                    axis: [0.0, 0.0, 1.0],
                    base_intensity: 5000.0,
                    phase_offset: 0.0,
                },
            },
            Command::RuntimeAttachAnimationToActor {
                names: vec!["MCP_LED_Tower_001".to_string()],
                tags: vec!["mcp.group:buildings".to_string()],
                blueprint: Some("/Game/MCP/Blueprints/BP_LED_Tower".to_string()),
                animations: vec![
                    "/Game/MCP/Runtime/DA_LED_Pulse".to_string(),
                    "/Game/MCP/Runtime/DA_WindowGlow_Move".to_string(),
                ],
            },
        ],
    );

    let bytes = encode_msgpack_request(&request).expect("encode request");
    let decoded = decode_msgpack_request(&bytes).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn runtime_blueprint_results_roundtrip_preserve_payloads() {
    let response = ResponseEnvelope::success(
        61,
        11,
        vec![
            CommandResult::BlueprintOperation(BlueprintOperation {
                path: "/Game/MCP/Blueprints/BP_LED_Tower".to_string(),
                created: true,
                compiled: false,
            }),
            CommandResult::BlueprintComponentOperation(BlueprintComponentOperation {
                blueprint: "/Game/MCP/Blueprints/BP_LED_Tower".to_string(),
                components: vec!["BuildingMesh".to_string(), "WindowGlow".to_string()],
                count: 2,
            }),
            CommandResult::RuntimeAnimationOperation(RuntimeAnimationOperation {
                path: Some("/Game/MCP/Runtime/DA_LED_Pulse".to_string()),
                attached: vec!["MCP_LED_Tower_001".to_string()],
                count: 1,
            }),
        ],
    );

    let text = encode_json_response(&response).expect("encode response");
    let decoded = decode_json_response(&text).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn landscape_grounding_requests_roundtrip_preserve_payloads() {
    let request = RequestEnvelope::new(
        62,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![
            Command::LandscapeCreate {
                spec: LandscapeCreateSpec {
                    name: "MCP_Landscape".to_string(),
                    component_count: [4, 4],
                    section_size: 63,
                    sections_per_component: 1,
                    location: [0.0, 0.0, 0.0],
                    scale: [100.0, 100.0, 100.0],
                    material: Some("/Game/MCP/Materials/M_Ground".to_string()),
                    tags: vec!["mcp.scene:grounding".to_string()],
                },
            },
            Command::LandscapeSetHeightfield {
                patch: LandscapeHeightPatch {
                    name: "MCP_Landscape".to_string(),
                    width: 253,
                    height: 253,
                    base_height: 0.0,
                    amplitude: 1200.0,
                    frequency: 2.0,
                    seed: 1234,
                    city_pad_radius: 6500.0,
                    city_pad_falloff: 1500.0,
                    samples: vec![0.0, 0.25, -0.25, 0.5],
                },
            },
            Command::LandscapePaintLayers {
                paint: LandscapeLayerPaint {
                    name: "MCP_Landscape".to_string(),
                    material: Some("/Game/MCP/Materials/M_Ground".to_string()),
                    layers: vec!["Concrete".to_string(), "Hills".to_string()],
                },
            },
            Command::PlacementBulkSnapToGround {
                spec: PlacementSnapSpec {
                    names: vec!["MCP_Test_Cube".to_string()],
                    tags: vec!["mcp.group:buildings".to_string()],
                    include_generated: true,
                    trace_distance: 20000.0,
                    offset_z: 50.0,
                },
            },
        ],
    );

    let bytes = encode_msgpack_request(&request).expect("encode request");
    let decoded = decode_msgpack_request(&bytes).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn landscape_grounding_results_roundtrip_preserve_payloads() {
    let response = ResponseEnvelope::success(
        63,
        12,
        vec![
            CommandResult::LandscapeOperation(LandscapeOperation {
                name: "MCP_Landscape".to_string(),
                path: "PersistentLevel.MCP_Landscape".to_string(),
                component_count: [4, 4],
                vertex_count: [253, 253],
                changed: vec!["created".to_string(), "heightfield".to_string()],
            }),
            CommandResult::PlacementSnap(PlacementSnapResult {
                actors: vec![PlacementSnapActor {
                    name: "MCP_Test_Cube".to_string(),
                    path: "PersistentLevel.MCP_Test_Cube".to_string(),
                    old_location: [0.0, 0.0, 500.0],
                    new_location: [0.0, 0.0, 50.0],
                }],
                count: 1,
            }),
        ],
    );

    let text = encode_json_response(&response).expect("encode response");
    let decoded = decode_json_response(&text).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn asset_import_mesh_requests_roundtrip_preserve_payloads() {
    let request = RequestEnvelope::new(
        64,
        ResponseMode::Summary,
        ErrorMode::Continue,
        vec![
            Command::AssetImportTexture {
                spec: AssetImportSpec {
                    source_file: "C:/Temp/mcp_texture.png".to_string(),
                    destination_path: "/Game/MCP/Assets/T_Test".to_string(),
                    replace_existing: true,
                    save: false,
                    srgb: Some(true),
                    generate_collision: None,
                },
            },
            Command::AssetImportStaticMesh {
                spec: AssetImportSpec {
                    source_file: "C:/Temp/mcp_mesh.fbx".to_string(),
                    destination_path: "/Game/MCP/Assets/SM_Test".to_string(),
                    replace_existing: true,
                    save: false,
                    srgb: None,
                    generate_collision: Some(true),
                },
            },
            Command::AssetBulkImport {
                items: vec![
                    AssetImportItem {
                        kind: "texture".to_string(),
                        source_file: "C:/Temp/a.png".to_string(),
                        destination_path: "/Game/MCP/Assets/T_A".to_string(),
                        replace_existing: true,
                        save: false,
                        srgb: Some(false),
                        generate_collision: None,
                    },
                    AssetImportItem {
                        kind: "static_mesh".to_string(),
                        source_file: "C:/Temp/a.fbx".to_string(),
                        destination_path: "/Game/MCP/Assets/SM_A".to_string(),
                        replace_existing: false,
                        save: true,
                        srgb: None,
                        generate_collision: Some(true),
                    },
                ],
            },
            Command::AssetValidate {
                spec: AssetValidateSpec {
                    paths: vec![
                        "/Game/MCP/Assets/T_A".to_string(),
                        "/Game/MCP/Assets/SM_A".to_string(),
                    ],
                },
            },
            Command::MeshCreateBuilding {
                spec: GeneratedBuildingSpec {
                    path: "/Game/MCP/Meshes/SM_TestBuilding".to_string(),
                    width: 800.0,
                    depth: 600.0,
                    height: 2400.0,
                    floors: 12,
                    window_rows: 12,
                    window_columns: 6,
                    material: Some("/Game/MCP/Materials/M_Glass".to_string()),
                },
            },
            Command::MeshCreateSign {
                spec: GeneratedSignSpec {
                    path: "/Game/MCP/Meshes/SM_HollywoodSignPanel".to_string(),
                    width: 900.0,
                    height: 240.0,
                    depth: 30.0,
                    text: Some("HOLLYWOOD".to_string()),
                    material: Some("/Game/MCP/Materials/M_WhitePaint".to_string()),
                },
            },
            Command::StaticMeshSetCollision {
                spec: StaticMeshCollisionSpec {
                    paths: vec!["/Game/MCP/Meshes/SM_TestBuilding".to_string()],
                    collision_trace: "use_simple_as_complex".to_string(),
                    simple_collision: true,
                    rebuild: true,
                },
            },
        ],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn asset_import_mesh_results_roundtrip_preserve_payloads() {
    let response = ResponseEnvelope::success(
        65,
        13,
        vec![
            CommandResult::AssetImport(AssetImportResult {
                assets: vec![AssetImportOperation {
                    source_file: "C:/Temp/mcp_texture.png".to_string(),
                    path: "/Game/MCP/Assets/T_Test".to_string(),
                    class_name: "Texture2D".to_string(),
                    imported: true,
                    message: None,
                }],
                count: 1,
            }),
            CommandResult::AssetValidation(AssetValidationResult {
                assets: vec![AssetValidation {
                    path: "/Game/MCP/Assets/T_Test".to_string(),
                    exists: true,
                    class_name: Some("Texture2D".to_string()),
                }],
                count: 1,
            }),
            CommandResult::GeneratedMesh(GeneratedMeshOperation {
                path: "/Game/MCP/Meshes/SM_TestBuilding".to_string(),
                created: true,
                vertex_count: 96,
                triangle_count: 48,
            }),
            CommandResult::StaticMeshOperation(StaticMeshOperationResult {
                meshes: vec![StaticMeshOperation {
                    path: "/Game/MCP/Meshes/SM_TestBuilding".to_string(),
                    changed: true,
                    collision_trace: "use_simple_as_complex".to_string(),
                }],
                count: 1,
            }),
        ],
    );

    let bytes = encode_msgpack_response(&response).expect("encode response");
    let decoded = decode_msgpack_response(&bytes).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn scene_assembly_requests_roundtrip_preserve_payloads() {
    let request = RequestEnvelope::new(
        66,
        ResponseMode::Summary,
        ErrorMode::Continue,
        vec![
            Command::RoadCreateNetwork {
                spec: RoadNetworkSpec {
                    name_prefix: "MCP_Road".to_string(),
                    scene: Some("la".to_string()),
                    group: Some("downtown_roads".to_string()),
                    origin: [0.0, 0.0, 0.0],
                    rows: 3,
                    columns: 4,
                    block_size: [2400.0, 1800.0],
                    road_width: 320.0,
                    road_thickness: 20.0,
                    road_mesh: Some("/Engine/BasicShapes/Cube.Cube".to_string()),
                },
            },
            Command::SceneBulkPlaceOnGrid {
                spec: GridPlacementSpec {
                    name_prefix: "MCP_Tower".to_string(),
                    mesh: "/Game/MCP/Meshes/SM_Tower".to_string(),
                    scene: Some("la".to_string()),
                    group: Some("downtown_buildings".to_string()),
                    origin: [0.0, 0.0, 0.0],
                    rows: 2,
                    columns: 3,
                    spacing: [500.0, 600.0],
                    rotation: [0.0, 0.0, 0.0],
                    scale: [1.0, 1.0, 1.0],
                    yaw_variation: 12.0,
                    scale_variation: 0.2,
                    seed: 42,
                },
            },
            Command::SceneCreateCityBlock {
                spec: CityBlockSpec {
                    name_prefix: "MCP_Block".to_string(),
                    scene: Some("la".to_string()),
                    group: Some("block_a".to_string()),
                    origin: [0.0, 0.0, 0.0],
                    size: [2400.0, 1800.0],
                    road_width: 320.0,
                    sidewalk_width: 180.0,
                    road_mesh: Some("/Engine/BasicShapes/Cube.Cube".to_string()),
                    sidewalk_mesh: Some("/Engine/BasicShapes/Cube.Cube".to_string()),
                    building_mesh: "/Game/MCP/Meshes/SM_Tower".to_string(),
                    building_rows: 2,
                    building_columns: 3,
                    building_scale: [2.0, 2.0, 8.0],
                    seed: 7,
                },
            },
            Command::SceneCreateDistrict {
                spec: DistrictSpec {
                    name_prefix: "MCP_Downtown".to_string(),
                    preset: "downtown".to_string(),
                    scene: Some("la".to_string()),
                    group: Some("downtown".to_string()),
                    origin: [0.0, 0.0, 0.0],
                    blocks: [3, 2],
                    block_size: [2400.0, 1800.0],
                    road_width: 320.0,
                    road_mesh: Some("/Engine/BasicShapes/Cube.Cube".to_string()),
                    building_mesh: "/Game/MCP/Meshes/SM_Tower".to_string(),
                    seed: 99,
                },
            },
        ],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn scene_assembly_results_roundtrip_preserve_payloads() {
    let response = ResponseEnvelope::success(
        67,
        9,
        vec![CommandResult::SceneAssembly(SceneAssemblyResult {
            spawned: vec![
                SpawnedActor {
                    name: "MCP_Road_0".to_string(),
                    path: "PersistentLevel.MCP_Road_0".to_string(),
                },
                SpawnedActor {
                    name: "MCP_Tower_0_0".to_string(),
                    path: "PersistentLevel.MCP_Tower_0_0".to_string(),
                },
            ],
            count: 2,
            road_count: 1,
            sidewalk_count: 0,
            building_count: 1,
            prop_count: 0,
        })],
    );

    let bytes = encode_msgpack_response(&response).expect("encode response");
    let decoded = decode_msgpack_response(&bytes).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn gameplay_requests_roundtrip_preserve_payloads() {
    let request = RequestEnvelope::new(
        68,
        ResponseMode::Summary,
        ErrorMode::Continue,
        vec![
            Command::GameCreatePlayer {
                spec: GamePlayerSpec {
                    name: "MCP_PlayerStart".to_string(),
                    scene: Some("prototype".to_string()),
                    group: Some("gameplay".to_string()),
                    location: [0.0, 0.0, 120.0],
                    rotation: [0.0, 90.0, 0.0],
                    spawn_tag: Some("default".to_string()),
                    create_camera: true,
                    camera_name: Some("MCP_PlayerCamera".to_string()),
                    camera_location: [-400.0, 0.0, 260.0],
                    camera_rotation: [-10.0, 0.0, 0.0],
                },
            },
            Command::GameCreateCheckpoint {
                spec: GameCheckpointSpec {
                    name: "MCP_Checkpoint_A".to_string(),
                    scene: Some("prototype".to_string()),
                    group: Some("checkpoints".to_string()),
                    checkpoint_id: "cp_a".to_string(),
                    order: 1,
                    location: [1000.0, 0.0, 80.0],
                    rotation: [0.0, 0.0, 0.0],
                    scale: [2.0, 2.0, 0.25],
                },
            },
            Command::GameCreateInteraction {
                spec: GameInteractionSpec {
                    name: "MCP_DoorSwitch".to_string(),
                    kind: "button".to_string(),
                    scene: Some("prototype".to_string()),
                    group: Some("interactions".to_string()),
                    interaction_id: Some("door_switch".to_string()),
                    target: Some("MCP_Door_A".to_string()),
                    action: Some("open".to_string()),
                    prompt: Some("Open".to_string()),
                    location: [500.0, 300.0, 120.0],
                    rotation: [0.0, 0.0, 0.0],
                    scale: [0.5, 0.5, 0.5],
                },
            },
            Command::GameCreateCollectibles {
                spec: GameCollectiblesSpec {
                    name_prefix: "MCP_Coin".to_string(),
                    mesh: "/Engine/BasicShapes/Cube.Cube".to_string(),
                    scene: Some("prototype".to_string()),
                    group: Some("collectibles".to_string()),
                    origin: [0.0, 1000.0, 120.0],
                    rows: 2,
                    columns: 3,
                    spacing: [180.0, 160.0],
                    value: 10,
                    rotation: [0.0, 0.0, 45.0],
                    scale: [0.2, 0.2, 0.2],
                    animation: Some("/Game/MCP/Runtime/DA_CoinBob".to_string()),
                },
            },
            Command::GameCreateObjectiveFlow {
                spec: GameObjectiveFlowSpec {
                    name_prefix: "MCP_Objective".to_string(),
                    scene: Some("prototype".to_string()),
                    group: Some("objectives".to_string()),
                    steps: vec![
                        GameObjectiveStepSpec {
                            id: "intro".to_string(),
                            label: "Reach the street".to_string(),
                            kind: "location".to_string(),
                            location: [0.0, 0.0, 100.0],
                            rotation: [0.0, 0.0, 0.0],
                            scale: [1.0, 1.0, 1.0],
                        },
                        GameObjectiveStepSpec {
                            id: "exit".to_string(),
                            label: "Find the exit".to_string(),
                            kind: "interaction".to_string(),
                            location: [1200.0, 0.0, 100.0],
                            rotation: [0.0, 0.0, 0.0],
                            scale: [1.0, 1.0, 1.0],
                        },
                    ],
                },
            },
        ],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn gameplay_results_roundtrip_preserve_payloads() {
    let response = ResponseEnvelope::success(
        69,
        8,
        vec![CommandResult::GameplayOperation(GameplayOperationResult {
            spawned: vec![
                SpawnedActor {
                    name: "MCP_PlayerStart".to_string(),
                    path: "PersistentLevel.MCP_PlayerStart".to_string(),
                },
                SpawnedActor {
                    name: "MCP_Coin_0_0".to_string(),
                    path: "PersistentLevel.MCP_Coin_0_0".to_string(),
                },
            ],
            count: 2,
            player_count: 1,
            checkpoint_count: 0,
            interaction_count: 0,
            collectible_count: 1,
            objective_count: 0,
        })],
    );

    let bytes = encode_msgpack_response(&response).expect("encode response");
    let decoded = decode_msgpack_response(&bytes).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn gameplay_runtime_requests_roundtrip_preserve_payloads() {
    let request = RequestEnvelope::new(
        70,
        ResponseMode::Summary,
        ErrorMode::Continue,
        vec![
            Command::GameplayCreateSystem {
                spec: GameplayCreateSystemSpec {
                    name: "MCP_GameplayManager".to_string(),
                    scene: Some("prototype".to_string()),
                    group: Some("runtime".to_string()),
                    location: [0.0, 0.0, 100.0],
                    tags: vec!["mcp.scene:prototype".to_string()],
                },
            },
            Command::GameplayBindCollectibles {
                spec: GameplayBindSpec {
                    names: vec!["MCP_Coin_000_000".to_string()],
                    tags: vec!["mcp.gameplay_actor:collectible".to_string()],
                    manager_name: "MCP_GameplayManager".to_string(),
                    include_generated: true,
                    value: 10,
                    destroy_on_collect: true,
                },
            },
            Command::GameplayBindCheckpoints {
                spec: GameplayBindSpec {
                    names: vec![],
                    tags: vec!["mcp.gameplay_actor:checkpoint".to_string()],
                    manager_name: "MCP_GameplayManager".to_string(),
                    include_generated: true,
                    value: 0,
                    destroy_on_collect: false,
                },
            },
            Command::GameplayBindInteractions {
                spec: GameplayBindSpec {
                    names: vec![],
                    tags: vec!["mcp.gameplay_actor:interaction".to_string()],
                    manager_name: "MCP_GameplayManager".to_string(),
                    include_generated: true,
                    value: 0,
                    destroy_on_collect: false,
                },
            },
            Command::GameplayBindObjectiveFlow {
                spec: GameplayBindSpec {
                    names: vec![],
                    tags: vec!["mcp.gameplay_actor:objective".to_string()],
                    manager_name: "MCP_GameplayManager".to_string(),
                    include_generated: true,
                    value: 0,
                    destroy_on_collect: false,
                },
            },
        ],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn gameplay_runtime_results_roundtrip_preserve_payloads() {
    let response = ResponseEnvelope::success(
        71,
        5,
        vec![CommandResult::GameplayRuntimeOperation(
            GameplayRuntimeOperationResult {
                manager: Some(SpawnedActor {
                    name: "MCP_GameplayManager".to_string(),
                    path: "PersistentLevel.MCP_GameplayManager".to_string(),
                }),
                bindings: vec![GameplayBindingSummary {
                    name: "MCP_Coin_000_000".to_string(),
                    path: "PersistentLevel.MCP_Coin_000_000".to_string(),
                    component: "MCP_CollectibleRuntime".to_string(),
                }],
                count: 1,
                collectible_count: 1,
                checkpoint_count: 0,
                interaction_count: 0,
                objective_count: 0,
            },
        )],
    );

    let bytes = encode_msgpack_response(&response).expect("encode response");
    let decoded = decode_msgpack_response(&bytes).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn level_and_spawn_results_roundtrip() {
    let response = ResponseEnvelope::success(
        53,
        5,
        vec![
            CommandResult::LevelList(LevelList {
                levels: vec![LevelInfo {
                    path: "/Game/MCP/Generated/L_Test".to_string(),
                    name: "L_Test".to_string(),
                }],
            }),
            CommandResult::LevelOperation(LevelOperation {
                path: "/Game/MCP/Generated/L_Test".to_string(),
                opened: true,
                saved: true,
            }),
            CommandResult::WorldBulkSpawn {
                spawned: vec![SpawnedActor {
                    name: "MCP_Test_Cube".to_string(),
                    path: "PersistentLevel.MCP_Test_Cube".to_string(),
                }],
                count: 1,
            },
        ],
    );

    let bytes = encode_msgpack_response(&response).expect("encode response");
    let decoded = decode_msgpack_response(&bytes).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn msgpack_response_roundtrip_preserves_success_envelope() {
    let response = ResponseEnvelope::success(
        44,
        5,
        vec![CommandResult::Pong {
            bridge_version: "0.1.0".to_string(),
        }],
    );

    let bytes = encode_msgpack_response(&response).expect("encode response");
    let decoded = decode_msgpack_response(&bytes).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn json_debug_response_roundtrip_preserves_failure_envelope() {
    let response = ResponseEnvelope::failure(
        45,
        8,
        vec![IndexedError {
            command_index: 2,
            item_index: Some(3),
            code: "bridge_error".to_string(),
            message: "bridge unavailable".to_string(),
        }],
    );

    let text = encode_json_response(&response).expect("encode response");
    let decoded = decode_json_response(&text).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn decoders_reject_unsupported_protocol_version() {
    let expected = 1;
    let actual = 999;

    let mut request = RequestEnvelope::new(
        46,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![Command::Ping],
    );
    request.protocol_version = actual;

    let request_json = encode_json_request(&request).expect("encode request");
    assert_unsupported_version(decode_json_request(&request_json), actual, expected);

    let request_msgpack = encode_msgpack_request(&request).expect("encode request");
    assert_unsupported_version(decode_msgpack_request(&request_msgpack), actual, expected);

    let mut response = ResponseEnvelope::success(
        46,
        13,
        vec![CommandResult::Pong {
            bridge_version: "0.1.0".to_string(),
        }],
    );
    response.protocol_version = actual;

    let response_json = encode_json_response(&response).expect("encode response");
    assert_unsupported_version(decode_json_response(&response_json), actual, expected);

    let response_msgpack = encode_msgpack_response(&response).expect("encode response");
    assert_unsupported_version(decode_msgpack_response(&response_msgpack), actual, expected);
}

fn assert_unsupported_version<T>(result: Result<T, ProtocolError>, actual: u16, expected: u16) {
    match result {
        Err(ProtocolError::UnsupportedProtocolVersion {
            actual: decoded,
            expected: supported,
        }) => {
            assert_eq!(decoded, actual);
            assert_eq!(supported, expected);
        }
        Err(error) => panic!("expected unsupported protocol version, got {error}"),
        Ok(_) => panic!("expected unsupported protocol version"),
    }
}
