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

#include <modules/base/rendering/grids/renderableboxgrid.h>

#include <modules/base/basemodule.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/openspaceengine.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/util/updatestructures.h>
#include <openspace/documentation/verifier.h>
#include <ghoul/glm.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/opengl/openglstatecache.h>
#include <ghoul/opengl/programobject.h>

namespace {
    constexpr const char* ProgramName = "GridProgram";

    constexpr openspace::properties::Property::PropertyInfo ColorInfo = {
        "Color",
        "Color",
        "This value determines the color of the grid lines that are rendered."
    };

    constexpr openspace::properties::Property::PropertyInfo LineWidthInfo = {
        "LineWidth",
        "Line Width",
        "This value specifies the line width of the spherical grid."
    };

    constexpr openspace::properties::Property::PropertyInfo SizeInfo = {
        "Size",
        "Grid Size",
        "This value species the size of each dimensions of the box"
    };

    struct [[codegen::Dictionary(RenderableBoxGrid)]] Parameters {
        // [[codegen::verbatim(ColorInfo.description)]]
        std::optional<glm::vec3> color [[codegen::color()]];

        // [[codegen::verbatim(LineWidthInfo.description)]]
        std::optional<float> lineWidth;

        // [[codegen::verbatim(SizeInfo.description)]]
        std::optional<glm::vec3> size;
    };
#include "renderableboxgrid_codegen.cpp"
} // namespace

namespace openspace {

documentation::Documentation RenderableBoxGrid::Documentation() {
    return codegen::doc<Parameters>("base_renderable_boxgrid");
}

RenderableBoxGrid::RenderableBoxGrid(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
    , _color(ColorInfo, glm::vec3(0.5f), glm::vec3(0.f), glm::vec3(1.f))
    , _lineWidth(LineWidthInfo, 0.5f, 1.f, 20.f)
    , _size(SizeInfo, glm::vec3(1.f), glm::vec3(1.f), glm::vec3(100.f))
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    addProperty(_opacity);
    registerUpdateRenderBinFromOpacity();

    _color = p.color.value_or(_color);
    _color.setViewOption(properties::Property::ViewOptions::Color);
    addProperty(_color);

    _lineWidth = p.lineWidth.value_or(_lineWidth);
    addProperty(_lineWidth);

    _size = p.size.value_or(_size);
    _size.onChange([&]() { _gridIsDirty = true; });
    addProperty(_size);
}

bool RenderableBoxGrid::isReady() const {
    return _gridProgram != nullptr;
}

void RenderableBoxGrid::initializeGL() {
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

    glGenVertexArrays(1, &_vaoID);
    glGenBuffers(1, &_vBufferID);

    glBindVertexArray(_vaoID);
    glBindBuffer(GL_ARRAY_BUFFER, _vBufferID);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void RenderableBoxGrid::deinitializeGL() {
    glDeleteVertexArrays(1, &_vaoID);
    _vaoID = 0;

    glDeleteBuffers(1, &_vBufferID);
    _vBufferID = 0;

    BaseModule::ProgramObjectManager.release(
        ProgramName,
        [](ghoul::opengl::ProgramObject* p) {
            global::renderEngine->removeRenderProgram(p);
        }
    );
    _gridProgram = nullptr;
}

void RenderableBoxGrid::render(const RenderData& data, RendererTasks&){
    _gridProgram->activate();

    glm::dmat4 modelTransform =
        glm::translate(glm::dmat4(1.0), data.modelTransform.translation) * // Translation
        glm::dmat4(data.modelTransform.rotation) *  // Spice rotation
        glm::scale(glm::dmat4(1.0), glm::dvec3(data.modelTransform.scale));

    glm::dmat4 modelViewTransform = data.camera.combinedViewMatrix() * modelTransform;

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
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnablei(GL_BLEND, 0);
    glEnable(GL_LINE_SMOOTH);
    glDepthMask(false);

    glBindVertexArray(_vaoID);
    glDrawArrays(_mode, 0, static_cast<GLsizei>(_varray.size()));
    glBindVertexArray(0);

    _gridProgram->deactivate();

    // Restore GL State
    global::renderEngine->openglStateCache().resetBlendState();
    global::renderEngine->openglStateCache().resetLineState();
    global::renderEngine->openglStateCache().resetDepthState();
}

void RenderableBoxGrid::update(const UpdateData&) {
    if (_gridIsDirty) {
        const glm::vec3 llf = -_size.value() / 2.f;
        const glm::vec3 urb =  _size.value() / 2.f;

        //     7
        //      --------------------  6
        //     /                   /
        //    /|                  /|
        // 4 / |                 / |
        //  x-------------------x  |
        //  |  |                |5 |
        //  |  |                |  |
        //  |  |                |  |
        //  | 3/----------------|--/ 2
        //  | /                 | /
        //  |/                  |/
        //  x-------------------x
        // 0                     1
        //
        //
        //  For Line strip:
        //  0 -> 1 -> 2 -> 3 -> 0 -> 4 -> 5 -> 6 -> 7 -> 4 -> 5(d) -> 1 -> 2(d) -> 6
        //  -> 7(d) -> 3

        const glm::vec3 v0 = glm::vec3(llf.x, llf.y, llf.z);
        const glm::vec3 v1 = glm::vec3(urb.x, llf.y, llf.z);
        const glm::vec3 v2 = glm::vec3(urb.x, urb.y, llf.z);
        const glm::vec3 v3 = glm::vec3(llf.x, urb.y, llf.z);
        const glm::vec3 v4 = glm::vec3(llf.x, llf.y, urb.z);
        const glm::vec3 v5 = glm::vec3(urb.x, llf.y, urb.z);
        const glm::vec3 v6 = glm::vec3(urb.x, urb.y, urb.z);
        const glm::vec3 v7 = glm::vec3(llf.x, urb.y, urb.z);

        _varray.clear();
        _varray.reserve(16);

        // First add the bounds
        _varray.push_back({ v0.x, v0.y, v0.z });
        _varray.push_back({ v1.x, v1.y, v1.z });
        _varray.push_back({ v2.x, v2.y, v2.z });
        _varray.push_back({ v3.x, v3.y, v3.z });
        _varray.push_back({ v0.x, v0.y, v0.z });
        _varray.push_back({ v4.x, v4.y, v4.z });
        _varray.push_back({ v5.x, v5.y, v5.z });
        _varray.push_back({ v6.x, v6.y, v6.z });
        _varray.push_back({ v7.x, v7.y, v7.z });
        _varray.push_back({ v4.x, v4.y, v4.z });
        _varray.push_back({ v5.x, v5.y, v5.z });
        _varray.push_back({ v1.x, v1.y, v1.z });
        _varray.push_back({ v2.x, v2.y, v2.z });
        _varray.push_back({ v6.x, v6.y, v6.z });
        _varray.push_back({ v7.x, v7.y, v7.z });
        _varray.push_back({ v3.x, v3.y, v3.z });

        glBindVertexArray(_vaoID);
        glBindBuffer(GL_ARRAY_BUFFER, _vBufferID);
        glBufferData(
            GL_ARRAY_BUFFER,
            _varray.size() * sizeof(Vertex),
            _varray.data(),
            GL_STATIC_DRAW
        );

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
        glBindVertexArray(0);

        _gridIsDirty = false;
    }
}

} // namespace openspace
