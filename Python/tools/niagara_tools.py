"""
Niagara Tools for Unreal MCP.

Wrappers for Niagara system primitives:
  - copy_niagara_system, set_niagara_parameters            (v1.17.0)
  - niagara_get_info, niagara_add_emitter,
    niagara_set_emitter_properties, niagara_set_module_input (v3.22.0)

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
Extended:   v3.22.0 (emitter graph introspection + authoring).
"""

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_niagara_tools(mcp: FastMCP) -> None:
    """Register Niagara tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def copy_niagara_system(
        ctx: Context[Any, Any, Any],
        sourcePath: str,
        destPath: str,
        ifExists: str = None,
    ) -> dict[str, Any]:
        """
        Duplicate a UNiagaraSystem to a new /Game/... path.

        Args:
            sourcePath: Full /Game/... path to an existing UNiagaraSystem.
            destPath: Full /Game/... path for the duplicated asset.
            ifExists: Idempotency mode ("skip" | "fail" | "overwrite").
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {
                "sourcePath": sourcePath,
                "destPath": destPath,
            }
            if ifExists is not None:
                params["ifExists"] = ifExists
            logger.info(f"Copying Niagara system: {params}")
            response = unreal.send_command("copy_niagara_system", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error copying Niagara system: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_niagara_parameters(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        params: dict[str, Any],
    ) -> dict[str, Any]:
        """
        Set exposed user parameters on a UNiagaraSystem. Each key is the
        parameter name (with or without "User." prefix; the C++ side normalizes).

        Args:
            assetPath: Full /Game/... path of an existing UNiagaraSystem.
            params: Dict of parameter name → value. Supported value types:
                bool, int/float (number), and vectors as [x,y,z] or [x,y,z,w].
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload = {
                "assetPath": assetPath,
                "params": params,
            }
            logger.info(f"Setting Niagara parameters: assetPath={assetPath}")
            response = unreal.send_command("set_niagara_parameters", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting Niagara parameters: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ── v3.22.0 — emitter graph introspection + authoring ────────────────────

    @mcp.tool()
    def niagara_get_info(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        includeModuleInputs: bool = False,
    ) -> dict[str, Any]:
        """
        Inspect a UNiagaraSystem: emitters, modules, renderers, loop settings.

        Read-only. Use this BEFORE assuming a system works — an empty system
        (0 emitters) still returns a valid component from
        UNiagaraFunctionLibrary::SpawnSystemAtLocation and logs nothing, so the
        only reliable "is this effect real?" check is meta.isEmpty /
        meta.warnings from this command.

        Args:
            assetPath: Full /Game/... path of the UNiagaraSystem.
                       Example: "/Game/Effects/Cards/NS_SmokeCloud"
            includeModuleInputs: When True, also dump every rapid-iteration
                       parameter (name/type/value) per emitter. Those names are
                       exactly what niagara_set_module_input accepts.

        Returns meta:
            emitterCount, enabledEmitterCount, totalModuleCount,
            totalRendererCount, isEmpty, warmupTime, warnings[],
            exposedParameters[], emitters[] (each with name, enabled, localSpace,
            simTarget, boundsMode, renderers[], modules{...}, loopBehavior,
            loopDuration, and moduleInputs[] when includeModuleInputs=True).

        Example:
            niagara_get_info(assetPath="/Game/Effects/Cards/NS_SmokeCloud")
            niagara_get_info(assetPath="/Game/Effects/Death/NS_DeathSmoke",
                             includeModuleInputs=True)
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload: dict[str, Any] = {
                "assetPath": assetPath,
                "includeModuleInputs": includeModuleInputs,
            }
            logger.info(f"Reading Niagara info: {assetPath}")
            response = unreal.send_command("niagara_get_info", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error reading Niagara info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def niagara_add_emitter(
        ctx: Context[Any, Any, Any],
        systemPath: str,
        emitterAssetPath: str,
        emitterName: str = "",
        compile: bool = True,
    ) -> dict[str, Any]:
        """
        Add an emitter asset (UNiagaraEmitter) into an existing UNiagaraSystem.

        The plugin verifies the emitter-handle count actually increased and
        fails with NIAGARA_ADD_EMITTER_FAILED otherwise — a success response
        always means the emitter is really in the system and saved to disk.

        Args:
            systemPath: Full /Game/... path of the UNiagaraSystem to modify.
            emitterAssetPath: Full path of a UNiagaraEmitter asset
                       ("/Game/Effects/Emitters/FX_Smoke" or an engine one).
            emitterName: Optional display name for the new emitter handle.
                       Empty string keeps the auto-generated unique name.
            compile: RequestCompile + wait after adding (default True).

        Example:
            niagara_add_emitter(systemPath="/Game/Effects/Cards/NS_SmokeCloud",
                                emitterAssetPath="/Game/Effects/Emitters/FX_SmokeBase",
                                emitterName="SmokeCloud")
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload: dict[str, Any] = {
                "systemPath": systemPath,
                "emitterAssetPath": emitterAssetPath,
                "compile": compile,
            }
            if emitterName:
                payload["emitterName"] = emitterName
            logger.info(f"Adding Niagara emitter: {payload}")
            response = unreal.send_command("niagara_add_emitter", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error adding Niagara emitter: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def niagara_set_emitter_properties(
        ctx: Context[Any, Any, Any],
        systemPath: str,
        emitterName: str,
        enabled: bool = None,
        localSpace: bool = None,
        simTarget: str = "",
        determinism: bool = None,
        randomSeed: int = None,
        boundsMode: str = "",
        fixedBounds: list[float] = None,
        loopBehavior: str = "",
        loopDuration: float = None,
        loopCount: int = None,
        compile: bool = True,
    ) -> dict[str, Any]:
        """
        Set properties on one emitter inside a UNiagaraSystem.

        Turning a one-shot burst into a standing cloud = loopBehavior="Infinite".
        Loop settings are routed to the emitter's "Emitter State" module; if that
        module is absent the call fails with NIAGARA_PROPERTY_NOT_APPLIED rather
        than reporting a fake success.

        Args:
            systemPath: Full /Game/... path of the UNiagaraSystem.
            emitterName: Emitter handle display name (case/space-insensitive).
            enabled: Enable/disable the emitter within the system.
            localSpace: True = particles follow the component.
            simTarget: "CPU" or "GPU".
            determinism: Deterministic RNG for this emitter.
            randomSeed: Seed used when determinism is True.
            boundsMode: "Dynamic" | "Fixed" | "Programmatic".
            fixedBounds: [minX, minY, minZ, maxX, maxY, maxZ] in cm.
            loopBehavior: "Once" | "Multiple" | "Infinite".
            loopDuration: Seconds per loop.
            loopCount: Loop count when loopBehavior="Multiple".
            compile: RequestCompile + wait after applying (default True).

        Example:
            niagara_set_emitter_properties(
                systemPath="/Game/Effects/Cards/NS_SmokeCloud",
                emitterName="SmokeCloud",
                loopBehavior="Infinite", localSpace=False, simTarget="GPU")
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload: dict[str, Any] = {
                "systemPath": systemPath,
                "emitterName": emitterName,
                "compile": compile,
            }
            if enabled is not None:
                payload["enabled"] = enabled
            if localSpace is not None:
                payload["localSpace"] = localSpace
            if simTarget:
                payload["simTarget"] = simTarget
            if determinism is not None:
                payload["determinism"] = determinism
            if randomSeed is not None:
                payload["randomSeed"] = randomSeed
            if boundsMode:
                payload["boundsMode"] = boundsMode
            if fixedBounds is not None:
                payload["fixedBounds"] = fixedBounds
            if loopBehavior:
                payload["loopBehavior"] = loopBehavior
            if loopDuration is not None:
                payload["loopDuration"] = loopDuration
            if loopCount is not None:
                payload["loopCount"] = loopCount

            if len(payload) <= 3:
                return {
                    "success": False,
                    "message": ("No property supplied. Provide at least one of: enabled, localSpace, "
                                "simTarget, determinism, randomSeed, boundsMode, fixedBounds, "
                                "loopBehavior, loopDuration, loopCount"),
                }

            logger.info(f"Setting Niagara emitter properties: {payload}")
            response = unreal.send_command("niagara_set_emitter_properties", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting Niagara emitter properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def niagara_set_module_input(
        ctx: Context[Any, Any, Any],
        systemPath: str,
        emitterName: str,
        inputName: str,
        moduleName: str = "",
        numberValue: float = None,
        boolValue: bool = None,
        stringValue: str = "",
        vectorValue: list[float] = None,
        compile: bool = True,
    ) -> dict[str, Any]:
        """
        Write a single module input on an emitter (SpawnRate, Lifetime, Color…).

        Exactly ONE of numberValue / boolValue / stringValue / vectorValue must
        be supplied — they exist instead of a bare `value: Any` so the tool
        schema stays typed.

        Resolution order inside the plugin: rapid-iteration parameters first,
        then static-switch pins on the module node. When neither matches, the
        error payload carries details.availableModules and
        details.availableInputs so the next call can use a real name instead of
        guessing. Discover names up-front with
        niagara_get_info(includeModuleInputs=True).

        Args:
            systemPath: Full /Game/... path of the UNiagaraSystem.
            emitterName: Emitter handle display name.
            inputName: Input name, spaces/case ignored ("SpawnRate",
                       "Lifetime", "Loop Behavior").
            moduleName: Optional module filter ("SpawnRate", "Emitter State",
                       "Initialize Particle"). Empty = search every module.
            numberValue: float/int inputs.
            boolValue: bool inputs.
            stringValue: enum inputs, by entry name ("Infinite").
            vectorValue: [x,y] / [x,y,z] / [r,g,b,a] inputs.
            compile: RequestCompile + wait after writing (default True).

        Examples:
            niagara_set_module_input(systemPath="/Game/Effects/Cards/NS_SmokeCloud",
                                     emitterName="SmokeCloud",
                                     moduleName="SpawnRate", inputName="SpawnRate",
                                     numberValue=15.0)
            niagara_set_module_input(systemPath="/Game/Effects/Cards/NS_SmokeCloud",
                                     emitterName="SmokeCloud",
                                     inputName="Lifetime", numberValue=2.5)
            niagara_set_module_input(systemPath="/Game/Effects/Cards/NS_SmokeCloud",
                                     emitterName="SmokeCloud",
                                     moduleName="Initialize Particle", inputName="Color",
                                     vectorValue=[0.498, 0.549, 0.553, 1.0])
        """
        from unreal_mcp_server import get_unreal_connection

        supplied: list[tuple[str, Any]] = [
            ("numberValue", numberValue),
            ("boolValue", boolValue),
            ("vectorValue", vectorValue),
        ]
        chosen: list[tuple[str, Any]] = [(k, v) for k, v in supplied if v is not None]
        if stringValue:
            chosen.append(("stringValue", stringValue))
        if len(chosen) != 1:
            return {
                "success": False,
                "message": ("Supply exactly one of numberValue / boolValue / stringValue / "
                            f"vectorValue (got {len(chosen)}: {[k for k, _ in chosen]})"),
            }

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload: dict[str, Any] = {
                "systemPath": systemPath,
                "emitterName": emitterName,
                "inputName": inputName,
                "value": chosen[0][1],
                "compile": compile,
            }
            if moduleName:
                payload["moduleName"] = moduleName
            logger.info(f"Setting Niagara module input: {payload}")
            response = unreal.send_command("niagara_set_module_input", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting Niagara module input: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Niagara tools registered successfully")
