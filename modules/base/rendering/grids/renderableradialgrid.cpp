
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

#include <modules/base/rendering/grids/renderableradialgrid.h>

#include <modules/base/basemodule.h>
#include <openspace/engine/globals.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/util/updatestructures.h>
#include <openspace/documentation/verifier.h>
#include <ghoul/glm.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/opengl/openglstatecache.h>
#include <ghoul/opengl/programobject.h>
#include <optional>

namespace {
    constexpr const char* ProgramName = "GridProgram";

    constexpr openspace::properties::Property::PropertyInfo ColorInfo = {
        "Color",
        "Color",
        "This value determines the color of the grid lines that are rendered."
    };

    constexpr openspace::properties::Property::PropertyInfo GridSegmentsInfo = {
        "GridSegments",
        "Number of Grid Segments",
        "Specifies the number of segments for the grid, in the radial and angular "
        "direction respectively"
    };

    constexpr openspace::properties::Property::PropertyInfo CircleSegmentsInfo = {
        "CircleSegments",
        "Number of Circle Segments",
        "This value specifies the number of segments that is used to render each circle "
        "in the grid"
    };

    constexpr openspace::properties::Property::PropertyInfo LineWidthInfo = {
        "LineWidth",
        "Line Width",
        "This value specifies the line width of the spherical grid."
    };

    constexpr openspace::properties::Property::PropertyInfo RadiiInfo = {
        "Radii",
        "Inner and Outer Radius",
        "The radii values that determine the size of the circular grid. The first value "
        "is the radius of the inmost ring and the second is the radius of the outmost "
        "ring."
    };

    struct [[codegen::Dictionary(RenderableRadialGrid)]] Parameters {
        // [[codegen::verbatim(ColorInfo.description)]]
        std::optional<glm::vec3> color [[codegen::color()]];

        // [[codegen::verbatim(GridSegmentsInfo.description)]]
        std::optional<glm::ivec2> gridSegments;

        // [[codegen::verbatim(CircleSegmentsInfo.description)]]
        std::optional<int> circleSegments;

        // [[codegen::verbatim(LineWidthInfo.description)]]
        std::optional<float> lineWidth;

        // [[codegen::verbatim(RadiiInfo.description)]]
        std::optional<glm::vec2> radii;
    };
#include "renderableradialgrid_codegen.cpp"
} // namespace

