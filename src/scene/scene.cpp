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

#include <openspace/scene/scene.h>

#include <openspace/camera/camera.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/globalscallbacks.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/interaction/sessionrecording.h>
#include <openspace/query/query.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/scene/scenelicensewriter.h>
#include <openspace/scene/sceneinitializer.h>
#include <openspace/scripting/lualibrary.h>
#include <openspace/scripting/scriptengine.h>
#include <openspace/util/updatestructures.h>
#include <ghoul/opengl/programobject.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/misc.h>
#include <ghoul/misc/profiling.h>
#include <string>
#include <stack>

#include "scene_lua.inl"

namespace {
    constexpr const char* _loggerCat = "Scene";
    constexpr const char* KeyIdentifier = "Identifier";
    constexpr const char* KeyParent = "Parent";

#ifdef TRACY_ENABLE
    constexpr const char* renderBinToString(int renderBin) {
        // Synced with Renderable::RenderBin
        if (renderBin == 1) {
            return "Background";
        }
        else if (renderBin == 2) {
            return "Opaque";
        }
        else if (renderBin == 4) {
            return "PreDeferredTransparent";
        }
        else if (renderBin == 8) {
            return "PostDeferredTransparent";
        }
        else if (renderBin == 16) {
            return "Overlay";
        }
        else {
            throw ghoul::MissingCaseException();
        }
    }
#endif // TRACY_ENABLE

    template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
    template <class... Ts> overloaded(Ts...)->overloaded<Ts...>;
} // namespace

