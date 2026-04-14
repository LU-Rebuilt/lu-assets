# AUD (Zone Audio Configuration) Format

**Extension:** `.aud`
**Used by:** The LEGO Universe client to configure audio parameters for individual scenes/areas within a zone

## Overview

AUD files are single-element XML documents that define the audio environment for a scene, including FMOD music cue names, 2D/3D sound GUIDs, and boredom timing for ambient variety.

## XML Structure

```xml
<SceneAudioAttributes
  musicCue="NT_Hallway"
  musicParamName=""
  guid2D="{GUID-STRING}"
  guid3D=""
  groupName=""
  programName=""
  musicParamValue="0"
  boredomTime="-1" />
```

### Attributes

| Attribute         | Type   | Description                                       |
|-------------------|--------|---------------------------------------------------|
| musicCue          | string | FMOD music cue name for this scene                |
| musicParamName    | string | FMOD parameter name for music control             |
| guid2D            | string | GUID for 2D ambient sound event                   |
| guid3D            | string | GUID for 3D positional sound event                |
| groupName         | string | FMOD event group name                             |
| programName       | string | FMOD program/project name                         |
| musicParamValue   | f32    | Default value for the music parameter             |
| boredomTime       | f32    | Seconds before ambient variety triggers (-1 = off)|

## Version

No versioning -- single-element XML format with no version attribute. A single format is used by all LU client files.

## Key Details

- Standard XML format (not binary)
- Each `.aud` file corresponds to one scene in the zone
- Referenced by the scene's audio layer in the LUZ file
- GUIDs reference FMOD events defined in `.fev` files
- boredomTime of -1 disables the boredom/variety system

## References

- FMOD Designer documentation (FMOD Event system)
- DarkflameServer (github.com/DarkflameUniverse/DarkflameServer)
