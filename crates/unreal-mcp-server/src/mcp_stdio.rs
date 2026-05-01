use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncRead, AsyncWrite, AsyncWriteExt, BufReader, BufWriter};

use crate::{ConnectionTools, ToolResponse};

const MCP_PROTOCOL_VERSION: &str = "2025-03-26";
const SERVER_NAME: &str = "unreal-mcp-server";
const SERVER_VERSION: &str = "0.1.0";

pub async fn run_stdio_server<R, W>(
    reader: R,
    writer: W,
    tools: ConnectionTools,
) -> anyhow::Result<()>
where
    R: AsyncRead + Unpin,
    W: AsyncWrite + Unpin,
{
    let mut lines = BufReader::new(reader).lines();
    let mut writer = BufWriter::new(writer);

    while let Some(line) = lines.next_line().await? {
        if line.trim().is_empty() {
            continue;
        }

        let response = handle_line(&tools, &line).await;
        if let Some(response) = response {
            writer.write_all(response.to_string().as_bytes()).await?;
            writer.write_all(b"\n").await?;
            writer.flush().await?;
        }
    }

    Ok(())
}

async fn handle_line(tools: &ConnectionTools, line: &str) -> Option<Value> {
    let request = match serde_json::from_str::<Value>(line) {
        Ok(request) => request,
        Err(error) => {
            return Some(json_rpc_error(
                Value::Null,
                -32700,
                format!("Parse error: {error}"),
            ));
        }
    };

    let id = request.get("id").cloned()?;

    let Some(method) = request.get("method").and_then(Value::as_str) else {
        return Some(json_rpc_error(
            id,
            -32600,
            "Invalid request: missing method".to_string(),
        ));
    };

    Some(match method {
        "initialize" => initialize_response(id, &request),
        "tools/list" => tools_list_response(id),
        "tools/call" => tools_call_response(id, request.get("params"), tools).await,
        _ => json_rpc_error(id, -32601, format!("Method not found: {method}")),
    })
}

fn initialize_response(id: Value, request: &Value) -> Value {
    let requested_version = request
        .get("params")
        .and_then(|params| params.get("protocolVersion"))
        .and_then(Value::as_str)
        .unwrap_or(MCP_PROTOCOL_VERSION);
    let protocol_version = if requested_version == MCP_PROTOCOL_VERSION {
        requested_version
    } else {
        MCP_PROTOCOL_VERSION
    };

    json!({
        "jsonrpc": "2.0",
        "id": id,
        "result": {
            "protocolVersion": protocol_version,
            "capabilities": {
                "tools": {
                    "listChanged": false
                }
            },
            "serverInfo": {
                "name": SERVER_NAME,
                "version": SERVER_VERSION
            }
        }
    })
}

