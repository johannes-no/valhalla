# Custom Attributes Sidecar

Per-directed-edge float attributes stored in a separate tar archive alongside the main tile set. Use cases include routing penalties derived from data that changes independently of the OSM graph — traffic safety scores, corporate fleet restrictions, road quality surveys, etc. — without rebuilding tiles.

## How it works

The sidecar is a tar archive (`custom_attributes.tar`) with the same directory structure as `valhalla_tiles.tar`. For each routing tile `2/001/234/567.gph` there is a corresponding sidecar tile `2/001/234/567.cab`. A `.cab` file is a compact binary:

```
[CustomAttributesTileHeader: uint32_t edge_count]
[float value[0]]
[float value[1]]
...
[float value[edge_count - 1]]
```

Each float corresponds to one directed edge in the tile, indexed by the same local index that `GraphId::id()` returns. The index space covers all directed edges including shortcut edges (i.e. `directededgecount()` from `GraphTileHeader`). Values are application-defined; the routing engine treats them as unitless weights in `[0.0, 1.0]`.

`GraphReader` loads the archive at startup (read-only mmap). The archive is held open so an external process can update it while the service runs — the same pattern used by the live traffic overlay. The `GraphTile` wrapper (`CustomAttributesTile`) marks its pointers `const volatile` for the same reason.

## Building the sidecar tar

`scripts/build_custom_attributes_tar.py` creates an initial sidecar from an existing `valhalla_tiles.tar`. Every edge gets the same default value (0.0 unless overridden).

```bash
python3 scripts/build_custom_attributes_tar.py \
    --tiles-tar /data/valhalla/tiles.tar \
    --output    /data/custom_attributes.tar \
    --default-value 0.0
```

Options:

| Flag | Default | Description |
|------|---------|-------------|
| `--tiles-tar` | required | Path to the main `valhalla_tiles.tar` |
| `--output` | required | Output path for the new sidecar tar |
| `--default-value` | `0.0` | Float written into every edge slot |
| `--random` | off | Fill each slot with a random value in `[0.0, 1.0)` instead of the default |

The script reads only the `GraphTileHeader` from each tile to determine `directededgecount`. It does **not** modify the main tile tar.

After writing the initial archive, replace individual `.cab` entries in-place to populate real values. The in-memory format is simple enough to update with any language that supports `struct.pack` or equivalent. The sidecar is mmap'd read-write by the update process, read-only by the routing service — the same arrangement as `traffic.tar`.

**Important:** Always regenerate or update the sidecar from the same tile set it was built against. Edge indices are stable within a tileset version but will shift if tiles are rebuilt from new OSM data. There is no cross-check between the sidecar and the main tileset at load time; a mismatched sidecar silently applies values to the wrong edges.

## Configuration

Add `custom_attributes_extract` to the `mjolnir` section of your config. `valhalla_build_config` emits this key by default:

```json
{
  "mjolnir": {
    "tile_extract":              "/data/valhalla/tiles.tar",
    "traffic_extract":           "/data/valhalla/traffic.tar",
    "custom_attributes_extract": "/data/custom_attributes.tar"
  }
}
```

The key is optional — if the file does not exist or the key is omitted, routing proceeds normally with no custom attribute effect and no error.

## Using custom attributes in a route request

Set `use_custom_attribute` in the costing options. Valid range: `[0.0, 1.0]`. Default: `0.0` (disabled).

```json
{
  "locations": [
    {"lat": 52.5, "lon": 13.4},
    {"lat": 52.6, "lon": 13.5}
  ],
  "costing": "auto",
  "costing_options": {
    "auto": {
      "use_custom_attribute": 0.8
    }
  }
}
```

Supported costing models: `auto`, `bus`, `taxi`, `truck`, `motorcycle`, `motor_scooter`, `bicycle`, `pedestrian`.

### Cost semantics

The custom attribute contributes additively to the edge cost factor:

```
factor += cat->value(edge_index) * use_custom_attribute
```

`factor` is then multiplied by the base edge traversal time. A `cab_value` of `1.0` with `use_custom_attribute=1.0` adds `1.0` to `factor` — equivalent to the maximum highway avoidance bias. At `use_custom_attribute=0.5` and `cab_value=1.0` the contribution is `0.5`. Edges with `cab_value=0.0` are unaffected regardless of `use_custom_attribute`.

The factor penalty is proportional to edge traversal time: a slow 10 km/h urban street receives a larger absolute cost increase than a fast 100 km/h motorway for the same `cab_value`. This is intentional for penalizing time-weighted exposure (e.g. noise, pollution), but may not suit all use cases.

## Inspecting custom attribute values via `trace_attributes`

The raw `.cab` value for each matched edge is available in `trace_attributes` responses:

```json
{
  "shape": "...",
  "edges": [
    {
      "custom_attribute": 0.42,
      ...
    }
  ]
}
```

Enable it by adding `"edge.custom_attribute": true` to the `filters.attributes` list. The field is omitted (not `0.0`) when no sidecar is loaded, so clients can distinguish "no data" from a genuine zero value.

`use_custom_attribute` in the costing options does **not** affect the value returned here — `custom_attribute` is always the raw `.cab` float, independent of the routing weight.

## Binary format reference

```
Offset  Size  Type      Field
0       4     uint32_t  edge_count
4       4*N   float[]   value[0..edge_count-1]
```

Total file size must equal `sizeof(CustomAttributesTileHeader) + edge_count * sizeof(float)` exactly. A mismatch causes the tile to be skipped with a `LOG_ERROR` at startup; routing for that tile proceeds without custom attributes.

There is currently no format version field. Do not change the layout without updating both the C++ reader (`valhalla/baldr/custom_attributes_tile.h`) and the Python build script simultaneously.
