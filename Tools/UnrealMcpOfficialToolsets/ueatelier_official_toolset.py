"""UE 5.8+ official ToolsetRegistry wrappers for governed UEAtelier tools."""

from __future__ import annotations

from toolset_registry.registration import Registration
import toolset_registry
import unreal  # type: ignore[import-not-found]


UEATELIER_TOOLSET_VERSION = "0.1"


@unreal.uclass()
class UEAtelierOfficialToolset(unreal.ToolsetDefinition):
    """Official MCP discovery wrapper whose execution delegates to UEAtelier."""

    def get_toolset_version(self) -> str:
        return UEATELIER_TOOLSET_VERSION

    @toolset_registry.tool_call
    @staticmethod
    def editor_status() -> str:
        """Return the governed UEAtelier editor status result."""
        return unreal.UnrealMcpCallTool.call_tool("unreal.editor_status", "{}")


REGISTRATION = Registration((UEAtelierOfficialToolset,))


def register() -> bool:
    """Register the UEAtelier official toolset with Unreal's ToolsetRegistry."""
    return REGISTRATION.register()


def unregister() -> None:
    """Unregister the UEAtelier official toolset."""
    REGISTRATION.unregister()
