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

#include <openspace/navigation/waypoint.h>

#include <openspace/engine/globals.h>
#include <openspace/engine/moduleengine.h>
#include <openspace/navigation/navigationhandler.h>
#include <openspace/navigation/navigationstate.h>
#include <openspace/navigation/pathnavigator.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/query/query.h>
#include <ghoul/logging/logmanager.h>

namespace {
    constexpr const char* _loggerCat = "Waypoint";
} // namespace

namespace openspace::interaction {

Waypoint::Waypoint(const glm::dvec3& pos, const glm::dquat& rot, const std::string& ref)
    : _nodeIdentifier(ref)
{
    _pose = { pos, rot };

    const SceneGraphNode* node = sceneGraphNode(_nodeIdentifier);
    if (!node) {
        LERROR(fmt::format("Could not find node '{}'", _nodeIdentifier));
        return;
    }
    _validBoundingSphere = findValidBoundingSphere(node);
}

Waypoint::Waypoint(const NavigationState& ns) {
    const SceneGraphNode* anchorNode = sceneGraphNode(ns.anchor);

    if (!anchorNode) {
        LERROR(fmt::format("Could not find node '{}' to target", ns.anchor));
        return;
    }

    _nodeIdentifier = ns.anchor;
    _validBoundingSphere = findValidBoundingSphere(anchorNode);
    _pose = ns.cameraPose();
}

CameraPose Waypoint::pose() const {
    return _pose;
}

glm::dvec3 Waypoint::position() const {
    return _pose.position;
}

glm::dquat Waypoint::rotation() const {
    return _pose.rotation;
}

SceneGraphNode* Waypoint::node() const {
    return sceneGraphNode(_nodeIdentifier);
}

std::string Waypoint::nodeIdentifier() const {
    return _nodeIdentifier;
}

double Waypoint::validBoundingSphere() const {
    return _validBoundingSphere;
}

double Waypoint::findValidBoundingSphere(const SceneGraphNode* node) {
    double bs = static_cast<double>(node->boundingSphere());

    const double minValidBoundingSphere =
        global::navigationHandler->pathNavigator().minValidBoundingSphere();

    if (bs < minValidBoundingSphere) {
        // If the bs of the target is too small, try to find a good value in a child node.
        // Only check the closest children, to avoid deep traversal in the scene graph.
        // Alsp. the chance to find a bounding sphere that represents the visual size of
        // the target well is higher for these nodes
        for (SceneGraphNode* child : node->children()) {
            bs = static_cast<double>(child->boundingSphere());
            if (bs > minValidBoundingSphere) {
                LWARNING(fmt::format(
                    "The scene graph node '{}' has no, or a very small, bounding sphere. "
                    "Using bounding sphere of child node '{}' in computations.",
                    node->identifier(),
                    child->identifier()
                ));

                return bs;
            }
        }

        LWARNING(fmt::format(
            "The scene graph node '{}' has no, or a very small, bounding sphere. Using "
            "minimal value. This might lead to unexpected results.",
            node->identifier())
        );

        bs = minValidBoundingSphere;
    }

    return bs;
}

} // namespace openspace::interaction