fn tools_list_response(id: Value) -> Value {
    json!({
        "jsonrpc": "2.0",
        "id": id,
        "result": {
            "tools": [
                tool_definition(
                    "connection.ping",
                    "Check whether the Unreal bridge responds.",
                    empty_schema()
                ),
                tool_definition(
                    "connection.status",
                    "Return compact Unreal bridge connection status.",
                    empty_schema()
                ),
                tool_definition(
                    "connection.capabilities",
                    "Return bridge-supported command names.",
                    empty_schema()
                ),
                tool_definition(
                    "level.create",
                    "Create a new Unreal level asset.",
                    level_create_schema()
                ),
                tool_definition(
                    "level.open",
                    "Open an Unreal level asset.",
                    level_path_schema()
                ),
                tool_definition(
                    "level.save",
                    "Save the current or specified Unreal level.",
                    level_save_schema()
                ),
                tool_definition(
                    "level.list",
                    "List project level assets.",
                    empty_schema()
                ),
                tool_definition(
                    "world.bulk_spawn",
                    "Spawn many static mesh actors in one bridge request.",
                    world_bulk_spawn_schema()
                ),
                tool_definition(
                    "world.bulk_delete",
                    "Delete actors by name or tag in one bridge request.",
                    world_bulk_delete_schema()
                ),
                tool_definition(
                    "world.query",
                    "Query actors by name or tag.",
                    world_query_schema()
                ),
                tool_definition(
                    "world.snapshot",
                    "Write a compact world snapshot to disk.",
                    world_snapshot_schema()
                ),
                tool_definition(
                    "asset.create_folder",
                    "Create a Content Browser folder.",
                    path_only_schema()
                ),
                tool_definition(
                    "asset.import_texture",
                    "Import one local texture file into a Content Browser asset.",
                    asset_import_schema()
                ),
                tool_definition(
                    "asset.import_static_mesh",
                    "Import one local static mesh file into a Content Browser asset.",
                    asset_import_schema()
                ),
                tool_definition(
                    "asset.bulk_import",
                    "Import many texture or static mesh assets in one bridge request.",
                    asset_bulk_import_schema()
                ),
                tool_definition(
                    "asset.validate",
                    "Validate Unreal asset paths and return compact class data.",
                    asset_validate_schema()
                ),
                tool_definition(
                    "mesh.create_building",
                    "Create a reusable generated building static mesh asset.",
                    mesh_create_building_schema()
                ),
                tool_definition(
                    "mesh.create_sign",
                    "Create a reusable generated sign static mesh asset.",
                    mesh_create_sign_schema()
                ),
                tool_definition(
                    "static_mesh.set_collision",
                    "Set simple collision options on static mesh assets.",
                    static_mesh_collision_schema()
                ),
                tool_definition(
                    "road.create_network",
                    "Create a grid road network in one bridge request.",
                    road_create_network_schema()
                ),
                tool_definition(
                    "scene.bulk_place_on_grid",
                    "Place repeated static mesh actors on a deterministic grid.",
                    scene_bulk_place_on_grid_schema()
                ),
                tool_definition(
                    "scene.create_city_block",
                    "Create one city block with roads, sidewalks, and buildings.",
                    scene_create_city_block_schema()
                ),
                tool_definition(
                    "scene.create_district",
                    "Create a preset multi-block district in one bridge request.",
                    scene_create_district_schema()
                ),
                tool_definition(
                    "material.create",
                    "Create a simple Unreal material asset.",
                    material_create_schema()
                ),
                tool_definition(
                    "material.create_instance",
                    "Create a material instance asset.",
                    material_instance_schema()
                ),
                tool_definition(
                    "material.create_procedural_texture",
                    "Create a small procedural texture asset.",
                    texture_create_schema()
                ),
                tool_definition(
                    "material.set_parameters",
                    "Set material or material instance parameters.",
                    material_set_parameters_schema()
                ),
                tool_definition(
                    "material.bulk_apply",
                    "Apply materials to actors by names or tags in bulk.",
                    material_bulk_apply_schema()
                ),
                tool_definition(
                    "world.bulk_set_materials",
                    "Set one material on actors selected by names or tags.",
                    world_bulk_set_materials_schema()
                ),
                tool_definition(
                    "lighting.set_night_scene",
                    "Configure moonlight, sky, fog, and exposure for a night scene.",
                    lighting_night_scene_schema()
                ),
                tool_definition(
                    "lighting.set_sky",
                    "Configure sky lighting for the current level.",
                    lighting_sky_schema()
                ),
                tool_definition(
                    "lighting.set_fog",
                    "Configure exponential height fog for the current level.",
                    lighting_fog_schema()
                ),
                tool_definition(
                    "lighting.set_post_process",
                    "Configure an unbound post-process volume.",
                    lighting_post_process_schema()
                ),
                tool_definition(
                    "lighting.bulk_set_lights",
                    "Create or update many lights in one bridge request.",
                    lighting_bulk_set_lights_schema()
                ),
                tool_definition(
                    "lighting.set_time_of_day",
                    "Configure one active sun directional light.",
                    lighting_time_of_day_schema()
                ),
                tool_definition(
                    "landscape.create",
                    "Create or update a real Unreal Landscape actor.",
                    landscape_create_schema()
                ),
                tool_definition(
                    "landscape.set_heightfield",
                    "Update a landscape heightfield from compact samples or procedural controls.",
                    landscape_heightfield_schema()
                ),
                tool_definition(
                    "landscape.paint_layers",
                    "Assign a landscape material and register paint layers.",
                    landscape_paint_layers_schema()
                ),
                tool_definition(
                    "placement.bulk_snap_to_ground",
                    "Snap actors selected by names or tags onto ground surfaces.",
                    placement_bulk_snap_schema()
                ),
                tool_definition(
                    "blueprint.create_actor",
                    "Create an actor Blueprint asset.",
                    blueprint_create_actor_schema()
                ),
                tool_definition(
                    "blueprint.add_static_mesh_component",
                    "Add a static mesh component template to a Blueprint.",
                    blueprint_static_mesh_component_schema()
                ),
                tool_definition(
                    "blueprint.add_light_component",
                    "Add a point, rect, or spot light component template to a Blueprint.",
                    blueprint_light_component_schema()
                ),
                tool_definition(
                    "blueprint.compile",
                    "Compile an Unreal Blueprint asset.",
                    blueprint_compile_schema()
                ),
                tool_definition(
                    "runtime.create_led_animation",
                    "Create a reusable LED material pulse animation asset.",
                    runtime_animation_schema()
                ),
                tool_definition(
                    "runtime.create_moving_light_animation",
                    "Create a reusable moving light animation asset.",
                    runtime_animation_schema()
                ),
                tool_definition(
                    "runtime.create_material_parameter_animation",
                    "Create a reusable material parameter animation asset.",
                    runtime_animation_schema()
                ),
                tool_definition(
                    "runtime.attach_animation_to_actor",
                    "Attach runtime animation assets to placed actors or Blueprints.",
                    runtime_attach_animation_schema()
                )
            ]
        }
    })
}