namespace openspace {

documentation::Documentation RenderableRadialGrid::Documentation() {
    return codegen::doc<Parameters>("base_renderable_radialgrid");
}

RenderableRadialGrid::RenderableRadialGrid(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
    , _color(ColorInfo, glm::vec3(0.5f), glm::vec3(0.f), glm::vec3(1.f))
    , _gridSegments(GridSegmentsInfo, glm::ivec2(1), glm::ivec2(1), glm::ivec2(200))
    , _circleSegments(CircleSegmentsInfo, 36, 4, 200)
    , _lineWidth(LineWidthInfo, 0.5f, 1.f, 20.f)
    , _radii(RadiiInfo, glm::vec2(0.f, 1.f), glm::vec2(0.f), glm::vec2(20.f))
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    addProperty(_opacity);
    registerUpdateRenderBinFromOpacity();

    _color = p.color.value_or(_color);
    _color.setViewOption(properties::Property::ViewOptions::Color);
    addProperty(_color);

    _gridSegments = p.gridSegments.value_or(_gridSegments);
    _gridSegments.onChange([&]() { _gridIsDirty = true; });
    addProperty(_gridSegments);

    _circleSegments = p.circleSegments.value_or(_circleSegments);
    _circleSegments.onChange([&]() {
        if (_circleSegments.value() % 2 == 1) {
            _circleSegments = _circleSegments - 1;
        }
        _gridIsDirty = true;
    });
    addProperty(_circleSegments);

    _lineWidth = p.lineWidth.value_or(_lineWidth);
    addProperty(_lineWidth);

    _radii = p.radii.value_or(_radii);
    _radii.setViewOption(properties::Property::ViewOptions::MinMaxRange);
    _radii.onChange([&]() { _gridIsDirty = true; });

    addProperty(_radii);
}

bool RenderableRadialGrid::isReady() const {
    return _gridProgram != nullptr;
}

void RenderableRadialGrid::initializeGL() {
    _gridProgram = BaseModule::ProgramObjectManager.request(
        ProgramName,
        []() -> std::unique_ptr<ghoul::opengl::ProgramObject> {
            return global::renderEngine->buildRenderProgram(
                ProgramName,
                absPath("${MODULE_BASE}/shaders/grid_vs.glsl"),
                absPath("${MODULE_BASE}/shaders/grid_fs.glsl")
            );
        }
    );
}

void RenderableRadialGrid::deinitializeGL() {
    BaseModule::ProgramObjectManager.release(
        ProgramName,
        [](ghoul::opengl::ProgramObject* p) {
            global::renderEngine->removeRenderProgram(p);
        }
    );
    _gridProgram = nullptr;
}

void RenderableRadialGrid::render(const RenderData& data, RendererTasks&) {
    _gridProgram->activate();

    const glm::dmat4 modelTransform =
        glm::translate(glm::dmat4(1.0), data.modelTransform.translation) * // Translation
        glm::dmat4(data.modelTransform.rotation) *  // Spice rotation
        glm::scale(glm::dmat4(1.0), glm::dvec3(data.modelTransform.scale));

    const glm::dmat4 modelViewTransform = data.camera.combinedViewMatrix() *
                                          modelTransform;

    _gridProgram->setUniform("modelViewTransform", modelViewTransform);
    _gridProgram->setUniform(
        "MVPTransform",
        glm::dmat4(data.camera.projectionMatrix()) * modelViewTransform
    );
    _gridProgram->setUniform("opacity", _opacity);
    _gridProgram->setUniform("gridColor", _color);

    // Change GL state:
#ifndef __APPLE__
    glLineWidth(_lineWidth);
#else
    glLineWidth(1.f);
#endif
    glEnablei(GL_BLEND, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glDepthMask(false);

    for (GeometryData& c : _circles) {
        c.render();
    }

    _lines.render();

    _gridProgram->deactivate();

    // Restore GL State
    global::renderEngine->openglStateCache().resetBlendState();
    global::renderEngine->openglStateCache().resetLineState();
    global::renderEngine->openglStateCache().resetDepthState();
}

void RenderableRadialGrid::update(const UpdateData&) {
    if (!_gridIsDirty) {
        return;
    }

    const float innerRadius = _radii.value().x;
    const float outerRadius = _radii.value().y;

    // Circles
    const int nRadialSegments = _gridSegments.value()[0];
    const float fnCircles = static_cast<float>(nRadialSegments);
    const float deltaRadius = (outerRadius - innerRadius) / fnCircles;

    const bool hasInnerRadius = innerRadius > 0.f;
    const int nCircles = hasInnerRadius ? nRadialSegments : nRadialSegments + 1;

    _circles.clear();
    _circles.reserve(nCircles);

    auto addRing = [this](int nSegments, float radius) {
        std::vector<rendering::helper::Vertex> vertices =
            rendering::helper::createRing(nSegments, radius);

        _circles.push_back(GeometryData(GL_LINE_STRIP));
        _circles.back().varray = rendering::helper::convert(vertices);
        _circles.back().update();
    };

    // add an extra inmost circle
    if (hasInnerRadius) {
        addRing(_circleSegments, innerRadius);
    }

    for (int i = 0; i < nRadialSegments; ++i) {
        const float ri = static_cast<float>(i + 1) * deltaRadius + innerRadius;
        addRing(_circleSegments, ri);
    }

    // Lines
    const int nLines = _gridSegments.value()[1];
    const int nVertices = 2 * nLines;

    _lines.varray.clear();
    _lines.varray.reserve(nVertices);

    if (nLines > 1) {
        std::vector<rendering::helper::Vertex> outerVertices =
            rendering::helper::createRing(nLines, outerRadius);

        std::vector<rendering::helper::Vertex> innerVertices =
            rendering::helper::createRing(nLines, innerRadius);

        for (int i = 0; i < nLines; ++i) {
            const rendering::helper::VertexXYZ vOut =
                rendering::helper::convertToXYZ(outerVertices[i]);

            const rendering::helper::VertexXYZ vIn =
                rendering::helper::convertToXYZ(innerVertices[i]);

            _lines.varray.push_back(vOut);
            _lines.varray.push_back(vIn);
        }
    }
    _lines.update();

    _gridIsDirty = false;
}

RenderableRadialGrid::GeometryData::GeometryData(GLenum renderMode)
    : mode(renderMode)
{
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

RenderableRadialGrid::GeometryData::GeometryData(GeometryData&& other) noexcept {
    if (this == &other) return;
    vao = other.vao;
    vbo = other.vbo;
    varray = std::move(other.varray);
    mode = other.mode;

    other.vao = 0;
    other.vbo = 0;
}

RenderableRadialGrid::GeometryData&
RenderableRadialGrid::GeometryData::operator=(GeometryData&& other) noexcept
{
    if (this != &other) {
        vao = other.vao;
        vbo = other.vbo;
        varray = std::move(other.varray);
        mode = other.mode;

        other.vao = 0;
        other.vbo = 0;
    }
    return *this;
}

RenderableRadialGrid::GeometryData::~GeometryData() {
    glDeleteVertexArrays(1, &vao);
    vao = 0;

    glDeleteBuffers(1, &vbo);
    vbo = 0;
}

void RenderableRadialGrid::GeometryData::update() {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        varray.size() * sizeof(rendering::helper::VertexXYZ),
        varray.data(),
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(rendering::helper::VertexXYZ),
        nullptr
    );
}

void RenderableRadialGrid::GeometryData::render() {
    glBindVertexArray(vao);
    glDrawArrays(mode, 0, static_cast<GLsizei>(varray.size()));
    glBindVertexArray(0);
}

} // namespace openspace
