#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// LU Triggers XML parser.
// Zone trigger volumes reference XML files that define enter/exit/etc events
// and the commands those events execute (update missions, play effects, etc).
//
// Format:
//   <triggers nextID="5">
//     <trigger id="4" enabled="1">
//       <event id="OnEnter">
//         <command id="updateMission" target="target" args="exploretask,1,1,1,AG_POI_RESEARCH" />
//       </event>
//     </trigger>
//   </triggers>

struct LuTriggersError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct LuTriggerCommand {
    std::string id;
    std::string target;
    std::string args;
};

struct LuTriggerEvent {
    std::string id;
    std::vector<LuTriggerCommand> commands;
};

struct LuTrigger {
    uint32_t id = 0;
    bool enabled = false;
    std::vector<LuTriggerEvent> events;
};

struct LuTriggersFile {
    // The original file bytes, verbatim. XML formatting (whitespace, attribute order,
    // quoting) has no canonical binary form, so byte-identical round-trips carry the
    // source bytes; lutriggers_write emits these. Structured edits must regenerate the XML
    // and cannot be byte-identical to the original file.
    std::vector<uint8_t> raw;

    uint32_t next_id = 0;
    std::vector<LuTrigger> triggers;
};
} // namespace lu::assets