fn tool_definition(name: &str, description: &str, input_schema: Value) -> Value {
    json!({
        "name": name,
        "description": description,
        "inputSchema": input_schema
    })
}

fn empty_schema() -> Value {
    json!({
        "type": "object",
        "properties": {},
        "additionalProperties": false
    })
}

fn level_create_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "open": { "type": "boolean" },
            "save": { "type": "boolean" }
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn level_path_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" }
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn level_save_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" }
        },
        "additionalProperties": false
    })
}

fn string_array_schema() -> Value {
    json!({
        "type": "array",
        "items": { "type": "string" }
    })
}

fn vec3_schema() -> Value {
    json!({
        "type": "array",
        "items": { "type": "number" },
        "minItems": 3,
        "maxItems": 3
    })
}

fn vec2_schema() -> Value {
    json!({
        "type": "array",
        "items": { "type": "number" },
        "minItems": 2,
        "maxItems": 2
    })
}

fn vec4_schema() -> Value {
    json!({
        "type": "array",
        "items": { "type": "number" },
        "minItems": 4,
        "maxItems": 4
    })
}

fn u32_pair_schema() -> Value {
    json!({
        "type": "array",
        "items": { "type": "integer", "minimum": 1 },
        "minItems": 2,
        "maxItems": 2
    })
}

fn world_bulk_spawn_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "actors": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "name": { "type": "string" },
                        "mesh": { "type": "string" },
                        "location": vec3_schema(),
                        "rotation": vec3_schema(),
                        "scale": vec3_schema(),
                        "scene": { "type": "string" },
                        "group": { "type": "string" }
                    },
                    "required": ["name", "mesh"],
                    "additionalProperties": false
                }
            }
        },
        "required": ["actors"],
        "additionalProperties": false
    })
}

fn path_only_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" }
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn asset_import_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "source_file": { "type": "string" },
            "destination_path": { "type": "string" },
            "replace_existing": { "type": "boolean" },
            "save": { "type": "boolean" },
            "srgb": { "type": "boolean" },
            "generate_collision": { "type": "boolean" }
        },
        "required": ["source_file", "destination_path"],
        "additionalProperties": false
    })
}

