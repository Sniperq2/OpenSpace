#ifndef __OPENSPACE_MODULE_SKYBROWSER___WWTDATAHANDLER___H__
#define __OPENSPACE_MODULE_SKYBROWSER___WWTDATAHANDLER___H__

#include <openspace/documentation/documentation.h>
#include <modules/skybrowser/tinyxml2/tinyxml2.h>
#include <modules/space/speckloader.h>
#include <unordered_map>
// For speck loading
#include <ghoul/glm.h>
#include <ghoul/misc/boolean.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace openspace::documentation { struct Documentation; }

namespace openspace {

	struct ImageData {
		std::string name;
		std::string thumbnailUrl;
        std::string imageUrl;
		std::string credits;
		std::string creditsUrl;
		glm::dvec2 celestCoords;
		std::string collection;
		float fov;
        bool hasCelestCoords{ false };
        bool has3dCoords{ false };
	    glm::dvec3 position3d;
	};

	struct ImageCollection {
		std::string name;
		std::string url;
		bool loaded = false;
	};

	class WWTDataHandler {
	
	public:
		WWTDataHandler() = default;
		~WWTDataHandler();
		// Image downloading and xml parsing
		bool downloadFile(std::string& url, std::string& fileDestination);
		void loadWTMLCollectionsFromURL(std::string directory, std::string url, std::string fileName);
		bool loadWTMLCollectionsFromDirectory(std::string directory);
		int loadImagesFromLoadedXMLs();

		const std::vector<ImageCollection>& getAllImageCollectionUrls() const;
		std::vector<ImageData>& getLoadedImages();
		void loadSpeckData(speck::Dataset& dataset);
        std::string createSearchableString(std::string name);

	private:
		void loadImagesFromXML(tinyxml2::XMLElement* node, 
                                std::string collectionName);
		int loadImageFromXmlNode(tinyxml2::XMLElement* imageSet, 
                                std::string collectionName);
        void setImageDataValues(tinyxml2::XMLElement* node,
                                std::string credits,
                                std::string creditsUrl,
                                std::string thumbnail,
                                std::string collectionName,
                                std::string imageUrl,
                                ImageData& img);
        bool directoryExists(std::string& path);

		std::string getChildNodeContentFromImageSet(tinyxml2::XMLElement* imageSet, 
                                                    std::string elementName);
		std::string getURLFromPlace(tinyxml2::XMLElement* place);
		tinyxml2::XMLElement* getDirectChildNode(tinyxml2::XMLElement* node, 
                                                 std::string name);
		tinyxml2::XMLElement* getChildNode(tinyxml2::XMLElement* node, std::string name);

		std::vector<ImageData> images;
		std::vector<ImageCollection> imageUrls;
		std::vector<tinyxml2::XMLDocument*> xmls;
		// 3D position data loaded from speck files
		std::unordered_map<std::string, glm::dvec3> _3dPositions;
        int nImagesWith3dPositions = 0;
	};
}



#endif // __OPENSPACE_MODULE_SKYBROWSER___WWTDATAHANDLER___H__

