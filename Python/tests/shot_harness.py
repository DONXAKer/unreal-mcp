"""Lookdev shot harness (TOOL-SHOT-001).

What it is: "Playwright for the picture". The gameplay harness
(tests/playtest_visual.py) already drives the game and catches REGRESSION by
pixel-diffing against its own previous frames. This one answers a different
question - "is the picture good, and are the figures the right size" - by

  1. framing a shot from an EXACT camera pose (reproducible run to run),
  2. orbiting the camera around a target so proportions and silhouette are
     judged from every side rather than one lucky angle,
  3. turning each frame into NUMBERS (brightness, contrast, saturation, blown
     highlights, crushed blacks) and checking them against a target corridor,
  4. measuring mesh bounds/triangles/texture size against the style guide,
  5. laying the frames next to an art-direction reference so a vision agent can
     score them on a rubric.

Reference look: Broken Arrow (modern military RTS, top-down tactical), with
Gostomel Landing as a secondary reference.

Runs against ONE editor on port 55557. Two editors with Lumen exhaust VRAM and
produce a red banner plus overexposed frames that are indistinguishable from
real graphics bugs - never open a second one for lookdev.

Usage (from D:/WarCard/unreal-mcp/Python):
    uv run python tests/shot_harness.py
    uv run python tests/shot_harness.py --shots board_overview,unit_orbit
    uv run python tests/shot_harness.py --skip-mesh-audit

Requires: Unreal Editor open with UnrealMCP 3.14.0+ (viewport_set_camera,
viewport_set_view_mode, mesh_get_stats).

All console output is ASCII - the Windows console is cp1251 here.
"""

from __future__ import annotations

import argparse
import json
import logging
import math
import shutil
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._frame_metrics import check_targets, compute_metrics, format_report
from unreal_mcp_server import UnrealConnection

MCP_HOST = "127.0.0.1"
MCP_PORT = 55557  # single editor only - see module docstring

REPO_ROOT = Path("D:/WarCard")
PROJECT_SAVED = REPO_ROOT / "client" / "Saved"
SHOTS_ROOT = PROJECT_SAVED / "Shots"
REFS_ROOT = REPO_ROOT / "client" / "ArtSource" / "Refs"
CONFIG_PATH = Path(__file__).with_name("shots.json")

SCREENSHOT_TIMEOUT = 60.0
SCREENSHOT_POLL = 0.5

MIN_PLUGIN_VERSION = (3, 14, 0)


# ---------------------------------------------------------------------------
# Bridge helpers
# ---------------------------------------------------------------------------

def _send(conn: UnrealConnection, command: str, params: dict[str, Any]) -> dict[str, Any]:
    """Send one bridge command; returns {} on transport failure."""
    try:
        response = conn.send_command(command, params)
    except Exception as exc:
        print(f"  ! {command} transport error: {exc}")
        return {}
    return response or {}


def _result(response: dict[str, Any]) -> dict[str, Any]:
    """Unwrap the bridge envelope; handles both raw and {status,result} shapes."""
    if not response:
        return {}
    if isinstance(response.get("result"), dict):
        return response["result"]
    return response


def _check_plugin_version(conn: UnrealConnection) -> bool:
    resp = _result(_send(conn, "ping", {}))
    version = str(resp.get("plugin_version", "0.0.0"))
    try:
        parsed = tuple(int(p) for p in version.split(".")[:3])
    except ValueError:
        parsed = (0, 0, 0)
    if parsed < MIN_PLUGIN_VERSION:
        need = ".".join(str(p) for p in MIN_PLUGIN_VERSION)
        print(f"ERROR: plugin {version} is older than {need} - rebuild the plugin first")
        return False
    print(f"Plugin version {version} OK")
    return True


# ---------------------------------------------------------------------------
# Capture
# ---------------------------------------------------------------------------

def _capture(
    conn: UnrealConnection,
    filename: str,
    resolution: list[int],
    dest: Path,
) -> Path | None:
    """Capture the level viewport and copy the PNG into the run folder.

    Uses `viewport_capture`, not `take_screenshot`: the latter goes through
    FScreenshotRequest, which is serviced by the GAME viewport and therefore
    never fires in a plain editor session - verified 2026-08-03, the file simply
    never appears. `viewport_capture` draws and reads pixels synchronously, so
    the file is on disk when the command returns.

    Consequence: the captured size follows the editor viewport, so the
    `resolution` field in shots.json is advisory here and recorded for the
    report rather than enforced.
    """
    resp = _result(_send(conn, "viewport_capture", {"filename": filename}))
    if not resp or not resp.get("assetPath"):
        print(f"  ! capture failed for {filename}")
        return None

    engine_path = Path(resp["assetPath"])
    if not engine_path.exists():
        engine_path = PROJECT_SAVED / "Screenshots" / filename
    if not engine_path.exists():
        print(f"  ! capture reported success but no file at {engine_path}")
        return None

    got = [int(resp.get("width", 0)), int(resp.get("height", 0))]
    if resolution and got != [int(resolution[0]), int(resolution[1])]:
        print(f"    note: viewport is {got[0]}x{got[1]}, shots.json asked for "
              f"{int(resolution[0])}x{int(resolution[1])}")

    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(engine_path, dest)
    return dest