fn asset_import_item_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "kind": {
                "type": "string",
                "enum": ["texture", "static_mesh"]
            },
            "source_file": { "type": "string" },
            "destination_path": { "type": "string" },
            "replace_existing": { "type": "boolean" },
            "save": { "type": "boolean" },
            "srgb": { "type": "boolean" },
            "generate_collision": { "type": "boolean" }
        },
        "required": ["kind", "source_file", "destination_path"],
        "additionalProperties": false
    })
}

fn asset_bulk_import_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "items": {
                "type": "array",
                "items": asset_import_item_schema()
            }
        },
        "required": ["items"],
        "additionalProperties": false
    })
}

fn asset_validate_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "paths": string_array_schema()
        },
        "required": ["paths"],
        "additionalProperties": false
    })
}

fn mesh_create_building_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "width": { "type": "number", "minimum": 1 },
            "depth": { "type": "number", "minimum": 1 },
            "height": { "type": "number", "minimum": 1 },
            "floors": { "type": "integer", "minimum": 1 },
            "window_rows": { "type": "integer", "minimum": 0 },
            "window_columns": { "type": "integer", "minimum": 0 },
            "material": { "type": "string" }
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn mesh_create_sign_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "width": { "type": "number", "minimum": 1 },
            "height": { "type": "number", "minimum": 1 },
            "depth": { "type": "number", "minimum": 1 },
            "text": { "type": "string" },
            "material": { "type": "string" }
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn static_mesh_collision_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "paths": string_array_schema(),
            "collision_trace": {
                "type": "string",
                "enum": ["project_default", "simple_and_complex", "use_simple_as_complex", "use_complex_as_simple"]
            },
            "simple_collision": { "type": "boolean" },
            "rebuild": { "type": "boolean" }
        },
        "required": ["paths"],
        "additionalProperties": false
    })
}

fn road_create_network_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "name_prefix": { "type": "string" },
            "scene": { "type": "string" },
            "group": { "type": "string" },
            "origin": vec3_schema(),
            "rows": { "type": "integer", "minimum": 1 },
            "columns": { "type": "integer", "minimum": 1 },
            "block_size": vec2_schema(),
            "road_width": { "type": "number", "minimum": 1 },
            "road_thickness": { "type": "number", "minimum": 1 },
            "road_mesh": { "type": "string" }
        },
        "required": ["name_prefix"],
        "additionalProperties": false
    })
}

fn scene_bulk_place_on_grid_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "name_prefix": { "type": "string" },
            "mesh": { "type": "string" },
            "scene": { "type": "string" },
            "group": { "type": "string" },
            "origin": vec3_schema(),
            "rows": { "type": "integer", "minimum": 1 },
            "columns": { "type": "integer", "minimum": 1 },
            "spacing": vec2_schema(),
            "rotation": vec3_schema(),
            "scale": vec3_schema(),
            "yaw_variation": { "type": "number", "minimum": 0 },
            "scale_variation": { "type": "number", "minimum": 0 },
            "seed": { "type": "integer", "minimum": 0 }
        },
        "required": ["name_prefix", "mesh"],
        "additionalProperties": false
    })
}

fn scene_create_city_block_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "name_prefix": { "type": "string" },
            "scene": { "type": "string" },
            "group": { "type": "string" },
            "origin": vec3_schema(),
            "size": vec2_schema(),
            "road_width": { "type": "number", "minimum": 1 },
            "sidewalk_width": { "type": "number", "minimum": 0 },
            "road_mesh": { "type": "string" },
            "sidewalk_mesh": { "type": "string" },
            "building_mesh": { "type": "string" },
            "building_rows": { "type": "integer", "minimum": 1 },
            "building_columns": { "type": "integer", "minimum": 1 },
            "building_scale": vec3_schema(),
            "seed": { "type": "integer", "minimum": 0 }
        },
        "required": ["name_prefix", "building_mesh"],
        "additionalProperties": false
    })
}

