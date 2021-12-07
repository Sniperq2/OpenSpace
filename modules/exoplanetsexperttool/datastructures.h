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

#ifndef __OPENSPACE_MODULE_EXOPLANETSEXPERTTOOL___DATASTRUCTURES___H__
#define __OPENSPACE_MODULE_EXOPLANETSEXPERTTOOL___DATASTRUCTURES___H__

#include <optional>
#include <string>

// @TODO: separate namespace
namespace openspace::exoplanets {

// Represent a data point with upper and lower uncertainty values
struct DataPoint {
    float value = std::numeric_limits<float>::quiet_NaN();
    float errorUpper = 0.f;
    float errorLower = 0.f;

    // TODO:move to a cpp file
    bool hasValue() const {
        return !std::isnan(value);
    };
};

struct ExoplanetItem {
    int id; // Id used for UI (same as row number in data file)
    std::string planetName;
    std::string hostName;
    DataPoint radius; // in Earth radii
    DataPoint mass; // in Earth mass
    DataPoint eqilibriumTemp;  // in Kelvin
    DataPoint eccentricity;
    DataPoint semiMajorAxis; // in AU
    DataPoint period; // in days
    DataPoint inclination;
    float tsm = std::numeric_limits<float>::quiet_NaN();
    float esm = std::numeric_limits<float>::quiet_NaN();
    bool multiSystemFlag;
    int nStars;
    int nPlanets;
    int discoveryYear;
    DataPoint surfaceGravity;
    DataPoint starEffectiveTemp; // in Kelvin
    DataPoint starAge; // in Gyr
    DataPoint starRadius; // in Solar radii
    DataPoint starMetallicity; // in dex
    std::string starMetallicityRatio;
    DataPoint magnitudeJ; // apparent magnitude in the J band (star)
    DataPoint magnitudeK; // apparent magnitude in the K band (star)

    DataPoint ra; // in decimal degrees
    DataPoint dec; // in decimal degrees
    DataPoint distance; // in Parsec

    // in Parsec
    std::optional<glm::dvec3> position = std::nullopt;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_EXOPLANETSEXPERTTOOL___DATASTRUCTURES___H__
