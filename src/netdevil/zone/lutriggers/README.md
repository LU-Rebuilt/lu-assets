# LU Triggers Format

**Extension:** `.lutriggers`
**Used by:** Zone trigger volumes in the LEGO Universe client to define enter/exit events and their associated commands

## Overview

LU Triggers files are XML documents that define trigger volumes and their event handlers. When a player enters, exits, or interacts with a trigger volume, the associated commands execute (updating missions, playing effects, etc.).

## XML Structure

```xml
<triggers nextID="5">
  <trigger id="4" enabled="1">
    <event id="OnEnter">
      <command id="updateMission" target="target" args="exploretask,1,1,1,AG_POI_RESEARCH" />
    </event>
  </trigger>
</triggers>
```

### Elements

- `<triggers>` — root element
  - `nextID` (u32): next available trigger ID for the editor
- `<trigger>` — a single trigger definition
  - `id` (u32): unique trigger identifier
  - `enabled` (bool): 1 = active, 0 = disabled
- `<event>` — an event handler on a trigger
  - `id` (string): event type (e.g. "OnEnter", "OnExit", "OnCreate", "OnDestroy")
- `<command>` — an action executed when the event fires
  - `id` (string): command type (e.g. "updateMission", "PlayEffect", "addBuff")
  - `target` (string): command target identifier
  - `args` (string): comma-separated command arguments

## Key Details

- Standard XML format (not binary)
- Referenced by trigger volume objects in LVL scene files via LDF config
- Command types correspond to server-side script actions
- Multiple commands per event and multiple events per trigger are supported

## References

- DarkflameServer (github.com/DarkflameUniverse/DarkflameServer) — trigger command handling
- LUDevNet/Docs (github.com/LUDevNet/Docs) — trigger event/command documentation
