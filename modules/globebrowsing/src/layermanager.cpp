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

#include <modules/globebrowsing/src/layermanager.h>

#include <modules/globebrowsing/src/layer.h>
#include <modules/globebrowsing/src/layergroup.h>
#include <modules/globebrowsing/src/tileprovider/tileprovider.h>
#include <modules/globebrowsing/src/tiletextureinitdata.h>
#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/profiling.h>

namespace openspace::globebrowsing {

documentation::Documentation LayerManager::Documentation() {
    using namespace documentation;
    return {
        "LayerManager",
        "globebrowsing_layermanager",
        {
            {
                "*",
                new ReferencingVerifier("globebrowsing_layer"),
                Optional::Yes,
                "Specifies an individual layer"
            }
        }
    };
}

LayerManager::LayerManager() : properties::PropertyOwner({ "Layers" }) {}

void LayerManager::initialize(const ghoul::Dictionary& layerGroupsDict) {
    ZoneScoped

    // First create empty layer groups in case not all are specified
    for (size_t i = 0; i < _layerGroups.size(); ++i) {
        _layerGroups[i] = std::make_unique<LayerGroup>(layergroupid::GroupID(i));
    }

    // Create all the layer groups
    for (std::string_view groupName : layerGroupsDict.keys()) {
        layergroupid::GroupID id = ghoul::from_string<layergroupid::GroupID>(groupName);

        if (id != layergroupid::GroupID::Unknown) {
            ghoul::Dictionary d = layerGroupsDict.value<ghoul::Dictionary>(groupName);
            _layerGroups[static_cast<int>(id)]->setLayersFromDict(d);
        }
        else {
            LWARNINGC("LayerManager", fmt::format("Unknown layer group: {}", groupName));
        }
    }

    for (const std::unique_ptr<LayerGroup>& layerGroup : _layerGroups) {
        addPropertySubOwner(layerGroup.get());
    }

    for (const std::unique_ptr<LayerGroup>& lg : _layerGroups) {
        lg->initialize();
    }
}

void LayerManager::deinitialize() {
    for (const std::unique_ptr<LayerGroup>& lg : _layerGroups) {
        lg->deinitialize();
    }
}

Layer* LayerManager::addLayer(layergroupid::GroupID groupId,
                              const ghoul::Dictionary& layerDict)
{
    ZoneScoped

    ghoul_assert(groupId != layergroupid::Unknown, "Layer group ID must be known");
    try {
        return _layerGroups[groupId]->addLayer(layerDict);
    }
    catch (const documentation::SpecificationError& e) {
        LERRORC(e.component, e.message);
        for (const documentation::TestResult::Offense& o : e.result.offenses) {
            LERRORC(o.offender, ghoul::to_string(o.reason));
        }
        for (const documentation::TestResult::Warning& w : e.result.warnings) {
            LWARNINGC(w.offender, ghoul::to_string(w.reason));
        }
        return nullptr;
    }
    catch (const ghoul::RuntimeError& e) {
        LERRORC(e.component, e.message);
        return nullptr;
    }
}

void LayerManager::deleteLayer(layergroupid::GroupID id, const std::string& layerName) {
    ZoneScoped

    ghoul_assert(id != layergroupid::Unknown, "Layer group ID must be known");
    _layerGroups[id]->deleteLayer(layerName);
}

LayerGroup& LayerManager::layerGroup(layergroupid::GroupID groupId) {
    return *_layerGroups[groupId];
}

const LayerGroup& LayerManager::layerGroup(layergroupid::GroupID groupId) const {
    return *_layerGroups[groupId];
}

bool LayerManager::hasAnyBlendingLayersEnabled() const {
    ZoneScoped

    return std::any_of(
        _layerGroups.begin(),
        _layerGroups.end(),
        [](const std::unique_ptr<LayerGroup>& lg) {
            return lg->layerBlendingEnabled() && !lg->activeLayers().empty();
        }
    );
}

std::array<LayerGroup*, LayerManager::NumLayerGroups> LayerManager::layerGroups() const {
    ZoneScoped

    std::array<LayerGroup*, NumLayerGroups> res = {};
    for (int i = 0; i < NumLayerGroups; ++i) {
        res[i] = _layerGroups[i].get();
    }
    return res;
}

void LayerManager::update() {
    ZoneScoped

    for (std::unique_ptr<LayerGroup>& layerGroup : _layerGroups) {
        layerGroup->update();
    }
}

void LayerManager::reset(bool includeDisabled) {
    ZoneScoped

    for (std::unique_ptr<LayerGroup>& layerGroup : _layerGroups) {
        for (Layer* layer : layerGroup->layers()) {
            if ((layer->enabled() || includeDisabled) && layer->tileProvider()) {
                layer->tileProvider()->reset();
            }
        }
    }
}

void LayerManager::onChange(std::function<void(Layer*)> callback) {
    ZoneScoped

    for (std::unique_ptr<LayerGroup>& layerGroup : _layerGroups) {
        layerGroup->onChange(callback);
    }
}

} // namespace openspace::globebrowsing
