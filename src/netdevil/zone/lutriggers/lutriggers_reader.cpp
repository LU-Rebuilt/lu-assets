#include "netdevil/zone/lutriggers/lutriggers_reader.h"

#include <pugixml.hpp>

namespace lu::assets {

LuTriggersFile lutriggers_parse(std::span<const uint8_t> data) {
    pugi::xml_document doc;
    auto result = doc.load_buffer(data.data(), data.size());
    if (!result) {
        throw LuTriggersError("LuTriggers: XML parse error: " + std::string(result.description()));
    }

    auto root = doc.child("triggers");
    if (!root) {
        throw LuTriggersError("LuTriggers: missing root <triggers> element");
    }

    LuTriggersFile file;
    file.next_id = root.attribute("nextID").as_uint();

    for (auto trigger_node : root.children("trigger")) {
        LuTrigger trigger;
        trigger.id = trigger_node.attribute("id").as_uint();
        trigger.enabled = trigger_node.attribute("enabled").as_bool();

        for (auto event_node : trigger_node.children("event")) {
            LuTriggerEvent event;
            event.id = event_node.attribute("id").as_string();

            for (auto cmd_node : event_node.children("command")) {
                LuTriggerCommand cmd;
                cmd.id = cmd_node.attribute("id").as_string();
                cmd.target = cmd_node.attribute("target").as_string();
                cmd.args = cmd_node.attribute("args").as_string();
                event.commands.push_back(std::move(cmd));
            }

            trigger.events.push_back(std::move(event));
        }

        file.triggers.push_back(std::move(trigger));
    }

    return file;
}

} // namespace lu::assets
