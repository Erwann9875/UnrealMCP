use unreal_mcp_protocol::{
    ActorQuery, ActorSpawnSpec, AssetOperation, BlueprintComponentOperation, BlueprintOperation,
    Command, CommandResult, ErrorMode, IndexedError, LevelInfo, LevelList, LevelOperation,
    LightComponentSpec, LightSpec, LightSummary, LightingOperation, MaterialAppliedActor,
    MaterialApplyResult, MaterialAssignment, MaterialOperation, MaterialParameter,
    MaterialParameterOperation, ProceduralTextureOperation, ProtocolError, RequestEnvelope,
    ResponseEnvelope, ResponseMode, RuntimeAnimationOperation, RuntimeAnimationSpec, SpawnedActor,
    StaticMeshComponentSpec, TextureCreateSpec, Transform, WorldQueryResult,
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
