/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2022                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/server/include/topics/shortcuttopic.h>

#include <modules/server/include/connection.h>
#include <openspace/engine/globals.h>
#include <openspace/interaction/actionmanager.h>
#include <openspace/interaction/keybindingmanager.h>

namespace {
    constexpr const char* EventKey = "event";
    constexpr const char* StartSubscription = "start_subscription";
    constexpr const char* StopSubscription = "stop_subscription";
} // namespace

using nlohmann::json;

namespace openspace {

bool ShortcutTopic::isDone() const {
    return true;
}

std::vector<nlohmann::json> ShortcutTopic::shortcutsJson() const {
    std::vector<nlohmann::json> json;
    for (const interaction::Action& action : global::actionManager->actions()) {
        nlohmann::json shortcutJson = {
            { "identifier", action.identifier },
            { "name", action.name },
            { "script", action.command },
            { "synchronization", static_cast<bool>(action.synchronization) },
            { "documentation", action.documentation },
            { "guiPath", action.guiPath },
        };
        json.push_back(shortcutJson);
    }

    const std::multimap<KeyWithModifier, std::string>& keyBindings =
        global::keybindingManager->keyBindings();

    for (const std::pair<const KeyWithModifier, std::string>& keyBinding : keyBindings) {
        const KeyWithModifier& k = keyBinding.first;
        // @TODO (abock, 2021-08-05) Probably this should be rewritten to better account
        // for the new action mechanism
        const interaction::Action& action = global::actionManager->action(
            keyBinding.second
        );

        nlohmann::json shortcutJson = {
            { "key", ghoul::to_string(k.key) },
            { "modifiers",
                {
                    { "shift" , hasKeyModifier(k.modifier, KeyModifier::Shift) },
                    { "control" , hasKeyModifier(k.modifier, KeyModifier::Control) },
                    { "alt" , hasKeyModifier(k.modifier, KeyModifier::Alt) },
                    { "super" , hasKeyModifier(k.modifier, KeyModifier::Super) }
                }
            },
            { "action", action.identifier },
        };
        json.push_back(shortcutJson);
    }
    return json;
}

void ShortcutTopic::sendData() const {
    nlohmann::json data = { {"shortcuts", shortcutsJson()} };
    _connection->sendJson(wrappedPayload(data));
}

void ShortcutTopic::handleJson(const nlohmann::json& input) {
    const std::string& event = input.at(EventKey).get<std::string>();
    if (event == StartSubscription) {
        // TODO: Subscribe to shortcuts and keybindings
        // shortcutManager.subscribe(); ...
    }
    else if (event == StopSubscription) {
        // TODO: Unsubscribe to shortcuts and keybindings
        // shortcutManager.unsubscribe(); ...
        return;
    }
    sendData();
}

} // namespace openspace
