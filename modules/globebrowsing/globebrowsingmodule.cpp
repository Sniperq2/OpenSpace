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

#include <modules/globebrowsing/globebrowsingmodule.h>

#include <modules/globebrowsing/src/basictypes.h>
#include <modules/globebrowsing/src/dashboarditemglobelocation.h>
#include <modules/globebrowsing/src/gdalwrapper.h>
#include <modules/globebrowsing/src/geodeticpatch.h>
#include <modules/globebrowsing/src/globelabelscomponent.h>
#include <modules/globebrowsing/src/globetranslation.h>
#include <modules/globebrowsing/src/globerotation.h>
#include <modules/globebrowsing/src/layer.h>
#include <modules/globebrowsing/src/layeradjustment.h>
#include <modules/globebrowsing/src/layermanager.h>
#include <modules/globebrowsing/src/memoryawaretilecache.h>
#include <modules/globebrowsing/src/tileprovider/defaulttileprovider.h>
#include <modules/globebrowsing/src/tileprovider/imagesequencetileprovider.h>
#include <modules/globebrowsing/src/tileprovider/singleimagetileprovider.h>
#include <modules/globebrowsing/src/tileprovider/sizereferencetileprovider.h>
#include <modules/globebrowsing/src/tileprovider/temporaltileprovider.h>
#include <modules/globebrowsing/src/tileprovider/tileindextileprovider.h>
#include <modules/globebrowsing/src/tileprovider/tileprovider.h>
#include <modules/globebrowsing/src/tileprovider/tileproviderbyindex.h>
#include <modules/globebrowsing/src/tileprovider/tileproviderbylevel.h>
#include <openspace/documentation/verifier.h>
#include <openspace/engine/globalscallbacks.h>
#include <openspace/navigation/navigationhandler.h>
#include <openspace/navigation/navigationstate.h>
#include <openspace/navigation/orbitalnavigator.h>
#include <openspace/scripting/lualibrary.h>
#include <openspace/util/factorymanager.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/fmt.h>
#include <ghoul/misc/assert.h>
#include <ghoul/misc/templatefactory.h>
#include <ghoul/misc/profiling.h>
#include <ghoul/systemcapabilities/generalcapabilitiescomponent.h>
#include <vector>

#include <gdal.h>

#ifdef _MSC_VER
#pragma warning (push)
// CPL throws warning about missing DLL interface
#pragma warning (disable : 4251)
#endif // _MSC_VER

#include <cpl_string.h>

#ifdef _MSC_VER
#pragma warning (pop)
#endif // _MSC_VER

#include "globebrowsingmodule_lua.inl"

namespace {
    constexpr const char _loggerCat[] = "GlobeBrowsingModule";
    constexpr const char _factoryName[] = "TileProvider";

    constexpr const openspace::properties::Property::PropertyInfo WMSCacheEnabledInfo = {
        "WMSCacheEnabled",
        "WMS Cache Enabled",
        "Determines whether automatic caching of WMS servers is enabled. Changing the "
        "value of this property will not affect already created WMS datasets."
    };

    constexpr const openspace::properties::Property::PropertyInfo OfflineModeInfo = {
        "OfflineMode",
        "Offline Mode",
        "Determines whether loaded WMS servers should be used in offline mode, that is "
        "not even try to retrieve images through an internet connection. Please note "
        "that this setting is only reasonable, if the caching is enabled and there is "
        "available cached data. Changing the value of this property will not affect "
        "already created WMS datasets."
    };

    constexpr const openspace::properties::Property::PropertyInfo WMSCacheLocationInfo = {
        "WMSCacheLocation",
        "WMS Cache Location",
        "The location of the cache folder for WMS servers. Changing the value of this "
        "property will not affect already created WMS datasets."
    };

    constexpr const openspace::properties::Property::PropertyInfo WMSCacheSizeInfo = {
        "WMSCacheSize",
        "WMS Cache Size",
        "The maximum size of the cache for each WMS server. Changing the value of this "
        "property will not affect already created WMS datasets."
    };

