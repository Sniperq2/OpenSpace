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

#include <openspace/properties/scalar/doubleproperty.h>

#include <ghoul/lua/ghoul_lua.h>

namespace openspace::properties {

DoubleProperty::DoubleProperty(Property::PropertyInfo info, double value,
                               double minValue, double maxValue, double stepValue)
    : NumericalProperty<double>(std::move(info), value, minValue, maxValue, stepValue)
{}

std::string DoubleProperty::className() const {
    return "DoubleProperty";
}

int DoubleProperty::typeLua() const {
    return LUA_TNUMBER;
}

double DoubleProperty::fromLuaConversion(lua_State* state, bool& success) const {
    success = (lua_isnumber(state, -1) == 1);
    if (success) {
        double val = lua_tonumber(state, -1);
        return val;
    }
    else {
        return 0.0;
    }
}

} // namespace openspace::properties
