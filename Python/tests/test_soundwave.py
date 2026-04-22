"""Test recipe for SoundWave primitive: import WAV → asset_exists → delete."""

import struct
import tempfile
import os
from tools.result_format import ok, fail

TEST_ASSET = "/Game/Dev/MCP005_Test_SW_CardHit"


def _call(command, **params):
    from unreal_mcp_server import get_unreal_connection
    conn = get_unreal_connection()
    if conn is None:
        raise RuntimeError("No UE connection")
    raw = conn.send_command(command, params) or {}
    if isinstance(raw, dict) and "result" in raw:
        return raw["result"]
    return raw


def _make_minimal_wav(path: str) -> None:
    """Write a valid but silent 1-second 44100 Hz mono WAV file."""
    sample_rate = 44100
    num_samples = sample_rate  # 1 second
    data = b"\x00\x00" * num_samples  # 16-bit silent samples

    with open(path, "wb") as f:
        data_size = len(data)
        # RIFF header
        f.write(b"RIFF")
        f.write(struct.pack("<I", 36 + data_size))
        f.write(b"WAVE")
        # fmt chunk
        f.write(b"fmt ")
        f.write(struct.pack("<I", 16))       # chunk size
        f.write(struct.pack("<H", 1))        # PCM
        f.write(struct.pack("<H", 1))        # mono
        f.write(struct.pack("<I", sample_rate))
        f.write(struct.pack("<I", sample_rate * 2))  # byte rate
        f.write(struct.pack("<H", 2))        # block align
        f.write(struct.pack("<H", 16))       # bits per sample
        # data chunk
        f.write(b"data")
        f.write(struct.pack("<I", data_size))
        f.write(data)


def run() -> dict:
    errors = []
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        wav_path = f.name

    try:
        _make_minimal_wav(wav_path)

        # 1. Import SoundWave
        r = _call("import_sound_wave", wavPath=wav_path, assetPath=TEST_ASSET, ifExists="overwrite")
        if not r.get("ok"):
            return fail("test", "SW_IMPORT_FAILED", str(r), test="test_soundwave")

        # 2. Verify asset exists on disk
        exists_r = _call("asset_exists", assetPath=TEST_ASSET)
        if not exists_r.get("meta", {}).get("exists"):
            errors.append("SoundWave asset not found after import")

        # 3. Skip idempotency
        r2 = _call("import_sound_wave", wavPath=wav_path, assetPath=TEST_ASSET, ifExists="skip")
        if not r2.get("ok"):
            errors.append(f"skip retry failed: {r2}")
        if r2.get("status") != "skipped":
            errors.append(f"Expected status=skipped, got {r2.get('status')}")

    finally:
        os.unlink(wav_path)
        _call("delete_asset", assetPath=TEST_ASSET, ifMissing="skip")

    if errors:
        return fail("test", "ASSERTIONS_FAILED", "; ".join(errors), test="test_soundwave")
    return ok("created", "test-report", name="test_soundwave", duration_ms=0)