fn scene_create_district_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "name_prefix": { "type": "string" },
            "preset": {
                "type": "string",
                "enum": ["downtown", "residential", "industrial", "beach", "hills"]
            },
            "scene": { "type": "string" },
            "group": { "type": "string" },
            "origin": vec3_schema(),
            "blocks": u32_pair_schema(),
            "block_size": vec2_schema(),
            "road_width": { "type": "number", "minimum": 1 },
            "road_mesh": { "type": "string" },
            "building_mesh": { "type": "string" },
            "seed": { "type": "integer", "minimum": 0 }
        },
        "required": ["name_prefix", "building_mesh"],
        "additionalProperties": false
    })
}

fn scalar_parameter_schema() -> Value {
    json!({
        "type": "array",
        "items": {
            "type": "object",
            "properties": {
                "name": { "type": "string" },
                "value": { "type": "number" }
            },
            "required": ["name", "value"],
            "additionalProperties": false
        }
    })
}

fn vector_parameter_schema() -> Value {
    json!({
        "type": "array",
        "items": {
            "type": "object",
            "properties": {
                "name": { "type": "string" },
                "value": vec4_schema()
            },
            "required": ["name", "value"],
            "additionalProperties": false
        }
    })
}

fn texture_parameter_schema() -> Value {
    json!({
        "type": "array",
        "items": {
            "type": "object",
            "properties": {
                "name": { "type": "string" },
                "value": { "type": "string" }
            },
            "required": ["name", "value"],
            "additionalProperties": false
        }
    })
}

fn material_create_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "base_color": vec4_schema(),
            "metallic": { "type": "number" },
            "roughness": { "type": "number" },
            "specular": { "type": "number" },
            "emissive_color": vec4_schema()
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn material_instance_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "parent": { "type": "string" },
            "scalar_parameters": scalar_parameter_schema(),
            "vector_parameters": vector_parameter_schema(),
            "texture_parameters": texture_parameter_schema()
        },
        "required": ["path", "parent"],
        "additionalProperties": false
    })
}

fn texture_create_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "pattern": {
                "type": "string",
                "enum": ["solid", "checker"]
            },
            "width": { "type": "integer", "minimum": 1 },
            "height": { "type": "integer", "minimum": 1 },
            "color_a": vec4_schema(),
            "color_b": vec4_schema(),
            "checker_size": { "type": "integer", "minimum": 1 }
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn material_set_parameters_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "scalar_parameters": scalar_parameter_schema(),
            "vector_parameters": vector_parameter_schema(),
            "texture_parameters": texture_parameter_schema()
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn material_assignment_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "material": { "type": "string" },
            "names": string_array_schema(),
            "tags": string_array_schema(),
            "slot": { "type": "integer", "minimum": 0 }
        },
        "required": ["material"],
        "additionalProperties": false
    })
}

fn material_bulk_apply_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "assignments": {
                "type": "array",
                "items": material_assignment_schema()
            }
        },
        "required": ["assignments"],
        "additionalProperties": false
    })
}

fn world_bulk_set_materials_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "names": string_array_schema(),
            "tags": string_array_schema(),
            "material": { "type": "string" },
            "slot": { "type": "integer", "minimum": 0 }
        },
        "required": ["material"],
        "additionalProperties": false
    })
}

fn lighting_night_scene_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "moon_rotation": vec3_schema(),
            "moon_intensity": { "type": "number" },
            "moon_color": vec4_schema(),
            "sky_intensity": { "type": "number" },
            "fog_density": { "type": "number", "minimum": 0 },
            "exposure_compensation": { "type": "number" }
        },
        "required": [],
        "additionalProperties": false
    })
}

fn lighting_sky_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "sky_intensity": { "type": "number" },
            "lower_hemisphere_color": vec4_schema()
        },
        "required": [],
        "additionalProperties": false
    })
}

fn lighting_fog_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "density": { "type": "number", "minimum": 0 },
            "height_falloff": { "type": "number", "minimum": 0 },
            "color": vec4_schema(),
            "start_distance": { "type": "number", "minimum": 0 }
        },
        "required": [],
        "additionalProperties": false
    })
}