    constexpr const openspace::properties::Property::PropertyInfo TileCacheSizeInfo = {
        "TileCacheSize",
        "Tile Cache Size",
        "The maximum size of the MemoryAwareTileCache, on the CPU and GPU."
    };


    openspace::GlobeBrowsingModule::Capabilities
    parseSubDatasets(char** subDatasets, int nSubdatasets)
    {
        // Idea: Iterate over the list of sublayers keeping a current layer and identify
        //       it by its number.  If this number changes, we know that we have a new
        //       layer

        using Layer = openspace::GlobeBrowsingModule::Layer;
        std::vector<Layer> result;

        int currentLayerNumber = -1;
        Layer currentLayer;
        for (int i = 0; i < nSubdatasets; ++i) {
            int iDataset = -1;
            std::array<char, 256> IdentifierBuffer;
            std::fill(IdentifierBuffer.begin(), IdentifierBuffer.end(), '\0');
            int ret = sscanf(
                subDatasets[i],
                "SUBDATASET_%i_%256[^=]",
                &iDataset,
                IdentifierBuffer.data()
            );
            if (ret != 2) {
                LERROR("Error parsing dataset");
                continue;
            }


            if (iDataset != currentLayerNumber) {
                // We are done with the previous version
                result.push_back(std::move(currentLayer));
                currentLayer = Layer();
                currentLayerNumber = iDataset;
            }

            const std::string identifier = std::string(IdentifierBuffer.data());
            const std::string ds(subDatasets[i]);
            const std::string value = ds.substr(ds.find_first_of('=') + 1);

            // The DESC/NAME difference is not a typo
            if (identifier == "DESC") {
                currentLayer.name = value;
            }
            else if (identifier == "NAME") {
                currentLayer.url = value;
            }
            else {
                LINFOC(
                    "GlobeBrowsingGUI", "Unknown subdataset identifier: " + identifier
                );
            }
        }

        return result;
    }

    struct [[codegen::Dictionary(GlobeBrowsingModule)]] Parameters {
        // [[codegen::verbatim(WMSCacheEnabledInfo.description)]]
        std::optional<bool> cacheEnabled [[codegen::key("WMSCacheEnabled")]];

        // [[codegen::verbatim(OfflineModeInfo.description)]]
        std::optional<bool> offlineMode;

        // [[codegen::verbatim(WMSCacheLocationInfo.description)]]
        std::optional<std::string> cacheLocation [[codegen::key("WMSCacheLocation")]];

        // [[codegen::verbatim(WMSCacheSizeInfo.description)]]
        std::optional<int> wmsCacheSize [[codegen::key("WMSCacheSize")]];

        // [[codegen::verbatim(TileCacheSizeInfo.description)]]
        std::optional<int> tileCacheSize;

        // If you know what you are doing and you have WMS caching *disabled* but offline
        // mode *enabled*, you can set this value to 'true' to silence a warning that you
        // would otherwise get at startup
        std::optional<bool> noWarning;
    };
#include "globebrowsingmodule_codegen.cpp"
} // namespace

