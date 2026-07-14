# unreal.configure_player_input

**Category**: editor
**Title**: Configure Player Input
**Risk level**: medium

Configures standard and arbitrary legacy player-control input mappings for legacy input or an Enhanced Input mapping context, defaulting to dry-run diagnostics.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "inputSystem": {
      "type": "string",
      "description": "Input stack to configure.",
      "default": "auto",
      "enum": [
        "auto",
        "legacy",
        "enhanced"
      ]
    },
    "profile": {
      "type": "string",
      "description": "Mapping profile to use before applying mappings overrides.",
      "default": "third_person_basic",
      "enum": [
        "third_person_basic",
        "custom"
      ]
    },
    "mappings": {
      "type": "object",
      "properties": {
        "MoveForward": {
          "type": "object",
          "properties": {
            "kind": {
              "type": "string",
              "description": "Legacy mapping kind. Use action for button-style events and axis for scaled continuous input.",
              "default": "axis",
              "enum": [
                "axis",
                "action"
              ]
            },
            "keys": {
              "type": "array",
              "description": "Keys to bind for this named mapping.",
              "items": {
                "type": "object",
                "properties": {
                  "key": {
                    "type": "string",
                    "description": "Input key name such as W, S, A, D, MouseX, MouseY, or SpaceBar."
                  },
                  "scale": {
                    "type": "number",
                    "description": "Axis scale for this key. Ignored for action mappings.",
                    "default": 1
                  }
                },
                "required": [
                  "key"
                ],
                "additionalProperties": false
              }
            },
            "inputActionPath": {
              "type": "string",
              "description": "Optional Enhanced Input UInputAction asset path used when inputSystem is enhanced."
            }
          },
          "required": [],
          "additionalProperties": false
        },
        "MoveRight": {
          "type": "object",
          "properties": {
            "kind": {
              "type": "string",
              "description": "Legacy mapping kind. Use action for button-style events and axis for scaled continuous input.",
              "default": "axis",
              "enum": [
                "axis",
                "action"
              ]
            },
            "keys": {
              "type": "array",
              "description": "Keys to bind for this named mapping.",
              "items": {
                "type": "object",
                "properties": {
                  "key": {
                    "type": "string",
                    "description": "Input key name such as W, S, A, D, MouseX, MouseY, or SpaceBar."
                  },
                  "scale": {
                    "type": "number",
                    "description": "Axis scale for this key. Ignored for action mappings.",
                    "default": 1
                  }
                },
                "required": [
                  "key"
                ],
                "additionalProperties": false
              }
            },
            "inputActionPath": {
              "type": "string",
              "description": "Optional Enhanced Input UInputAction asset path used when inputSystem is enhanced."
            }
          },
          "required": [],
          "additionalProperties": false
        },
        "LookYaw": {
          "type": "object",
          "properties": {
            "kind": {
              "type": "string",
              "description": "Legacy mapping kind. Use action for button-style events and axis for scaled continuous input.",
              "default": "axis",
              "enum": [
                "axis",
                "action"
              ]
            },
            "keys": {
              "type": "array",
              "description": "Keys to bind for this named mapping.",
              "items": {
                "type": "object",
                "properties": {
                  "key": {
                    "type": "string",
                    "description": "Input key name such as W, S, A, D, MouseX, MouseY, or SpaceBar."
                  },
                  "scale": {
                    "type": "number",
                    "description": "Axis scale for this key. Ignored for action mappings.",
                    "default": 1
                  }
                },
                "required": [
                  "key"
                ],
                "additionalProperties": false
              }
            },
            "inputActionPath": {
              "type": "string",
              "description": "Optional Enhanced Input UInputAction asset path used when inputSystem is enhanced."
            }
          },
          "required": [],
          "additionalProperties": false
        },
        "LookPitch": {
          "type": "object",
          "properties": {
            "kind": {
              "type": "string",
              "description": "Legacy mapping kind. Use action for button-style events and axis for scaled continuous input.",
              "default": "axis",
              "enum": [
                "axis",
                "action"
              ]
            },
            "keys": {
              "type": "array",
              "description": "Keys to bind for this named mapping.",
              "items": {
                "type": "object",
                "properties": {
                  "key": {
                    "type": "string",
                    "description": "Input key name such as W, S, A, D, MouseX, MouseY, or SpaceBar."
                  },
                  "scale": {
                    "type": "number",
                    "description": "Axis scale for this key. Ignored for action mappings.",
                    "default": 1
                  }
                },
                "required": [
                  "key"
                ],
                "additionalProperties": false
              }
            },
            "inputActionPath": {
              "type": "string",
              "description": "Optional Enhanced Input UInputAction asset path used when inputSystem is enhanced."
            }
          },
          "required": [],
          "additionalProperties": false
        },
        "Jump": {
          "type": "object",
          "properties": {
            "kind": {
              "type": "string",
              "description": "Legacy mapping kind. Use action for button-style events and axis for scaled continuous input.",
              "default": "axis",
              "enum": [
                "axis",
                "action"
              ]
            },
            "keys": {
              "type": "array",
              "description": "Keys to bind for this named mapping.",
              "items": {
                "type": "object",
                "properties": {
                  "key": {
                    "type": "string",
                    "description": "Input key name such as W, S, A, D, MouseX, MouseY, or SpaceBar."
                  },
                  "scale": {
                    "type": "number",
                    "description": "Axis scale for this key. Ignored for action mappings.",
                    "default": 1
                  }
                },
                "required": [
                  "key"
                ],
                "additionalProperties": false
              }
            },
            "inputActionPath": {
              "type": "string",
              "description": "Optional Enhanced Input UInputAction asset path used when inputSystem is enhanced."
            }
          },
          "required": [],
          "additionalProperties": false
        }
      },
      "additionalProperties": {
        "type": "object",
        "properties": {
          "kind": {
            "type": "string",
            "description": "Legacy mapping kind. Use action for button-style events and axis for scaled continuous input.",
            "default": "axis",
            "enum": [
              "axis",
              "action"
            ]
          },
          "keys": {
            "type": "array",
            "description": "Keys to bind for this named mapping.",
            "items": {
              "type": "object",
              "properties": {
                "key": {
                  "type": "string",
                  "description": "Input key name such as W, S, A, D, MouseX, MouseY, or SpaceBar."
                },
                "scale": {
                  "type": "number",
                  "description": "Axis scale for this key. Ignored for action mappings.",
                  "default": 1
                }
              },
              "required": [
                "key"
              ],
              "additionalProperties": false
            }
          },
          "inputActionPath": {
            "type": "string",
            "description": "Optional Enhanced Input UInputAction asset path used when inputSystem is enhanced."
          }
        },
        "required": [],
        "additionalProperties": false
      },
      "description": "Optional overrides for standard player-control mappings plus arbitrary legacy axis/action mapping names."
    },
    "enhancedInputMappingContextPath": {
      "type": "string",
      "description": "Optional Enhanced Input UInputMappingContext asset path."
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview intended input config writes without mutating settings or mapping assets.",
      "default": true
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "inputSystem": "legacy",
  "profile": "third_person_basic"
}
```

## Provenance
- Source docs: Tools/UnrealMcpToolDocs/editor/configure_player_input.md
- Reason: v0.27.1 core gameplay setup primitive for reusable Legacy/Enhanced Input configuration under dry-run and postcheck evidence.
- Notes: inputSystem=auto configures Enhanced Input only when enhancedInputMappingContextPath is supplied and Enhanced Input classes are available; otherwise it uses legacy UInputSettings.