fn lighting_post_process_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "exposure_compensation": { "type": "number" },
            "min_brightness": { "type": "number", "minimum": 0 },
            "max_brightness": { "type": "number", "minimum": 0 },
            "bloom_intensity": { "type": "number", "minimum": 0 }
        },
        "required": [],
        "additionalProperties": false
    })
}

fn light_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "name": { "type": "string" },
            "kind": {
                "type": "string",
                "enum": ["point", "rect", "spot"]
            },
            "location": vec3_schema(),
            "rotation": vec3_schema(),
            "scale": vec3_schema(),
            "color": vec4_schema(),
            "intensity": { "type": "number", "minimum": 0 },
            "attenuation_radius": { "type": "number", "minimum": 0 },
            "source_radius": { "type": "number", "minimum": 0 },
            "source_width": { "type": "number", "minimum": 0 },
            "source_height": { "type": "number", "minimum": 0 },
            "tags": string_array_schema()
        },
        "required": ["name"],
        "additionalProperties": false
    })
}

fn lighting_bulk_set_lights_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "lights": {
                "type": "array",
                "items": light_schema()
            }
        },
        "required": ["lights"],
        "additionalProperties": false
    })
}

fn lighting_time_of_day_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "sun_rotation": vec3_schema(),
            "sun_intensity": { "type": "number", "minimum": 0 },
            "sun_color": vec4_schema()
        },
        "required": [],
        "additionalProperties": false
    })
}

fn landscape_create_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "name": { "type": "string" },
            "component_count": u32_pair_schema(),
            "section_size": { "type": "integer", "minimum": 1 },
            "sections_per_component": { "type": "integer", "minimum": 1 },
            "location": vec3_schema(),
            "scale": vec3_schema(),
            "material": { "type": "string" },
            "tags": string_array_schema()
        },
        "required": ["name"],
        "additionalProperties": false
    })
}

fn landscape_heightfield_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "name": { "type": "string" },
            "width": { "type": "integer", "minimum": 1 },
            "height": { "type": "integer", "minimum": 1 },
            "base_height": { "type": "number" },
            "amplitude": { "type": "number" },
            "frequency": { "type": "number" },
            "seed": { "type": "integer", "minimum": 0 },
            "city_pad_radius": { "type": "number", "minimum": 0 },
            "city_pad_falloff": { "type": "number", "minimum": 0 },
            "samples": {
                "type": "array",
                "items": { "type": "number" }
            }
        },
        "required": ["name"],
        "additionalProperties": false
    })
}

fn landscape_paint_layers_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "name": { "type": "string" },
            "material": { "type": "string" },
            "layers": string_array_schema()
        },
        "required": ["name"],
        "additionalProperties": false
    })
}

fn placement_bulk_snap_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "names": string_array_schema(),
            "tags": string_array_schema(),
            "include_generated": { "type": "boolean" },
            "trace_distance": { "type": "number", "minimum": 0 },
            "offset_z": { "type": "number" }
        },
        "required": [],
        "additionalProperties": false
    })
}

fn blueprint_create_actor_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "parent_class": { "type": "string" }
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn blueprint_compile_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "save": { "type": "boolean" }
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn blueprint_static_mesh_component_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "blueprint": { "type": "string" },
            "name": { "type": "string" },
            "mesh": { "type": "string" },
            "material": { "type": "string" },
            "location": vec3_schema(),
            "rotation": vec3_schema(),
            "scale": vec3_schema()
        },
        "required": ["blueprint", "name", "mesh"],
        "additionalProperties": false
    })
}

fn blueprint_light_component_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "blueprint": { "type": "string" },
            "name": { "type": "string" },
            "kind": {
                "type": "string",
                "enum": ["point", "rect", "spot"]
            },
            "location": vec3_schema(),
            "rotation": vec3_schema(),
            "scale": vec3_schema(),
            "color": vec4_schema(),
            "intensity": { "type": "number", "minimum": 0 },
            "attenuation_radius": { "type": "number", "minimum": 0 },
            "source_radius": { "type": "number", "minimum": 0 },
            "source_width": { "type": "number", "minimum": 0 },
            "source_height": { "type": "number", "minimum": 0 }
        },
        "required": ["blueprint", "name"],
        "additionalProperties": false
    })
}