def _set_camera(conn: UnrealConnection, location: list[float], rotation: list[float], fov: float) -> None:
    params: dict[str, Any] = {"location": location, "rotation": rotation}
    if fov and fov > 0:
        params["fov"] = fov
    _send(conn, "viewport_set_camera", params)


def _orbit_poses(shot: dict[str, Any]) -> list[tuple[list[float], list[float]]]:
    """Camera poses evenly spaced on a circle around the pivot.

    Each pose looks back at the pivot: yaw points inward, pitch is the angle
    down from the orbit height to the pivot.
    """
    pivot = shot.get("pivot", [0.0, 0.0, 0.0])
    radius = float(shot.get("radius", 500.0))
    height = float(shot.get("height", 300.0))
    steps = max(int(shot.get("steps", 8)), 1)

    pitch = -math.degrees(math.atan2(height, radius)) if radius > 0 else -90.0

    poses = []
    for i in range(steps):
        theta = 2.0 * math.pi * i / steps
        x = pivot[0] + radius * math.cos(theta)
        y = pivot[1] + radius * math.sin(theta)
        z = pivot[2] + height
        # Yaw so the camera faces the pivot: opposite of the outward direction.
        yaw = math.degrees(theta) + 180.0
        poses.append(([x, y, z], [pitch, yaw, 0.0]))
    return poses


# ---------------------------------------------------------------------------
# Shots
# ---------------------------------------------------------------------------

def _run_shot(
    conn: UnrealConnection,
    shot: dict[str, Any],
    defaults: dict[str, Any],
    run_dir: Path,
) -> list[dict[str, Any]]:
    name = shot["name"]
    resolution = shot.get("resolution", defaults.get("resolution", [1920, 1080]))
    view_mode = shot.get("view_mode", defaults.get("view_mode", "lit"))
    targets = shot.get("targets", defaults.get("targets", {}))
    fov = float(shot.get("fov", 0.0))

    print(f"\n== shot: {name} ({shot.get('type', 'static')})")
    if shot.get("description"):
        print(f"   {shot['description']}")

    _send(conn, "viewport_set_view_mode", {"mode": view_mode})

    frames: list[dict[str, Any]] = []

    if shot.get("type") == "orbit":
        poses = _orbit_poses(shot)
        for idx, (location, rotation) in enumerate(poses):
            frame_name = f"{name}_{idx:02d}"
            _set_camera(conn, location, rotation, fov)
            dest = _capture(conn, f"shot_{frame_name}.png", resolution, run_dir / f"{frame_name}.png")
            frames.append(_measure(frame_name, dest, targets, location, rotation, fov))
    else:
        camera = shot.get("camera", {})
        location = camera.get("location", [0, -2500, 2100])
        rotation = camera.get("rotation", [-48, 90, 0])
        shot_fov = float(camera.get("fov", fov))
        _set_camera(conn, location, rotation, shot_fov)
        dest = _capture(conn, f"shot_{name}.png", resolution, run_dir / f"{name}.png")
        frames.append(_measure(name, dest, targets, location, rotation, shot_fov))

    return frames


def _measure(
    frame_name: str,
    dest: Path | None,
    targets: dict[str, Any],
    location: list[float],
    rotation: list[float],
    fov: float,
) -> dict[str, Any]:
    entry: dict[str, Any] = {
        "frame": frame_name,
        "camera": {"location": location, "rotation": rotation, "fov": fov},
    }
    if dest is None:
        entry["error"] = "capture failed"
        print(f"  [SKIP] {frame_name}: capture failed")
        return entry

    metrics = compute_metrics(dest)
    check = check_targets(metrics, targets)
    entry["file"] = dest.name
    entry["metrics"] = metrics
    entry["targets"] = targets
    entry["check"] = check
    print("  " + format_report(frame_name, metrics, check))

    # A near-flat frame means the capture is broken (black screen, wrong
    # viewport), not that the lookdev is bad - call it out explicitly.
    if metrics["contrast"] < 0.02:
        entry["warning"] = "frame is nearly flat - capture likely broken, not a lookdev result"
        print("    warning: frame is nearly flat - check the capture, not the lighting")
    return entry


# ---------------------------------------------------------------------------
# Mesh audit
# ---------------------------------------------------------------------------

