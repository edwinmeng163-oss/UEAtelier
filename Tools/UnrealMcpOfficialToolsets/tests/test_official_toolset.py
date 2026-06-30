from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import types
import unittest


ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = ROOT.parents[1]
MODULE_PATH = ROOT / "ueatelier_official_toolset.py"


class FakeToolsetRegistry:
    available = True
    registered: list[type] = []

    @classmethod
    def reset(cls) -> None:
        cls.available = True
        cls.registered = []

    @classmethod
    def is_available(cls) -> bool:
        return cls.available

    @classmethod
    def register_toolset_class(cls, toolset_class: type) -> None:
        cls.registered.append(toolset_class)

    @classmethod
    def unregister_toolset_class(cls, toolset_class: type) -> None:
        cls.registered = [item for item in cls.registered if item is not toolset_class]

    @classmethod
    def execute_tool(cls, toolset_name: str, tool_name: str, json_input: str) -> str:
        _ = json.loads(json_input or "{}")
        for toolset_class in cls.registered:
            candidate_name = f"{toolset_class.__module__}.{toolset_class.__name__}"
            if candidate_name == toolset_name:
                return getattr(toolset_class, tool_name)()
        raise AssertionError(f"toolset not registered: {toolset_name}")


class FakeUnrealMcpCallTool:
    calls: list[tuple[str, str]] = []

    @classmethod
    def reset(cls) -> None:
        cls.calls = []

    @classmethod
    def call_tool(cls, tool_name: str, arguments_json: str) -> str:
        cls.calls.append((tool_name, arguments_json))
        return json.dumps(
            {
                "toolName": tool_name,
                "isError": False,
                "structuredContent": {"engineVersion": "test"},
                "meta": {
                    "policyDecision": "allow",
                },
            }
        )


class FakeRegistration:
    def __init__(self, toolset_classes):
        self._toolset_classes = tuple(toolset_classes)

    def register(self) -> bool:
        if not FakeToolsetRegistry.is_available():
            return False
        for toolset_class in self._toolset_classes:
            FakeToolsetRegistry.register_toolset_class(toolset_class)
        return True

    def unregister(self) -> None:
        for toolset_class in self._toolset_classes:
            FakeToolsetRegistry.unregister_toolset_class(toolset_class)


def _install_fake_unreal_modules() -> None:
    unreal = types.ModuleType("unreal")

    def uclass():
        def decorate(cls):
            return cls

        return decorate

    unreal.uclass = uclass
    unreal.ToolsetDefinition = type("ToolsetDefinition", (), {})
    unreal.UnrealMcpCallTool = FakeUnrealMcpCallTool
    unreal.ToolsetRegistry = FakeToolsetRegistry

    toolset_registry = types.ModuleType("toolset_registry")

    def tool_call(func):
        return func

    toolset_registry.tool_call = tool_call

    registration = types.ModuleType("toolset_registry.registration")
    registration.Registration = FakeRegistration

    sys.modules["unreal"] = unreal
    sys.modules["toolset_registry"] = toolset_registry
    sys.modules["toolset_registry.registration"] = registration


def _load_official_toolset():
    module_name = "ueatelier_official_toolset_test_target"
    sys.modules.pop(module_name, None)
    spec = importlib.util.spec_from_file_location(module_name, MODULE_PATH)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


class OfficialToolsetTests(unittest.TestCase):
    def setUp(self) -> None:
        FakeToolsetRegistry.reset()
        FakeUnrealMcpCallTool.reset()
        _install_fake_unreal_modules()

    def test_official_registry_call_delegates_to_governed_call_tool(self) -> None:
        module = _load_official_toolset()

        self.assertTrue(module.register())
        toolset_name = f"{module.__name__}.UEAtelierOfficialToolset"
        result_json = FakeToolsetRegistry.execute_tool(toolset_name, "editor_status", "{}")

        self.assertEqual(FakeUnrealMcpCallTool.calls, [("unreal.editor_status", "{}")])
        result = json.loads(result_json)
        self.assertFalse(result["isError"])
        self.assertEqual(result["meta"]["policyDecision"], "allow")
        self.assertNotIn("activityLog", result["structuredContent"])

    def test_call_tool_library_source_writes_activity_log_at_governed_seam(self) -> None:
        source_path = (
            REPO_ROOT
            / "Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpCallToolLibrary.cpp"
        )
        source = source_path.read_text(encoding="utf-8")

        execute_index = source.index("Module->ExecuteToolFromEditorUI")
        emit_index = source.index("EmitActivityLogEventForKnownTool(", execute_index)

        self.assertLess(execute_index, emit_index)
        self.assertIn("UnrealMcp::WriteActivityEvent(Event);", source)
        self.assertIn("Event.EventKind = TEXT(\"tool_call\");", source)
        self.assertIn("UnrealMcp::FindToolRegistryEntry(ToolName)", source)
        self.assertIn("UnrealMcp::UserRegistry::FindUserTool(ToolName)", source)
        self.assertIn("CaptureRedaction::AttachCaptureMetadata", source)

    def test_validator_accepts_the_delegating_wrapper(self) -> None:
        validator_path = ROOT / "validate_official_toolset.py"
        spec = importlib.util.spec_from_file_location("validate_official_toolset_test_target", validator_path)
        assert spec and spec.loader
        validator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(validator)

        issues = validator.validate_file(MODULE_PATH)

        self.assertEqual(issues, [])

    def test_validator_rejects_direct_editor_api_calls(self) -> None:
        validator_path = ROOT / "validate_official_toolset.py"
        spec = importlib.util.spec_from_file_location("validate_official_toolset_test_target", validator_path)
        assert spec and spec.loader
        validator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(validator)

        issues = validator.validate_source(
            """
import unreal

def bypass():
    return unreal.EditorLevelLibrary.spawn_actor_from_class(None, [0, 0, 0])
""",
            source_name="bad_toolset.py",
        )

        joined = "\n".join(issues)
        self.assertIn("direct_unreal_call", joined)
        self.assertIn("EditorLevelLibrary.spawn_actor_from_class", joined)


if __name__ == "__main__":
    unittest.main()
