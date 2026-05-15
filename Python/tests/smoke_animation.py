"""Smoke test: Animation Blueprint authoring.

Run:
    python -m tests.smoke_animation                                # uses placeholder skeleton path
    python -m tests.smoke_animation --skeleton /Game/Foo/SK_Bar   # supply a real skeleton path
    python -m tests.smoke_animation --no-cleanup                  # alias — no cleanup is performed

The placeholder skeleton path ("/Game/Smoke/SK_Placeholder") almost certainly
does NOT exist in your project; operators should pass --skeleton pointing at a
real Skeleton asset, otherwise step 2 will fail with a NOT_FOUND error and the
test will exit 1.

Steps:
    1. create_animation_blueprint(ABP_SmokeTest, skeleton, /Game/Smoke)
    2. add_state_machine(MainSM)

Exit 0 on success, 1 on any failure.
"""

from __future__ import annotations

import sys
from typing import Any, Dict

from tests._smoke_common import (
    assert_success,
    parse_no_cleanup,
    run_steps,
    send_command,
)

ANIM_BP_NAME = "ABP_SmokeTest"
ANIM_BP_FOLDER = "/Game/Smoke"
DEFAULT_SKELETON_PATH = "/Game/Smoke/SK_Placeholder"  # юзер заменит на свой


def _parse_skeleton(argv: list[str]) -> str:
    for i, arg in enumerate(argv[1:], start=1):
        if arg == "--skeleton" and i + 1 < len(argv):
            return argv[i + 1]
        if arg.startswith("--skeleton="):
            return arg.split("=", 1)[1]
    return DEFAULT_SKELETON_PATH


def main(argv: list[str]) -> int:
    _no_cleanup = parse_no_cleanup(argv)  # noqa: F841
    skeleton_path = _parse_skeleton(argv)
    print(f"using skeleton: {skeleton_path}")

    def step_create():
        resp = send_command("create_animation_blueprint", {
            "name": ANIM_BP_NAME,
            "skeleton_path": skeleton_path,
            "folder_path": ANIM_BP_FOLDER,
        })
        assert_success(resp, 1, "create_animation_blueprint")

    def step_state_machine():
        resp = send_command("add_state_machine", {
            "blueprint_name": ANIM_BP_NAME,
            "state_machine_name": "MainSM",
        })
        assert_success(resp, 2, "add_state_machine")

    steps = [
        ("create_animation_blueprint", step_create),
        ("add_state_machine",          step_state_machine),
    ]
    return run_steps("smoke_animation", steps)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