def _run_mesh_audit(conn: UnrealConnection, audit: dict[str, Any]) -> list[dict[str, Any]]:
    assets = audit.get("assets", [])
    if not assets:
        print("\n== mesh audit: no assets listed in shots.json - skipped")
        return []

    tile_size = float(audit.get("tile_size", 100.0))
    th = audit.get("thresholds", {})
    min_tris = int(th.get("min_triangles_lod0", 10000))
    min_tex = int(th.get("min_texture_dim", 2048))
    max_tiles = float(th.get("max_size_in_tiles", 2.5))
    min_tiles = float(th.get("min_size_in_tiles", 0.25))

    print(f"\n== mesh audit: {len(assets)} asset(s), tile={tile_size} UU")
    results = []
    for asset_path in assets:
        resp = _result(_send(conn, "mesh_get_stats", {"assetPath": asset_path, "tileSize": tile_size}))
        meta = resp.get("meta", resp)
        if not meta or "sizeInTiles" not in meta:
            print(f"  [SKIP] {asset_path}: no stats returned")
            results.append({"asset": asset_path, "error": "no stats returned"})
            continue

        tiles = meta.get("sizeInTiles", [0, 0, 0])
        longest_tiles = max(tiles[0], tiles[1])
        tris = int(meta.get("trianglesLOD0", 0))
        tex = int(meta.get("maxTextureDim", 0))

        problems = []
        if tris < min_tris:
            problems.append(f"LOD0 {tris} tris < {min_tris}")
        if tex < min_tex:
            problems.append(f"max texture {tex}px < {min_tex}px")
        if longest_tiles > max_tiles:
            problems.append(f"footprint {longest_tiles:.2f} tiles > {max_tiles} (import scale too big?)")
        if longest_tiles < min_tiles:
            problems.append(f"footprint {longest_tiles:.2f} tiles < {min_tiles} (import scale too small?)")

        status = "PASS" if not problems else "FAIL"
        print(f"  [{status}] {asset_path}: {longest_tiles:.2f} tiles, {tris} tris, {tex}px, nanite={meta.get('naniteEnabled')}")
        for p in problems:
            print(f"      {p}")

        results.append(
            {
                "asset": asset_path,
                "sizeUU": meta.get("sizeUU"),
                "sizeInTiles": tiles,
                "footprintTiles": round(longest_tiles, 3),
                "trianglesLOD0": tris,
                "maxTextureDim": tex,
                "naniteEnabled": meta.get("naniteEnabled"),
                "problems": problems,
                "pass": not problems,
            }
        )
    return results


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description="Lookdev shot harness (TOOL-SHOT-001)")
    parser.add_argument("--shots", default="", help="comma-separated shot names; default all")
    parser.add_argument("--skip-mesh-audit", action="store_true")
    parser.add_argument("--config", default=str(CONFIG_PATH))
    args = parser.parse_args()

    config = json.loads(Path(args.config).read_text(encoding="utf-8"))
    defaults = config.get("defaults", {})
    all_shots = config.get("shots", [])

    wanted = [s.strip() for s in args.shots.split(",") if s.strip()]
    shots = [s for s in all_shots if not wanted or s["name"] in wanted]
    if not shots:
        print("ERROR: no shots matched")
        return 2

    conn = UnrealConnection(MCP_HOST, MCP_PORT)
    if not _check_plugin_version(conn):
        return 2

    level = defaults.get("level")
    if level:
        print(f"Loading level {level}")
        _send(conn, "load_level", {"mapPath": level})

    run_id = time.strftime("%Y%m%d_%H%M%S")
    run_dir = SHOTS_ROOT / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    print(f"Run dir: {run_dir}")

    frames: list[dict[str, Any]] = []
    for shot in shots:
        frames.extend(_run_shot(conn, shot, defaults, run_dir))

    mesh_results: list[dict[str, Any]] = []
    if not args.skip_mesh_audit:
        mesh_results = _run_mesh_audit(conn, config.get("mesh_audit", {}))

    # Reference frames live outside the run dir and are never written by the
    # harness - they are the target, not an output.
    references = {}
    for shot in shots:
        ref = shot.get("reference")
        if ref:
            ref_path = REFS_ROOT / ref
            references[shot["name"]] = {
                "path": str(ref_path),
                "exists": ref_path.exists(),
            }

    failed_frames = [f for f in frames if f.get("check") and not f["check"]["pass"]]
    failed_meshes = [m for m in mesh_results if not m.get("pass", True)]

    manifest = {
        "run_id": run_id,
        "reference_look": "Broken Arrow (primary), Gostomel Landing (secondary)",
        "rubric": "client/Documentation/Lookdev_Rubric.md",
        "frames": frames,
        "mesh_audit": mesh_results,
        "references": references,
        "summary": {
            "frames_total": len(frames),
            "frames_failed": len(failed_frames),
            "meshes_total": len(mesh_results),
            "meshes_failed": len(failed_meshes),
        },
    }
    (run_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8"
    )

    print("\n== summary")
    print(f"  frames: {len(frames)} total, {len(failed_frames)} outside target corridor")
    print(f"  meshes: {len(mesh_results)} audited, {len(failed_meshes)} below style-guide bar")
    missing_refs = [n for n, r in references.items() if not r["exists"]]
    if missing_refs:
        print(f"  missing reference frames: {', '.join(missing_refs)}")
        print("  (generate them with client/Scripts/gen_lookdev_target.py)")
    print(f"  manifest: {run_dir / 'manifest.json'}")
    print("\nNext: hand the run folder to the visual-playtest-reviewer agent for rubric scoring.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