fn runtime_animation_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "target_component": { "type": "string" },
            "parameter_name": { "type": "string" },
            "color_a": vec4_schema(),
            "color_b": vec4_schema(),
            "from_scalar": { "type": "number" },
            "to_scalar": { "type": "number" },
            "speed": { "type": "number" },
            "amplitude": { "type": "number" },
            "axis": vec3_schema(),
            "base_intensity": { "type": "number", "minimum": 0 },
            "phase_offset": { "type": "number" }
        },
        "required": ["path"],
        "additionalProperties": false
    })
}

fn runtime_attach_animation_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "names": string_array_schema(),
            "tags": string_array_schema(),
            "blueprint": { "type": "string" },
            "animations": string_array_schema()
        },
        "required": ["animations"],
        "additionalProperties": false
    })
}

fn world_bulk_delete_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "names": string_array_schema(),
            "tags": string_array_schema()
        },
        "additionalProperties": false
    })
}

fn world_query_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "names": string_array_schema(),
            "tags": string_array_schema(),
            "include_generated": { "type": "boolean" },
            "limit": {
                "type": "integer",
                "minimum": 1
            }
        },
        "additionalProperties": false
    })
}

fn world_snapshot_schema() -> Value {
    json!({
        "type": "object",
        "properties": {
            "path": { "type": "string" },
            "tags": string_array_schema()
        },
        "additionalProperties": false
    })
}

async fn tools_call_response(id: Value, params: Option<&Value>, tools: &ConnectionTools) -> Value {
    let Some(name) = params
        .and_then(|params| params.get("name"))
        .and_then(Value::as_str)
    else {
        return json_rpc_error(id, -32602, "Missing tool name".to_string());
    };

    let arguments = params
        .and_then(|params| params.get("arguments"))
        .cloned()
        .unwrap_or_else(|| json!({}));

    match call_tool(name, arguments, tools).await {
        Ok(response) => json!({
            "jsonrpc": "2.0",
            "id": id,
            "result": {
                "content": [
                    {
                        "type": "text",
                        "text": response.summary
                    },
                    {
                        "type": "text",
                        "text": response.data.to_string()
                    }
                ],
                "isError": false
            }
        }),
        Err(error) if error.to_string().starts_with("Unknown tool") => {
            json_rpc_error(id, -32602, error.to_string())
        }
        Err(error) => json!({
            "jsonrpc": "2.0",
            "id": id,
            "result": {
                "content": [
                    {
                        "type": "text",
                        "text": error.to_string()
                    }
                ],
                "isError": true
            }
        }),
    }
}

