# SCM (Script Command Macro) Format

**Extension:** `.scm`
**Used by:** The LEGO Universe client for GM macros and test scripts

## Overview

SCM files are plain-text lists of slash commands, one per line. Used by game masters and developers for scripted command sequences.

## Text Layout

```
/gmadditem 3
/teleport 100 200 300
/setinventorysize 100
```

## Key Details

- Plain text format, one slash command per line
- No header, no magic number
- Each line is a complete client slash command
- Commands are executed sequentially

## References

- DarkflameServer (github.com/DarkflameUniverse/DarkflameServer) — slash command handling
