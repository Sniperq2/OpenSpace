/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2021                                                               *
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

#include <modules/exoplanetsexperttool/dataloader.h>

#include <modules/exoplanetsexperttool/datahelper.h>
#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/util/coordinateconversion.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/misc/csvreader.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/dictionary.h>
#include <charconv>
#include <cmath>
#include <fstream>

namespace {
    constexpr const char _loggerCat[] = "ExoplanetsDataLoader";

    // @TODO: naturally, this path should not be hardcoded
    std::string DataPath = "${MODULES}/exoplanetsexperttool/data/aggregated_data.csv";

    constexpr const double EarthMass = 5.972e24; // kg
    constexpr const double EarthRadius = 6.3781e6; // meter
} // namespace

namespace openspace::exoplanets {

DataLoader::DataLoader() : _inExoplanetsCsvPath(absPath(DataPath).string()) {}

std::vector<ExoplanetItem> DataLoader::loadData() {
    std::ifstream exoplanetsCsvFile(_inExoplanetsCsvPath);
    if (!exoplanetsCsvFile.good()) {
        LERROR(fmt::format("Failed to open input file '{}'", _inExoplanetsCsvPath));
        return std::vector<ExoplanetItem>();
    }

    LINFO("Reading Exoplanets CSV");

    bool includeFirstLine = true;
    std::vector<std::vector<std::string>> csvContent = ghoul::loadCSVFile(
        _inExoplanetsCsvPath,
        includeFirstLine
    );

    if (csvContent.empty()) {
        LERROR(
            fmt::format("Could not read CSV data from file '{}'", _inExoplanetsCsvPath)
        );
        return std::vector<ExoplanetItem>();
    }

    // Write exoplanet records to file
   std::vector<std::string> columns = csvContent[0];

   const int nRows = static_cast<int>(csvContent.size());

   std::vector<ExoplanetItem> planets;
   planets.reserve(nRows);

   int idCounter = 0;

   for (int row = 1; row < nRows; row++) {
       ExoplanetItem p;
       std::string name;
       std::string component;
       std::string hostStar;

       for (int col = 0; col < columns.size(); col++) {
           const std::string& column = columns[col];
           const std::string& data = csvContent[row][col];

           if (column == "pl_name") {
               p.planetName = data;
               // TODO: create identifier matching exoplanets module?
           }
           else if (column == "hostname") {
               p.hostName = data;
               // TODO: create identifier matching exoplanets module?
           }
           // Planet properties
           else if (column == "pl_rade") {
               p.radius.value = data::parseFloatData(data);
           }
           else if (column == "pl_bmasse") {
               p.mass.value = data::parseFloatData(data);
           }
           // Orbital properties
           else if (column == "pl_orbsmax") {
               p.semiMajorAxis.value = data::parseFloatData(data);
           }
           else if (column == "pl_orbeccen") {
               p.eccentricity.value = data::parseFloatData(data);
           }
           else if (column == "pl_orbper") {
               p.period.value = data::parseFloatData(data);
           }
           else if (column == "pl_orbincl") {
               p.inclination.value = data::parseFloatData(data);
           }
           else if (column == "pl_Teq") {
               p.eqilibriumTemp.value = data::parseFloatData(data);
           }
           // Star properties
           else if (column == "st_teff") {
               p.starEffectiveTemp.value = data::parseFloatData(data);
           }
           else if (column == "st_rad") {
               p.starRadius.value = data::parseFloatData(data);
           }
           else if (column == "st_age") {
               p.starAge.value = data::parseFloatData(data);
           }
           else if (column == "st_met") {
               p.starMetallicity.value = data::parseFloatData(data);
           }
           else if (column == "st_metratio") {
               p.starMetallicityRatio = data;
           }
           else if (column == "sy_jmag") {
               p.magnitudeJ.value = data::parseFloatData(data);
           }
           else if (column == "sy_kmag") {
               p.magnitudeK.value = data::parseFloatData(data);
           }
           // System properties
           else if (column == "sy_snum") {
               p.nStars = data::parseFloatData(data);
           }
           else if (column == "sy_pnum") {
               p.nPlanets = data::parseFloatData(data);
           }
           else if (column == "disc_year") {
               p.discoveryYear = data::parseFloatData(data);
           }
           // Position
           else if (column == "ra") {
               p.ra.value = data::parseFloatData(data);
           }
           else if (column == "dec") {
               p.dec.value = data::parseFloatData(data);
           }
           else if (column == "sy_dist") {
               p.distance.value = data::parseFloatData(data);
           }
           else if (column == "ESM") {
               p.esm = data::parseFloatData(data);
           }
           else if (column == "TSM") {
               p.tsm = data::parseFloatData(data);
           }
       }

       p.multiSystemFlag = (p.nPlanets > 1);

       // Compute galactic position of system
       bool hasPos = p.ra.hasValue() && p.dec.hasValue() && p.distance.hasValue();
       if (hasPos) {
           const float ra = p.ra.value;
           const float dec = p.dec.value;
           p.position = icrsToGalacticCartesian(ra, dec, p.distance.value);
       }

       // If unknown, compute planet mass
       // TODO: move to python
       if ((!p.mass.hasValue()) && p.radius.hasValue()) {
           float r = p.radius.value;

           // Mass radius relationship from Chen & Kipping (2017)
           // See eq. (2) in https://arxiv.org/pdf/1805.03671.pdf

           if (r < 1.23f) { // Terran
               p.mass.value = 0.9718f * glm::pow(r, 3.58f);
           }
           else if (r < 14.26) { // Neptunian
               p.mass.value = 1.436f * glm::pow(r, 1.70f);
           }
           // TODO: constant for larger planets (Jovian & Stellar)
           // Use their python package!
           // Their paper: https://iopscience.iop.org/article/10.3847/1538-4357/834/1/17
       }

       // TODO: move to python
       if (p.radius.hasValue() && p.mass.hasValue()) {
           constexpr const double G = 6.67430e-11;
           const double r = static_cast<double>(p.radius.value) * EarthRadius;
           const double M = static_cast<double>(p.mass.value) * EarthMass;
           p.surfaceGravity.value = static_cast<float>((G * M) / (r * r));
       }

       p.id = idCounter;
       idCounter++;

       planets.push_back(p);
   }

   planets.shrink_to_fit();
   return planets;
}

} // namespace openspace::exoplanets