namespace openspace {

GlobeBrowsingModule::GlobeBrowsingModule()
    : OpenSpaceModule(Name)
    , _wmsCacheEnabled(WMSCacheEnabledInfo, false)
    , _offlineMode(OfflineModeInfo, false)
    , _wmsCacheLocation(WMSCacheLocationInfo, "${BASE}/cache_gdal")
    , _wmsCacheSizeMB(WMSCacheSizeInfo, 1024)
    , _tileCacheSizeMB(TileCacheSizeInfo, 1024)
{
    addProperty(_wmsCacheEnabled);
    addProperty(_offlineMode);
    addProperty(_wmsCacheLocation);
    addProperty(_wmsCacheSizeMB);
    addProperty(_tileCacheSizeMB);
}

void GlobeBrowsingModule::internalInitialize(const ghoul::Dictionary& dict) {
    using namespace globebrowsing;

    const Parameters p = codegen::bake<Parameters>(dict);
    _wmsCacheEnabled = p.cacheEnabled.value_or(_wmsCacheEnabled);
    _offlineMode = p.offlineMode.value_or(_offlineMode);
    _wmsCacheLocation = p.cacheLocation.value_or(_wmsCacheLocation);
    _wmsCacheSizeMB = p.wmsCacheSize.value_or(_wmsCacheSizeMB);
    _tileCacheSizeMB = p.tileCacheSize.value_or(_tileCacheSizeMB);
    const bool noWarning = p.noWarning.value_or(false);

    if (!_wmsCacheEnabled && _offlineMode && !noWarning) {
        LWARNINGC(
            "GlobeBrowsingModule",
            "WMS caching is disabled, but offline mode is enabled. Unless you know "
            "what you are doing, this will probably cause many servers to stop working. "
            "If you want to silence this warning, set the 'NoWarning' parameter to "
            "'true'."
        );
    }


    // Initialize
    global::callback::initializeGL->emplace_back([&]() {
        ZoneScopedN("GlobeBrowsingModule")

        _tileCache = std::make_unique<cache::MemoryAwareTileCache>(_tileCacheSizeMB);
        addPropertySubOwner(_tileCache.get());

        TileProvider::initializeDefaultTile();

        // Convert from MB to Bytes
        GdalWrapper::create(
            512ULL * 1024ULL * 1024ULL, // 512 MB
            static_cast<size_t>(CpuCap.installedMainMemory() * 0.25 * 1024 * 1024)
        );
        addPropertySubOwner(GdalWrapper::ref());
    });

    global::callback::deinitializeGL->emplace_back([]() {
        ZoneScopedN("GlobeBrowsingModule")

        TileProvider::deinitializeDefaultTile();
    });

    // Render
    global::callback::render->emplace_back([&]() {
        ZoneScopedN("GlobeBrowsingModule")

        _tileCache->update();
    });

    // Deinitialize
    global::callback::deinitialize->emplace_back([&]() {
        ZoneScopedN("GlobeBrowsingModule")

        GdalWrapper::destroy();
    });

    auto fRenderable = FactoryManager::ref().factory<Renderable>();
    ghoul_assert(fRenderable, "Renderable factory was not created");
    fRenderable->registerClass<globebrowsing::RenderableGlobe>("RenderableGlobe");

    auto fTranslation = FactoryManager::ref().factory<Translation>();
    ghoul_assert(fTranslation, "Translation factory was not created");
    fTranslation->registerClass<globebrowsing::GlobeTranslation>("GlobeTranslation");

    auto fRotation = FactoryManager::ref().factory<Rotation>();
    ghoul_assert(fRotation, "Rotation factory was not created");
    fRotation->registerClass<globebrowsing::GlobeRotation>("GlobeRotation");

    auto fTileProvider = std::make_unique<ghoul::TemplateFactory<TileProvider>>();
    ghoul_assert(fTileProvider, "TileProvider factory was not created");

    fTileProvider->registerClass<DefaultTileProvider>(
        layergroupid::LAYER_TYPE_NAMES[static_cast<int>(
            layergroupid::TypeID::DefaultTileLayer
        )]
    );
    fTileProvider->registerClass<SingleImageProvider>(
        layergroupid::LAYER_TYPE_NAMES[static_cast<int>(
            layergroupid::TypeID::SingleImageTileLayer
        )]
    );
    fTileProvider->registerClass<ImageSequenceTileProvider>(
        layergroupid::LAYER_TYPE_NAMES[static_cast<int>(
            layergroupid::TypeID::ImageSequenceTileLayer
        )]
    );
    fTileProvider->registerClass<TemporalTileProvider>(
        layergroupid::LAYER_TYPE_NAMES[static_cast<int>(
            layergroupid::TypeID::TemporalTileLayer
        )]
    );
    fTileProvider->registerClass<TileIndexTileProvider>(
        layergroupid::LAYER_TYPE_NAMES[static_cast<int>(
            layergroupid::TypeID::TileIndexTileLayer
        )]
    );
    fTileProvider->registerClass<SizeReferenceTileProvider>(
        layergroupid::LAYER_TYPE_NAMES[static_cast<int>(
            layergroupid::TypeID::SizeReferenceTileLayer
        )]
    );
    fTileProvider->registerClass<TileProviderByLevel>(
        layergroupid::LAYER_TYPE_NAMES[static_cast<int>(
            layergroupid::TypeID::ByLevelTileLayer
        )]
    );
    fTileProvider->registerClass<TileProviderByIndex>(
        layergroupid::LAYER_TYPE_NAMES[static_cast<int>(
            layergroupid::TypeID::ByIndexTileLayer
        )]
    );

    FactoryManager::ref().addFactory(std::move(fTileProvider), _factoryName);

    auto fDashboard = FactoryManager::ref().factory<DashboardItem>();
    ghoul_assert(fDashboard, "Dashboard factory was not created");

    fDashboard->registerClass<DashboardItemGlobeLocation>("DashboardItemGlobeLocation");
}

globebrowsing::cache::MemoryAwareTileCache* GlobeBrowsingModule::tileCache() {
    return _tileCache.get();
}

scripting::LuaLibrary GlobeBrowsingModule::luaLibrary() const {
    std::string listLayerGroups = layerGroupNamesList();

    scripting::LuaLibrary res;
    res.name = "globebrowsing";
    res.functions = {
        {
            "addLayer",
            &globebrowsing::luascriptfunctions::addLayer,
            "string, string, table",
            "Adds a layer to the specified globe. The first argument specifies the "
            "name of the scene graph node of which to add the layer. The renderable "
            "of the specified scene graph node needs to be a renderable globe. "
            "The second argument is the layer group which can be any of "
            + listLayerGroups + ". The third argument is the dictionary defining the "
            "layer."
        },
        {
            "deleteLayer",
            &globebrowsing::luascriptfunctions::deleteLayer,
            "string, string",
            "Removes a layer from the specified globe. The first argument specifies "
            "the name of the scene graph node of which to remove the layer. "
            "The renderable of the specified scene graph node needs to be a "
            "renderable globe. The second argument is the layer group which can be "
            "any of " + listLayerGroups + ". The third argument is the dictionary"
            "defining the layer."
        },
        {
            "getLayers",
            &globebrowsing::luascriptfunctions::getLayers,
            "string, string",
            "Returns the list of layers for the scene graph node specified in the first "
            "parameter. The second parameter specifies which layer type should be "
            "queried."
        },
        {
            "moveLayer",
            &globebrowsing::luascriptfunctions::moveLayer,
            "string, string, number, number",
            "Rearranges the order of a single layer on a globe. The first parameter"
            "is the identifier of the globe, the second parameter specifies "
            "the name of the layer group, the third parameter is the original position "
            "of the layer that should be moved and the last parameter is the new "
            "position in the list. The first position in the list has index 0, and the "
            "last position is given by the number of layers minus one. The new position "
            "may be -1 to place the layer at the top or any number bigger than the "
            "number of layers to place it at the bottom."
        },
        {
            "goToChunk",
            &globebrowsing::luascriptfunctions::goToChunk,
            "void",
            "Go to chunk with given index x, y, level"
        },
        {
            "goToGeo",
            &globebrowsing::luascriptfunctions::goToGeo,
            "[string], number, number, [number]",
            "Go to geographic coordinates of a globe. The first (optional) argument is "
            "the identifier of a scene graph node that has a RenderableGlobe attached. "
            "If no globe is passed in, the current anchor will be used. "
            "The second argument is latitude and the third is longitude (degrees). "
            "North and East are expressed as positive angles, while South and West are "
            "negative. The optional fourh argument is the altitude in meters. If no "
            "altitude is provided, the altitude will be kept as the current distance to "
            "the surface of the specified globe."
        },
        {
            // @TODO (2021-06-23, emmbr) Combine with the above function when the camera
            // paths work really well close to surfaces
            "flyToGeo",
            &globebrowsing::luascriptfunctions::flyToGeo,
            "[string], number, number, number [, bool, number]",
            "Fly the camera to geographic coordinates of a globe, using the path "
            "navigation system. The first (optional) argument is the identifier of a "
            "scene graph node with a RenderableGlobe. If no globe is passed in, the "
            "current anchor will be used. The second and third argument is latitude and "
            "longitude (degrees). The fourth argument is the altitude, in meters. The "
            "last two optional arguments are: a bool specifying whether the up vector "
            "at the target position should be set to the globe's North vector, and a "
            "duration for the motion, in seconds. Either of the two can be left out."
        },
        {
            "getLocalPositionFromGeo",
            &globebrowsing::luascriptfunctions::getLocalPositionFromGeo,
            "string, number, number, number",
            "Returns a position in the local Cartesian coordinate system of the globe "
            "identified by the first argument, that corresponds to the given geographic "
            "coordinates: latitude, longitude and altitude (in degrees and meters). In "
            "the local coordinate system, the position (0,0,0) corresponds to the "
            "globe's center."
        },
        {
            "getGeoPositionForCamera",
            &globebrowsing::luascriptfunctions::getGeoPositionForCamera,
            "void",
            "Get geographic coordinates of the camera position in latitude, "
            "longitude, and altitude (degrees and meters)."
        },
        {
            "loadWMSCapabilities",
            &globebrowsing::luascriptfunctions::loadWMSCapabilities,
            "string, string, string",
            "Loads and parses the WMS capabilities xml file from a remote server. "
            "The first argument is the name of the capabilities that can be used to "
            "later refer to the set of capabilities. The second argument is the "
            "globe for which this server is applicable. The third argument is the "
            "URL at which the capabilities file can be found."
        },
        {
            "removeWMSServer",
            &globebrowsing::luascriptfunctions::removeWMSServer,
            "string",
            "Removes the WMS server identified by the first argument from the list "
            "of available servers. The parameter corrsponds to the first argument in "
            "the loadWMSCapabilities call that was used to load the WMS server."
        },
        {
            "capabilitiesWMS",
            &globebrowsing::luascriptfunctions::capabilities,
            "string",
            "Returns an array of tables that describe the available layers that are "
            "supported by the WMS server identified by the provided name. The 'URL'"
            "component of the returned table can be used in the 'FilePath' argument "
            "for a call to the 'addLayer' function to add the value to a globe."
        }
    };
    res.scripts = {
        absPath("${MODULE_GLOBEBROWSING}/scripts/layer_support.lua")
    };

    return res;
}

std::vector<documentation::Documentation> GlobeBrowsingModule::documentations() const {
    return {
        globebrowsing::Layer::Documentation(),
        globebrowsing::LayerAdjustment::Documentation(),
        globebrowsing::LayerManager::Documentation(),
        globebrowsing::GlobeTranslation::Documentation(),
        globebrowsing::GlobeRotation::Documentation(),
        globebrowsing::RenderableGlobe::Documentation(),
        globebrowsing::DefaultTileProvider::Documentation(),
        globebrowsing::ImageSequenceTileProvider::Documentation(),
        globebrowsing::SingleImageProvider::Documentation(),
        globebrowsing::SizeReferenceTileProvider::Documentation(),
        globebrowsing::TemporalTileProvider::Documentation(),
        globebrowsing::TileProviderByIndex::Documentation(),
        globebrowsing::TileProviderByLevel::Documentation(),
        GlobeLabelsComponent::Documentation(),
        RingsComponent::Documentation(),
        ShadowComponent::Documentation()
    };
}

void GlobeBrowsingModule::goToChunk(const globebrowsing::RenderableGlobe& globe,
                                    int x, int y, int level)
{
    ghoul_assert(level < std::numeric_limits<uint8_t>::max(), "Level way too big");
    goToChunk(
        globe,
        globebrowsing::TileIndex(x, y, static_cast<uint8_t>(level)),
        glm::vec2(0.5f, 0.5f),
        true
    );
}

void GlobeBrowsingModule::goToGeo(const globebrowsing::RenderableGlobe& globe,
                                  double latitude, double longitude)
{
    using namespace globebrowsing;
    goToGeodetic2(
        globe,
        Geodetic2{ glm::radians(latitude), glm::radians(longitude) },
        true
    );
}

void GlobeBrowsingModule::goToGeo(const globebrowsing::RenderableGlobe& globe,
                                  double latitude, double longitude, double altitude)
{
    using namespace globebrowsing;
    goToGeodetic3(
        globe,
        {
            Geodetic2{ glm::radians(latitude), glm::radians(longitude) },
            altitude
        },
        true
    );
}

glm::vec3 GlobeBrowsingModule::cartesianCoordinatesFromGeo(
                                              const globebrowsing::RenderableGlobe& globe,
                                       double latitude, double longitude, double altitude)
{
    using namespace globebrowsing;

    const Geodetic3 pos = {
        { glm::radians(latitude), glm::radians(longitude) },
        altitude
    };

    return glm::vec3(globe.ellipsoid().cartesianPosition(pos));
}

void GlobeBrowsingModule::goToChunk(const globebrowsing::RenderableGlobe& globe,
                                    const globebrowsing::TileIndex& ti,
                                    glm::vec2 uv, bool doResetCameraDirection)
{
    using namespace globebrowsing;

    const GeodeticPatch patch(ti);
    const Geodetic2 corner = patch.corner(SOUTH_WEST);
    Geodetic2 positionOnPatch = patch.size();
    positionOnPatch.lat *= uv.y;
    positionOnPatch.lon *= uv.x;
    const Geodetic2 pointPosition = {
        corner.lat + positionOnPatch.lat,
        corner.lon + positionOnPatch.lon
    };

    // Compute altitude
    const glm::dvec3 cameraPosition = global::navigationHandler->camera()->positionVec3();
    SceneGraphNode* globeSceneGraphNode = dynamic_cast<SceneGraphNode*>(globe.owner());
    if (!globeSceneGraphNode) {
        LERROR(
            "Cannot go to chunk. The renderable is not attached to a scene graph node."
        );
        return;
    }
    const glm::dmat4 inverseModelTransform = glm::inverse(
        globeSceneGraphNode->modelTransform()
    );
    const glm::dvec3 cameraPositionModelSpace =
        glm::dvec3(inverseModelTransform * glm::dvec4(cameraPosition, 1.0));
    const SurfacePositionHandle posHandle = globe.calculateSurfacePositionHandle(
        cameraPositionModelSpace
    );

    const double altitude = glm::length(
        cameraPositionModelSpace - posHandle.centerToReferenceSurface
    );

    goToGeodetic3(globe, { pointPosition, altitude }, doResetCameraDirection);
}

void GlobeBrowsingModule::goToGeodetic2(const globebrowsing::RenderableGlobe& globe,
                                        globebrowsing::Geodetic2 geo2,
                                        bool doResetCameraDirection)
{
    using namespace globebrowsing;

    const glm::dvec3 cameraPosition = global::navigationHandler->camera()->positionVec3();
    SceneGraphNode* globeSceneGraphNode = dynamic_cast<SceneGraphNode*>(globe.owner());
    if (!globeSceneGraphNode) {
        LERROR("Error when going to Geodetic2");
    }

    const glm::dmat4 inverseModelTransform = glm::inverse(
        globeSceneGraphNode->modelTransform()
    );

    const glm::dvec3 cameraPositionModelSpace =
        glm::dvec3(inverseModelTransform * glm::dvec4(cameraPosition, 1.0));
    const SurfacePositionHandle posHandle = globe.calculateSurfacePositionHandle(
        cameraPositionModelSpace
    );

    const glm::dvec3 centerToActualSurface = posHandle.centerToReferenceSurface +
                       posHandle.referenceSurfaceOutDirection * posHandle.heightToSurface;
    const double altitude = glm::length(cameraPositionModelSpace - centerToActualSurface);

    goToGeodetic3(globe, { geo2, altitude }, doResetCameraDirection);
}

void GlobeBrowsingModule::goToGeodetic3(const globebrowsing::RenderableGlobe& globe,
                                        globebrowsing::Geodetic3 geo3, bool)
{
    using namespace globebrowsing;
    const glm::dvec3 positionModelSpace = globe.ellipsoid().cartesianPosition(geo3);


    const glm::dvec3 slightlyNorth = globe.ellipsoid().cartesianSurfacePosition(
        Geodetic2{ geo3.geodetic2.lat + 0.001, geo3.geodetic2.lon }
    );

    interaction::NavigationState state;
    state.anchor = globe.owner()->identifier();
    state.referenceFrame = globe.owner()->identifier();
    state.position = positionModelSpace;
    state.up = slightlyNorth;

    global::navigationHandler->setNavigationStateNextFrame(state);
}

glm::dquat GlobeBrowsingModule::lookDownCameraRotation(
                                              const globebrowsing::RenderableGlobe& globe,
                                                              glm::dvec3 cameraModelSpace,
                                                            globebrowsing::Geodetic2 geo2)
{
    using namespace globebrowsing;

    // Lookup vector
    const glm::dvec3 positionModelSpace = globe.ellipsoid().cartesianSurfacePosition(
        geo2
    );
    const glm::dvec3 slightlyNorth = globe.ellipsoid().cartesianSurfacePosition(
        Geodetic2{ geo2.lat + 0.001, geo2.lon }
    );
    const glm::dvec3 lookUpModelSpace = glm::normalize(
        slightlyNorth - positionModelSpace
    );

    // Matrix
    const glm::dmat4 lookAtMatrix =
        glm::lookAt(cameraModelSpace, positionModelSpace, lookUpModelSpace);

    // Set rotation
    const glm::dquat rotation = glm::quat_cast(inverse(lookAtMatrix));
    return rotation;
}

const globebrowsing::RenderableGlobe*
GlobeBrowsingModule::castFocusNodeRenderableToGlobe()
{
    using namespace globebrowsing;

    const Renderable* renderable =
        global::navigationHandler->orbitalNavigator().anchorNode()->renderable();

    if (!renderable) {
        return nullptr;
    }
    using namespace globebrowsing;
    if (const RenderableGlobe* globe = dynamic_cast<const RenderableGlobe*>(renderable)) {
        return globe;
    }
    else {
        return nullptr;
    }
}

std::string GlobeBrowsingModule::layerGroupNamesList() {
    std::string listLayerGroups;
    for (int i = 0; i < globebrowsing::layergroupid::NUM_LAYER_GROUPS - 1; ++i) {
        listLayerGroups += globebrowsing::layergroupid::LAYER_GROUP_IDENTIFIERS[i] +
                           std::string(", ");
    }
    listLayerGroups += std::string(" and ") +
        globebrowsing::layergroupid::LAYER_GROUP_IDENTIFIERS[
            globebrowsing::layergroupid::NUM_LAYER_GROUPS - 1
        ];
    return listLayerGroups;
}

std::string GlobeBrowsingModule::layerTypeNamesList() {
    std::string listLayerTypes;
    for (int i = 0; i < globebrowsing::layergroupid::NUM_LAYER_TYPES - 1; ++i) {
        listLayerTypes += std::string(globebrowsing::layergroupid::LAYER_TYPE_NAMES[i]) +
                          ", ";
    }
    listLayerTypes += std::string(" and ") +
        globebrowsing::layergroupid::LAYER_TYPE_NAMES[
            globebrowsing::layergroupid::NUM_LAYER_TYPES - 1
        ];
    return listLayerTypes;
}

void GlobeBrowsingModule::loadWMSCapabilities(std::string name, std::string globe,
                                              std::string url)
{
    auto downloadFunction = [](const std::string& downloadUrl) {
        LDEBUG("Opening WMS capabilities: " + downloadUrl);
        GDALDatasetH dataset = GDALOpen(
            downloadUrl.c_str(),
            GA_ReadOnly
        );

        if (!dataset) {
            LWARNING("Could not open dataset: " + downloadUrl);
            return Capabilities();
        }
        char** subDatasets = GDALGetMetadata(dataset, "SUBDATASETS");
        const int nSubdatasets = CSLCount(subDatasets);
        Capabilities cap = parseSubDatasets(subDatasets, nSubdatasets);
        GDALClose(dataset);
        LDEBUG("Finished WMS capabilities: " + downloadUrl);
        return cap;
    };

    _inFlightCapabilitiesMap[name] = std::async(
        std::launch::async,
        downloadFunction,
        url
    );

    //_capabilitiesMap[name] = downloadFunction(url);

    _urlList.emplace(std::move(globe), UrlInfo{ std::move(name), std::move(url) });
}

GlobeBrowsingModule::Capabilities
GlobeBrowsingModule::capabilities(const std::string& name)
{
    // First check the ones that have already finished
    const auto it = _capabilitiesMap.find(name);
    if (it != _capabilitiesMap.end()) {
        return it->second;
    }
    else {
        const auto inFlightIt = _inFlightCapabilitiesMap.find(name);
        if (inFlightIt != _inFlightCapabilitiesMap.end()) {
            // If the download and the parsing has not finished yet, this will block,
            // otherwise it will just return
            const Capabilities cap = inFlightIt->second.get();
            _capabilitiesMap[name] = cap;
            _inFlightCapabilitiesMap.erase(inFlightIt);
            return cap;
        }
        else {
            return {};
        }
    }
}

void GlobeBrowsingModule::removeWMSServer(const std::string& name) {
    // First delete all the capabilities that are currently in flight
    const auto inFlightIt = _inFlightCapabilitiesMap.find(name);
    if (inFlightIt != _inFlightCapabilitiesMap.end()) {
        _inFlightCapabilitiesMap.erase(inFlightIt);
    }

    // Then download the ones that are already finished
    const auto capIt = _capabilitiesMap.find(name);
    if (capIt != _capabilitiesMap.end()) {
        _capabilitiesMap.erase(capIt);
    }

    // Then remove the calues from the globe server list
    for (auto it = _urlList.begin(); it != _urlList.end(); ) {
        // We have to increment first because the erase will invalidate the iterator
        const auto eraseIt = it++;

        if (eraseIt->second.name == name) {
            _urlList.erase(eraseIt);
        }
    }
}

std::vector<GlobeBrowsingModule::UrlInfo>
GlobeBrowsingModule::urlInfo(const std::string& globe) const
{
    const auto range = _urlList.equal_range(globe);
    std::vector<UrlInfo> res;
    for (auto i = range.first; i != range.second; ++i) {
        res.emplace_back(i->second);
    }
    return res;
}

bool GlobeBrowsingModule::hasUrlInfo(const std::string& globe) const {
    return _urlList.find(globe) != _urlList.end();
}

bool GlobeBrowsingModule::isWMSCachingEnabled() const {
    return _wmsCacheEnabled;
}

bool GlobeBrowsingModule::isInOfflineMode() const {
    return _offlineMode;
}

std::string GlobeBrowsingModule::wmsCacheLocation() const {
    return _wmsCacheLocation;
}

uint64_t GlobeBrowsingModule::wmsCacheSize() const {
    uint64_t size = _wmsCacheSizeMB;
    return size * 1024 * 1024;
}

} // namespace openspace