async fn call_tool(
    name: &str,
    arguments: Value,
    tools: &ConnectionTools,
) -> anyhow::Result<ToolResponse> {
    match name {
        "connection.ping" => tools.ping().await,
        "connection.status" => tools.status().await,
        "connection.capabilities" => tools.capabilities().await,
        "level.create" => tools.level_create(arguments).await,
        "level.open" => tools.level_open(arguments).await,
        "level.save" => tools.level_save(arguments).await,
        "level.list" => tools.level_list().await,
        "world.bulk_spawn" => tools.world_bulk_spawn(arguments).await,
        "world.bulk_delete" => tools.world_bulk_delete(arguments).await,
        "world.query" => tools.world_query(arguments).await,
        "world.snapshot" => tools.world_snapshot(arguments).await,
        "asset.create_folder" => tools.asset_create_folder(arguments).await,
        "asset.import_texture" => tools.asset_import_texture(arguments).await,
        "asset.import_static_mesh" => tools.asset_import_static_mesh(arguments).await,
        "asset.bulk_import" => tools.asset_bulk_import(arguments).await,
        "asset.validate" => tools.asset_validate(arguments).await,
        "mesh.create_building" => tools.mesh_create_building(arguments).await,
        "mesh.create_sign" => tools.mesh_create_sign(arguments).await,
        "static_mesh.set_collision" => tools.static_mesh_set_collision(arguments).await,
        "road.create_network" => tools.road_create_network(arguments).await,
        "scene.bulk_place_on_grid" => tools.scene_bulk_place_on_grid(arguments).await,
        "scene.create_city_block" => tools.scene_create_city_block(arguments).await,
        "scene.create_district" => tools.scene_create_district(arguments).await,
        "material.create" => tools.material_create(arguments).await,
        "material.create_instance" => tools.material_create_instance(arguments).await,
        "material.create_procedural_texture" => {
            tools.material_create_procedural_texture(arguments).await
        }
        "material.set_parameters" => tools.material_set_parameters(arguments).await,
        "material.bulk_apply" => tools.material_bulk_apply(arguments).await,
        "world.bulk_set_materials" => tools.world_bulk_set_materials(arguments).await,
        "lighting.set_night_scene" => tools.lighting_set_night_scene(arguments).await,
        "lighting.set_sky" => tools.lighting_set_sky(arguments).await,
        "lighting.set_fog" => tools.lighting_set_fog(arguments).await,
        "lighting.set_post_process" => tools.lighting_set_post_process(arguments).await,
        "lighting.bulk_set_lights" => tools.lighting_bulk_set_lights(arguments).await,
        "lighting.set_time_of_day" => tools.lighting_set_time_of_day(arguments).await,
        "landscape.create" => tools.landscape_create(arguments).await,
        "landscape.set_heightfield" => tools.landscape_set_heightfield(arguments).await,
        "landscape.paint_layers" => tools.landscape_paint_layers(arguments).await,
        "placement.bulk_snap_to_ground" => tools.placement_bulk_snap_to_ground(arguments).await,
        "blueprint.create_actor" => tools.blueprint_create_actor(arguments).await,
        "blueprint.add_static_mesh_component" => {
            tools.blueprint_add_static_mesh_component(arguments).await
        }
        "blueprint.add_light_component" => tools.blueprint_add_light_component(arguments).await,
        "blueprint.compile" => tools.blueprint_compile(arguments).await,
        "runtime.create_led_animation" => tools.runtime_create_led_animation(arguments).await,
        "runtime.create_moving_light_animation" => {
            tools.runtime_create_moving_light_animation(arguments).await
        }
        "runtime.create_material_parameter_animation" => {
            tools
                .runtime_create_material_parameter_animation(arguments)
                .await
        }
        "runtime.attach_animation_to_actor" => {
            tools.runtime_attach_animation_to_actor(arguments).await
        }
        _ => anyhow::bail!("Unknown tool: {name}"),
    }
}

fn json_rpc_error(id: Value, code: i64, message: String) -> Value {
    json!({
        "jsonrpc": "2.0",
        "id": id,
        "error": {
            "code": code,
            "message": message
        }
    })
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use crate::{BridgeClient, ConnectionTools};
    use unreal_mcp_tests::start_fake_bridge;

    use super::run_stdio_server;
    use tokio::time::timeout;

    #[tokio::test]
    async fn run_stdio_server_waits_for_stdin_eof() {
        let fake = start_fake_bridge().await.expect("fake bridge");
        let tools = ConnectionTools::new(BridgeClient::new(fake.address().to_string()));
        let (input_writer, server_reader) = tokio::io::duplex(64);
        let (_output_reader, server_writer) = tokio::io::duplex(64);

        let mut server_task = tokio::spawn(run_stdio_server(server_reader, server_writer, tools));

        let early_exit = timeout(Duration::from_millis(25), &mut server_task).await;
        assert!(
            early_exit.is_err(),
            "stdio server should not complete before stdin EOF"
        );

        drop(input_writer);

        let result = timeout(Duration::from_secs(1), server_task)
            .await
            .expect("stdio server should exit after stdin EOF")
            .expect("stdio server task should not panic");
        result.expect("stdio server should return ok");
    }
}