namespace openspace {

Scene::InvalidSceneError::InvalidSceneError(std::string msg, std::string comp)
    : ghoul::RuntimeError(std::move(msg), std::move(comp))
{}

Scene::Scene(std::unique_ptr<SceneInitializer> initializer)
    : properties::PropertyOwner({"Scene", "Scene"})
    , _initializer(std::move(initializer))
{
    _rootDummy.setIdentifier(SceneGraphNode::RootNodeIdentifier);
    _rootDummy.setScene(this);
}

Scene::~Scene() {
    clear();
    _rootDummy.setScene(nullptr);
}

void Scene::attachNode(ghoul::mm_unique_ptr<SceneGraphNode> node) {
    _rootDummy.attachChild(std::move(node));
}

ghoul::mm_unique_ptr<SceneGraphNode> Scene::detachNode(SceneGraphNode& node) {
    return _rootDummy.detachChild(node);
}

void Scene::setCamera(std::unique_ptr<Camera> camera) {
    _camera = std::move(camera);
}

Camera* Scene::camera() const {
    return _camera.get();
}

void Scene::registerNode(SceneGraphNode* node) {
    if (_nodesByIdentifier.count(node->identifier())) {
        throw Scene::InvalidSceneError(
            "Node with identifier " + node->identifier() + " already exits."
        );
    }

    _topologicallySortedNodes.push_back(node);
    _nodesByIdentifier[node->identifier()] = node;
    addPropertySubOwner(node);
    _dirtyNodeRegistry = true;
    global::eventEngine->publishEvent<events::EventSceneGraphNodeAdded>(node);
}

void Scene::unregisterNode(SceneGraphNode* node) {
    _topologicallySortedNodes.erase(
        std::remove(
            _topologicallySortedNodes.begin(),
            _topologicallySortedNodes.end(),
            node
        ),
        _topologicallySortedNodes.end()
    );
    _nodesByIdentifier.erase(node->identifier());
    // Just try to remove all properties; if the property doesn't exist, the
    // removeInterpolation will not do anything
    for (properties::Property* p : node->properties()) {
        removePropertyInterpolation(p);
    }
    removePropertySubOwner(node);
    _dirtyNodeRegistry = true;
    global::eventEngine->publishEvent<events::EventSceneGraphNodeRemoved>(node);
}

void Scene::markNodeRegistryDirty() {
    _dirtyNodeRegistry = true;
}

void Scene::updateNodeRegistry() {
    ZoneScoped

    sortTopologically();
    _dirtyNodeRegistry = false;
}

void Scene::sortTopologically() {
    _topologicallySortedNodes.insert(
        _topologicallySortedNodes.end(),
        std::make_move_iterator(_circularNodes.begin()),
        std::make_move_iterator(_circularNodes.end())
    );
    _circularNodes.clear();

    ghoul_assert(
        _topologicallySortedNodes.size() == _nodesByIdentifier.size(),
        "Number of scene graph nodes is inconsistent"
    );

    if (_topologicallySortedNodes.empty()) {
        return;
    }

    // Only the Root node can have an in-degree of 0
    SceneGraphNode* root = _nodesByIdentifier[SceneGraphNode::RootNodeIdentifier];
    if (!root) {
        throw Scene::InvalidSceneError("No root node found");
    }

    std::unordered_map<SceneGraphNode*, size_t> inDegrees;
    for (SceneGraphNode* node : _topologicallySortedNodes) {
        size_t inDegree = node->dependencies().size();
        if (node->parent()) {
            inDegree++;
            inDegrees[node] = inDegree;
        }
    }

    std::stack<SceneGraphNode*> zeroInDegreeNodes;
    zeroInDegreeNodes.push(root);

    std::vector<SceneGraphNode*> nodes;
    nodes.reserve(_topologicallySortedNodes.size());
    while (!zeroInDegreeNodes.empty()) {
        SceneGraphNode* node = zeroInDegreeNodes.top();
        nodes.push_back(node);
        zeroInDegreeNodes.pop();

        for (SceneGraphNode* n : node->dependentNodes()) {
            const auto it = inDegrees.find(n);
            it->second -= 1;
            if (it->second == 0) {
                zeroInDegreeNodes.push(n);
                inDegrees.erase(it);
            }
        }
        for (SceneGraphNode* n : node->children()) {
            const auto it = inDegrees.find(n);
            it->second -= 1;
            if (it->second == 0) {
                zeroInDegreeNodes.push(n);
                inDegrees.erase(it);
            }
        }
    }
    if (!inDegrees.empty()) {
        LERROR(fmt::format(
            "The scene contains circular dependencies. {} nodes will be disabled",
            inDegrees.size()
        ));
    }

    for (const std::pair<SceneGraphNode* const, size_t>& it : inDegrees) {
        _circularNodes.push_back(it.first);
    }

    _topologicallySortedNodes = nodes;
}

void Scene::initializeNode(SceneGraphNode* node) {
    _initializer->initializeNode(node);
}

bool Scene::isInitializing() const {
    return _initializer->isInitializing();
}

/*
void Scene::initialize() {
    bool useMultipleThreads = true;
    if (OsEng.configurationManager().hasKey(
        ConfigurationManager::KeyUseMultithreadedInitialization
    ))
    {
        useMultipleThreads = OsEng.configurationManager().value<bool>(
            ConfigurationManager::KeyUseMultithreadedInitialization
        );
    }

    auto initFunction = [](SceneGraphNode* node){
        try {
            OsEng.loadingScreen().updateItem(
                node->name(),
                LoadingScreen::ItemStatus::Initializing
            );
            node->initialize();
            OsEng.loadingScreen().tickItem();
            OsEng.loadingScreen().updateItem(
                node->name(),
                LoadingScreen::ItemStatus::Finished
            );
        }
        catch (const ghoul::RuntimeError& e) {
            LERROR(node->name() << " not initialized.");
            LERRORC(std::string(_loggerCat) + "(" + e.component + ")", e.what());
            OsEng.loadingScreen().updateItem(
                node->name(),
                LoadingScreen::ItemStatus::Failed
            );
        }

    };

    if (useMultipleThreads) {
        unsigned int nThreads = std::thread::hardware_concurrency();

        ghoul::ThreadPool pool(nThreads == 0 ? 2 : nThreads - 1);

        OsEng.loadingScreen().postMessage("Initializing scene");

        for (SceneGraphNode* node : _topologicallySortedNodes) {
            pool.queue(initFunction, node);
        }

        pool.stop();
    }
    else {
        for (SceneGraphNode* node : _topologicallySortedNodes) {
            initFunction(node);
        }
    }
}

void Scene::initializeGL() {
    for (SceneGraphNode* node : _topologicallySortedNodes) {
        try {
            node->initializeGL();
        }
        catch (const ghoul::RuntimeError& e) {
            LERROR(node->name() << " not initialized.");
            LERRORC(std::string(_loggerCat) + "(" + e.component + ")", e.what());
        }
    }
}
*/

void Scene::update(const UpdateData& data) {
    ZoneScoped

    std::vector<SceneGraphNode*> initializedNodes = _initializer->takeInitializedNodes();
    for (SceneGraphNode* node : initializedNodes) {
        try {
            node->initializeGL();
        }
        catch (const ghoul::RuntimeError& e) {
            LERRORC(e.component, e.message);
        }
    }
    if (_dirtyNodeRegistry) {
        updateNodeRegistry();
    }
    for (SceneGraphNode* node : _topologicallySortedNodes) {
        try {
            node->update(data);
        }
        catch (const ghoul::RuntimeError& e) {
            LERRORC(e.component, e.what());
        }
    }
}

void Scene::render(const RenderData& data, RendererTasks& tasks) {
    ZoneScoped
    ZoneName(
        renderBinToString(data.renderBinMask),
        strlen(renderBinToString(data.renderBinMask))
    )

    for (SceneGraphNode* node : _topologicallySortedNodes) {
        try {
            node->render(data, tasks);
        }
        catch (const ghoul::RuntimeError& e) {
            LERRORC(e.component, e.what());
        }
        if (global::callback::webBrowserPerformanceHotfix) {
            (*global::callback::webBrowserPerformanceHotfix)();
        }
    }

    {
        ZoneScopedN("Get Error Hack")

        // @TODO(abock 2019-08-19) This glGetError call is a hack to prevent the GPU
        // thread and the CPU thread from diverging too much, particularly the uploading
        // of a lot of textures for the globebrowsing planets can cause a hard stuttering
        // effect. Asking for a glGetError after every rendering call will force the
        // threads to implicitly synchronize and thus prevent the stuttering.  The better
        // solution would be to reduce the number of uploads per frame, use a staggered
        // buffer, or something else like that preventing a large spike in uploads
        glGetError();
    }
}

void Scene::clear() {
    LINFO("Clearing current scene graph");
    _rootDummy.clearChildren();
}

const std::unordered_map<std::string, SceneGraphNode*>& Scene::nodesByIdentifier() const {
    return _nodesByIdentifier;
}

SceneGraphNode* Scene::root() {
    return &_rootDummy;
}

const SceneGraphNode* Scene::root() const {
    return &_rootDummy;
}

SceneGraphNode* Scene::sceneGraphNode(const std::string& name) const {
    const auto it = _nodesByIdentifier.find(name);
    if (it != _nodesByIdentifier.end()) {
        return it->second;
    }
    return nullptr;
}

const std::vector<SceneGraphNode*>& Scene::allSceneGraphNodes() const {
    return _topologicallySortedNodes;
}

SceneGraphNode* Scene::loadNode(const ghoul::Dictionary& nodeDictionary) {
    // First interpret the dictionary
    std::vector<std::string> dependencyNames;

    const std::string& nodeIdentifier = nodeDictionary.value<std::string>(KeyIdentifier);
    const bool hasParent = nodeDictionary.hasKey(KeyParent);

    if (_nodesByIdentifier.find(nodeIdentifier) != _nodesByIdentifier.end()) {
        LERROR(fmt::format(
            "Cannot add scene graph node '{}'. A node with that name already exists",
            nodeIdentifier
        ));
        return nullptr;
    }

    SceneGraphNode* parent = nullptr;
    if (hasParent) {
        const std::string parentIdentifier = nodeDictionary.value<std::string>(KeyParent);
        parent = sceneGraphNode(parentIdentifier);
        if (!parent) {
            // TODO: Throw exception
            LERROR(fmt::format(
                "Could not find parent '{}' for '{}'", parentIdentifier, nodeIdentifier
            ));
            return nullptr;
        }
    }

    ghoul::mm_unique_ptr<SceneGraphNode> node = SceneGraphNode::createFromDictionary(
        nodeDictionary
    );
    if (!node) {
        // TODO: Throw exception
        LERROR("Could not create node from dictionary: " + nodeIdentifier);
    }

    if (nodeDictionary.hasKey(SceneGraphNode::KeyDependencies)) {
        if (!nodeDictionary.hasValue<ghoul::Dictionary>(SceneGraphNode::KeyDependencies))
        {
            // TODO: Throw exception
            LERROR("Dependencies did not have the corrent type");
        }
        ghoul::Dictionary nodeDependencies =
            nodeDictionary.value<ghoul::Dictionary>(SceneGraphNode::KeyDependencies);

        for (std::string_view key : nodeDependencies.keys()) {
            std::string value = nodeDependencies.value<std::string>(key);
            dependencyNames.push_back(value);
        }
    }

    // Make sure all dependencies are found
    std::vector<SceneGraphNode*> dependencies;
    bool foundAllDeps = true;
    for (const std::string& depName : dependencyNames) {
        SceneGraphNode* dep = sceneGraphNode(depName);
        if (!dep) {
            // TODO: Throw exception
            LERROR(fmt::format(
                "Could not find dependency '{}' for '{}'", depName, nodeIdentifier
            ));
            foundAllDeps = false;
            continue;
        }
        dependencies.push_back(dep);
    }

    if (!foundAllDeps) {
        return nullptr;
    }

    // Now attach the node to the graph
    SceneGraphNode* rawNodePointer = node.get();

    if (parent) {
        parent->attachChild(std::move(node));
    }
    else {
        attachNode(std::move(node));
    }

    rawNodePointer->setDependencies(dependencies);
    return rawNodePointer;
}

std::chrono::steady_clock::time_point Scene::currentTimeForInterpolation() {
    if (global::sessionRecording->isSavingFramesDuringPlayback()) {
        return global::sessionRecording->currentPlaybackInterpolationTime();
    }
    else {
        return std::chrono::steady_clock::now();
    }
}

void Scene::addPropertyInterpolation(properties::Property* prop, float durationSeconds,
                                     ghoul::EasingFunction easingFunction)
{
    ghoul_precondition(prop != nullptr, "prop must not be nullptr");
    ghoul_precondition(durationSeconds > 0.f, "durationSeconds must be positive");
    ghoul_postcondition(
        std::find_if(
            _propertyInterpolationInfos.begin(),
            _propertyInterpolationInfos.end(),
            [prop](const PropertyInterpolationInfo& info) {
                return info.prop == prop && !info.isExpired;
            }
        ) != _propertyInterpolationInfos.end(),
        "A new interpolation record exists for p that is not expired"
    );

    ghoul::EasingFunc<float> func =
        (easingFunction == ghoul::EasingFunction::Linear) ?
        nullptr :
        ghoul::easingFunction<float>(easingFunction);

    // First check if the current property already has an interpolation information
    std::chrono::steady_clock::time_point now = currentTimeForInterpolation();
    for (PropertyInterpolationInfo& info : _propertyInterpolationInfos) {
        if (info.prop == prop) {
            info.beginTime = now;
            info.durationSeconds = durationSeconds;
            info.easingFunction = func;
            // If we found it, we can break since we make sure that each property is only
            // represented once in this
            return;
        }
    }

    PropertyInterpolationInfo i = {
        prop,
        now,
        durationSeconds,
        func
    };

    _propertyInterpolationInfos.push_back(std::move(i));
}

void Scene::removePropertyInterpolation(properties::Property* prop) {
    ghoul_precondition(prop != nullptr, "prop must not be nullptr");
    ghoul_postcondition(
        std::find_if(
            _propertyInterpolationInfos.begin(),
            _propertyInterpolationInfos.end(),
            [prop](const PropertyInterpolationInfo& info) {
                return info.prop == prop;
            }
        ) == _propertyInterpolationInfos.end(),
        "No interpolation record exists for prop"
    );

    _propertyInterpolationInfos.erase(
        std::remove_if(
            _propertyInterpolationInfos.begin(),
            _propertyInterpolationInfos.end(),
            [prop](const PropertyInterpolationInfo& info) { return info.prop == prop; }
        ),
        _propertyInterpolationInfos.end()
    );
}

void Scene::updateInterpolations() {
    ZoneScoped

    using namespace std::chrono;

    steady_clock::time_point now = currentTimeForInterpolation();
    // First, let's update the properties
    for (PropertyInterpolationInfo& i : _propertyInterpolationInfos) {
        long long usPassed = duration_cast<std::chrono::microseconds>(
            now - i.beginTime
        ).count();

        const float t = glm::clamp(
            static_cast<float>(
                static_cast<double>(usPassed) /
                static_cast<double>(i.durationSeconds * 1000000)
            ),
            0.f,
            1.f
        );

        // @FRAGILE(abock): This method might crash if someone deleted the property
        //                  underneath us. We take care of removing entire PropertyOwners,
        //                  but we assume that Propertys live as long as their
        //                  SceneGraphNodes. This is true in general, but if Propertys are
        //                  created and destroyed often by the SceneGraphNode, this might
        //                  become a problem.
        i.prop->interpolateValue(t, i.easingFunction);

        i.isExpired = (t == 1.f);

        if (i.isExpired) {
            global::eventEngine->publishEvent<events::EventInterpolationFinished>(i.prop);
        }
    }

    _propertyInterpolationInfos.erase(
        std::remove_if(
            _propertyInterpolationInfos.begin(),
            _propertyInterpolationInfos.end(),
            [](const PropertyInterpolationInfo& i) {
                return i.isExpired;
            }
        ),
        _propertyInterpolationInfos.end()
    );
}

void Scene::addInterestingTime(InterestingTime time) {
    _interestingTimes.push_back(std::move(time));
}

const std::vector<Scene::InterestingTime>& Scene::interestingTimes() const {
    return _interestingTimes;
}

void Scene::setPropertiesFromProfile(const Profile& p) {
    ghoul::lua::LuaState L(ghoul::lua::LuaState::IncludeStandardLibrary::Yes);

    for (const Profile::Property& prop : p.properties) {
        std::string uriOrRegex = prop.name;
        std::string groupName;
        if (doesUriContainGroupTag(uriOrRegex, groupName)) {
            // Remove group name from start of regex and replace with '*'
            uriOrRegex = removeGroupNameFromUri(uriOrRegex);
        }
        _profilePropertyName = uriOrRegex;
        ghoul::lua::push(L, uriOrRegex);
        ghoul::lua::push(L, 0.0);

        std::string workingValue = prop.value;
        ghoul::trimSurroundingCharacters(workingValue, ' ');
        // Later functions expect the value to be at the last position on the stack
        propertyPushProfileValueToLua(L, workingValue);

        applyRegularExpression(
            L,
            uriOrRegex,
            allProperties(),
            0.0,
            groupName,
            ghoul::EasingFunction::Linear
        );
        //Clear lua state stack
        lua_settop(L, 0);
    }
}

void Scene::propertyPushProfileValueToLua(ghoul::lua::LuaState& L,
                                                                 const std::string& value)
{
    _valueIsTable = false;
    ProfilePropertyLua elem = propertyProcessValue(L, value);
    if (!_valueIsTable) {
        std::visit(overloaded{
            [&L](const bool value) {
                ghoul::lua::push(L, value);
            },
            [&L](const float value) {
                ghoul::lua::push(L, value);
            },
            [&L](const std::string value) {
                ghoul::lua::push(L, value);
            },
            [&L](const ghoul::lua::nil_t nilValue) {
                ghoul::lua::push(L, nilValue);
            }
        }, elem);
    }
}

ProfilePropertyLua Scene::propertyProcessValue(ghoul::lua::LuaState& L,
                                                                 const std::string& value)
{
    ProfilePropertyLua result;
    PropertyValueType pType = propertyValueType(value);

    switch (pType) {
        case PropertyValueType::Boolean:
            result = (value == "true") ? true : false;
            break;
        case PropertyValueType::Float:
            result = std::stof(value);
            break;
        case PropertyValueType::Nil:
            result = ghoul::lua::nil_t();
            break;
        case PropertyValueType::Table:
            ghoul::trimSurroundingCharacters(const_cast<std::string&>(value), '{');
            ghoul::trimSurroundingCharacters(const_cast<std::string&>(value), '}');
            handlePropertyLuaTableEntry(L, value);
            _valueIsTable = true;
            break;
        case PropertyValueType::String:
        default:
            ghoul::trimSurroundingCharacters(const_cast<std::string&>(value), '\"');
            ghoul::trimSurroundingCharacters(const_cast<std::string&>(value), '[');
            ghoul::trimSurroundingCharacters(const_cast<std::string&>(value), ']');
            result = value;
            break;
    }
    return result;
}

void Scene::handlePropertyLuaTableEntry(ghoul::lua::LuaState& L, const std::string& value)
{
    PropertyValueType enclosedType;
    size_t commaPos = value.find(',', 0);
    if (commaPos != std::string::npos) {
        enclosedType = propertyValueType(value.substr(0, commaPos));
    }
    else {
        enclosedType = propertyValueType(value);
    }

    switch (enclosedType) {
        case PropertyValueType::Boolean:
            LERROR(fmt::format(
                "A lua table of bool values is not supported. (processing property {})",
                _profilePropertyName)
            );
            break;
        case PropertyValueType::Float:
            {
                std::vector<float> valsF;
                processPropertyValueTableEntries(L, value, valsF);
                ghoul::lua::push(L, valsF);
            }
            break;
        case PropertyValueType::String:
            {
                std::vector<std::string> valsS;
                processPropertyValueTableEntries(L, value, valsS);
                ghoul::lua::push(L, valsS);
            }
            break;
        case PropertyValueType::Table:
        default:
            LERROR(fmt::format(
                "Table-within-a-table values are not supported for profile a "
                "property (processing property {})", _profilePropertyName
            ));
            break;
    }
}

template <typename T>
void Scene::processPropertyValueTableEntries(ghoul::lua::LuaState& L,
    const std::string& value, std::vector<T>& table)
{
    size_t commaPos = 0;
    size_t prevPos = 0;
    std::string nextValue;
    while (commaPos != std::string::npos) {
        commaPos = value.find(',', prevPos);
        if (commaPos != std::string::npos) {
            nextValue = value.substr(prevPos, commaPos - prevPos);
            prevPos = commaPos + 1;
        }
        else {
            nextValue = value.substr(prevPos);
        }
        ghoul::trimSurroundingCharacters(nextValue, ' ');
        ProfilePropertyLua tableElement = propertyProcessValue(L, nextValue);
        try {
            table.push_back(std::get<T>(tableElement));
        }
        catch (std::bad_variant_access&) {
            LERROR(fmt::format(
                "Error attempting to parse profile property setting for "
                "{} using value = {}", _profilePropertyName, value
            ));
        }
    }
}

PropertyValueType Scene::propertyValueType(const std::string& value) {
    if (luascriptfunctions::isBoolValue(value)) {
        return PropertyValueType::Boolean;
    }
    else if (luascriptfunctions::isFloatValue(value)) {
        return PropertyValueType::Float;
    }
    else if (luascriptfunctions::isNilValue(value)) {
        return PropertyValueType::Nil;
    }
    else if (luascriptfunctions::isTableValue(value)) {
        return PropertyValueType::Table;
    }
    else {
        return PropertyValueType::String;
    }
}

scripting::LuaLibrary Scene::luaLibrary() {
    return {
        "",
        {
            {
                "setPropertyValue",
                &luascriptfunctions::property_setValue,
                "name, value [, duration, easing, optimization]",
                "Sets all property(s) identified by the URI (with potential wildcards) "
                "in the first argument. The second argument can be any type, but it has "
                "to match the type that the property (or properties) expect. If the "
                "third is not present or is '0', the value changes instantly, otherwise "
                "the change will take that many seconds and the value is interpolated at "
                "each step in between. The fourth parameter is an optional easing "
                "function if a 'duration' has been specified. If 'duration' is 0, this "
                "parameter value is ignored. Otherwise, it can be one of many supported "
                "easing functions. See easing.h for available functions. The fifth "
                "argument must be either empty, 'regex', or 'single'. If the fifth"
                "argument is empty (the default), the URI is interpreted using a "
                "wildcard in which '*' is expanded to '(.*)' and bracketed components "
                "'{ }' are interpreted as group tag names. Then, the passed value will "
                "be set on all properties that fit the regex + group name combination. "
                "If the fifth argument is 'regex' neither the '*' expansion, nor the "
                "group tag expansion is performed and the first argument is used as an "
                "ECMAScript style regular expression that matches against the fully "
                "qualified IDs of properties. If the fifth argument is 'single' no "
                "substitutions are performed and exactly 0 or 1 properties are changed."
            },
            {
                "setPropertyValueSingle",
                &luascriptfunctions::property_setValueSingle,
                "URI, value [, duration, easing]",
                "Sets the property identified by the URI in the first argument. The "
                "second argument can be any type, but it has to match the type that the "
                "property expects. If the third is not present or is '0', the value "
                "changes instantly, otherwise the change will take that many seconds and "
                "the value is interpolated at each step in between. The fourth "
                "parameter is an optional easing function if a 'duration' has been "
                "specified. If 'duration' is 0, this parameter value is ignored. "
                "Otherwise, it has to be 'linear', 'easein', 'easeout', or 'easeinout'. "
                "This is the same as calling the setValue method and passing 'single' as "
                "the fourth argument to setPropertyValue."
            },
            {
                "hasProperty",
                &luascriptfunctions::property_hasProperty,
                "string",
                "Returns whether a property with the given URI exists"
            },
            {
                "getPropertyValue",
                &luascriptfunctions::property_getValue,
                "string",
                "Returns the value the property, identified by the provided URI."
            },
            {
                "getProperty",
                &luascriptfunctions::property_getProperty,
                "string",
                "Returns a list of property identifiers that match the passed regular "
                "expression"
            },
            {
                "addSceneGraphNode",
                &luascriptfunctions::addSceneGraphNode,
                "table",
                "Loads the SceneGraphNode described in the table and adds it to the "
                "SceneGraph"
            },
            {
                "removeSceneGraphNode",
                &luascriptfunctions::removeSceneGraphNode,
                "string",
                "Removes the SceneGraphNode identified by name"
            },
            {
                "removeSceneGraphNodesFromRegex",
                &luascriptfunctions::removeSceneGraphNodesFromRegex,
                "string",
                "Removes all SceneGraphNodes with identifiers matching the input regular "
                "expression"
            },
            {
                "hasSceneGraphNode",
                &luascriptfunctions::hasSceneGraphNode,
                "string",
                "Checks whether the specifies SceneGraphNode is present in the current "
                "scene"
            },
            {
                "addInterestingTime",
                &luascriptfunctions::addInterestingTime,
                "string, string",
                "Adds an interesting time to the current scene. The first argument is "
                "the name of the time and the second argument is the time itself in the "
                "format YYYY-MM-DDThh:mm:ss.uuu"
            },
            {
                "worldPosition",
                &luascriptfunctions::worldPosition,
                "string",
                "Returns the world position of the scene graph node with the given "
                "string as identifier"
            },
            {
                "worldRotation",
                & luascriptfunctions::worldRotation,
                "string",
                "Returns the world rotation matrix of the scene graph node with the "
                "given string as identifier"
            },
            {
                "setParent",
                &luascriptfunctions::setParent,
                "string, string",
                "The scene graph node identified by the first string is reparented to be "
                "a child of the scene graph node identified by the second string."
            },
            {
                "boundingSphere",
                &luascriptfunctions::boundingSphere,
                "string",
                "Returns the bounding sphere of the scene graph node with the given "
                "string as identifier"
            },
            {
                "interactionSphere",
                &luascriptfunctions::interactionSphere,
                "string",
                "Returns the interaction sphere of the scene graph node with the given "
                "string as identifier"
            }
        }
    };
}

}  // namespace openspace
