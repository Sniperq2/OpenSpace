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

#include <openspace/camera/camerapose.h>
#include <openspace/interaction/mouseinputstate.h>
#include <openspace/interaction/keyboardinputstate.h>
#include <openspace/navigation/orbitalnavigator.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/util/updatestructures.h>
#include <openspace/query/query.h>
#include <openspace/engine/globals.h>
#include <openspace/events/event.h>
#include <openspace/events/eventengine.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/easing.h>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/vector_angle.hpp>

#include <cmath>

namespace {
    constexpr const char* _loggerCat = "OrbitalNavigator";

    constexpr const double AngleEpsilon = 1E-7;
    constexpr const double DistanceRatioAimThreshold = 1E-4;

    constexpr const openspace::properties::Property::PropertyInfo AnchorInfo = {
        "Anchor",
        "Anchor",
        "The name of the scene graph node that is the origin of the camera interaction. "
        "The camera follows, orbits and dollies towards this node. "
        "Any scene graph node can be the anchor node."
    };

    constexpr const openspace::properties::Property::PropertyInfo AimInfo = {
        "Aim",
        "Aim",
        "The name of the scene graph node that is the aim of the camera. "
        "The camera direction is relative to the vector from the camera position "
        "to this node."
    };

    constexpr const openspace::properties::Property::PropertyInfo
        RetargetAnchorInfo =
    {
        "RetargetAnchor",
        "Retarget Anchor",
        "When triggered, this property starts an interpolation to reset the "
        "camera direction to the anchor node."
    };

    constexpr const openspace::properties::Property::PropertyInfo
        RetargetAimInfo =
    {
        "RetargetAim",
        "Retarget Aim",
        "When triggered, this property starts an interpolation to reset the "
        "camera direction to the aim node."
    };

    constexpr openspace::properties::Property::PropertyInfo RollFrictionInfo = {
        "RollFriction",
        "Roll Friction",
        "If this is enabled, a small friction is applied to the rolling part of the "
        "camera motion, thus slowing it down within a small time period. If this value "
        "is disabled, the camera will roll forever."
    };

    constexpr openspace::properties::Property::PropertyInfo RotationalFrictionInfo =
    {
        "RotationalFriction",
        "Rotational Friction",
        "If this is enabled, a small friction is applied to the rotational part of the "
        "camera motion, thus slowing it down within a small time period. If this value "
        "is disabled, the camera will rotate forever."
    };

    constexpr openspace::properties::Property::PropertyInfo ZoomFrictionInfo = {
        "ZoomFriction",
        "Zoom Friction",
        "If this is enabled, a small friction is applied to the zoom part of the camera "
        "motion, thus slowing it down within a small time period. If this value is "
        "disabled, the camera will zoom in or out forever."
    };

    constexpr openspace::properties::Property::PropertyInfo MouseSensitivityInfo = {
        "MouseSensitivity",
        "Mouse Sensitivity",
        "Determines the sensitivity of the camera motion thorugh the mouse. The lower "
        "the sensitivity is the less impact a mouse motion will have."
    };

    constexpr openspace::properties::Property::PropertyInfo JoystickSensitivityInfo = {
        "JoystickSensitivity",
        "Joystick Sensitivity",
        "Determines the sensitivity of the camera motion thorugh a joystick. The lower "
        "the sensitivity is the less impact a joystick motion will have."
    };

    constexpr openspace::properties::Property::PropertyInfo WebsocketSensitivityInfo = {
        "WebsocketSensitivity",
        "Websocket Sensitivity",
        "Determines the sensitivity of the camera motion thorugh a websocket. The lower "
        "the sensitivity is the less impact a webstick motion will have."
    };

    constexpr openspace::properties::Property::PropertyInfo FrictionInfo = {
        "Friction",
        "Friction Factor",
        "Determines the factor that is applied if the 'Roll Friction', 'Rotational "
        "Friction', and 'Zoom Friction' values are enabled. The lower this value is, the "
        "faster the camera movements will stop."
    };

    constexpr openspace::properties::Property::PropertyInfo FollowAnchorNodeInfo = {
        "FollowAnchorNodeRotation",
        "Follow Anchor Node Rotation",
        "If true, the camera will rotate with the current achor node if within a "
        "certain distance from it. When this happens, the object will appear fixed in "
        "relation to the camera. The distance at which the change happens is controlled "
        "through another property."
    };

    constexpr openspace::properties::Property::PropertyInfo
        FollowAnchorNodeDistanceInfo = {
        "FollowAnchorNodeRotationDistance",
        "Follow Anchor Node Rotation Distance",
        "A factor used to determine the distance at which the camera starts rotating "
        "with the anchor node. The actual distance will be computed by multiplying "
        "this factor with the approximate radius of the node."
    };

    constexpr openspace::properties::Property::PropertyInfo MinimumDistanceInfo = {
        "MinimumAllowedDistance",
        "Minimum Allowed Distance",
        "Limits how close the camera can get to an object. The distance is given in "
        "meters above the surface."
    };

    constexpr openspace::properties::Property::PropertyInfo VelocityZoomControlInfo = {
        "VelocityZoomControl",
        "Velocity Zoom Control",
        "Controls the velocity of the camera motion when zooming in to the focus node "
        "on a linear flight. The higher the value the faster the camera will move "
        "towards the focus."
    };

    constexpr openspace::properties::Property::PropertyInfo ApplyLinearFlightInfo = {
        "ApplyLinearFlight",
        "Apply Linear Flight",
        "This property makes the camera move to the specified distance "
        "'DestinationDistance' while facing the anchor"
    };

    constexpr openspace::properties::Property::PropertyInfo FlightDestinationDistInfo = {
        "FlightDestinationDistance",
        "Flight Destination Distance",
        "The final distance we want to fly to, with regards to the anchor node."
    };

    constexpr openspace::properties::Property::PropertyInfo FlightDestinationFactorInfo =
    {
        "FlightDestinationFactor",
        "Flight Destination Factor",
        "The minimal distance factor that we need to reach to end linear flight."
    };

    constexpr openspace::properties::Property::PropertyInfo
        StereoInterpolationTimeInfo = {
            "StereoInterpolationTime",
            "Stereo Interpolation Time",
            "The time to interpolate to a new stereoscopic depth "
            "when the anchor node is changed, in seconds."
    };

    constexpr openspace::properties::Property::PropertyInfo
        RetargetInterpolationTimeInfo = {
            "RetargetAnchorInterpolationTime",
            "Retarget Interpolation Time",
            "The time to interpolate the camera rotation "
            "when the anchor or aim node is changed, in seconds."
    };

    constexpr openspace::properties::Property::PropertyInfo
        FollowRotationInterpTimeInfo = {
            "FollowRotationInterpolationTime",
            "Follow Rotation Interpolation Time",
            "The interpolation time when toggling following focus node rotation."
    };

    constexpr openspace::properties::Property::PropertyInfo InvertMouseButtons = {
        "InvertMouseButtons",
        "Invert Left and Right Mouse Buttons",
        "If this value is 'false', the left mouse button causes the camera to rotate "
        "around the object and the right mouse button causes the zooming motion. If this "
        "value is 'true', these two functionalities are reversed."
    };

    constexpr openspace::properties::Property::PropertyInfo
        UseAdaptiveStereoscopicDepthInfo = {
            "UseAdaptiveStereoscopicDepth",
            "Adaptive Steroscopic Depth",
            "Dynamically adjust the view scaling based on the distance to the surface of "
            "the anchor and aim nodes. If enabled, view scale will be set to "
            "StereoscopicDepthOfFocusSurface / min(anchorDistance, aimDistance). "
            "If disabled, view scale will be set to 10^StaticViewScaleExponent."
        };

    constexpr openspace::properties::Property::PropertyInfo
        StaticViewScaleExponentInfo = {
            "StaticViewScaleExponent",
            "Static View Scale Exponent",
            "Statically scale the world by 10^StaticViewScaleExponent. "
            "Only used if UseAdaptiveStereoscopicDepthInfo is set to false."
        };

    constexpr openspace::properties::Property::PropertyInfo
        StereoscopicDepthOfFocusSurfaceInfo = {
            "StereoscopicDepthOfFocusSurface",
            "Stereoscopic Depth of the Surface in Focus",
            "Set the stereoscopically perceived distance (in meters) to the closest "
            "point out of the surface of the anchor and the center of the aim node. "
            "Only used if UseAdaptiveStereoscopicDepthInfo is set to true."
        };

    constexpr openspace::properties::Property::PropertyInfo ApplyIdleBehaviorInfo = {
        "ApplyIdleBehavior",
        "Apply Idle Behavior",
        "When set to true, the chosen idle behavior will be applied to the camera, "
        "moving the camera accordingly."
    };

    constexpr openspace::properties::Property::PropertyInfo IdleBehaviorInfo = {
        "IdleBehavior",
        "Idle Behavior",
        "The chosen camera behavior that will be triggered when the idle behavior is "
        "applied. Each option represents a predefined camera behavior."
    };

    constexpr openspace::properties::Property::PropertyInfo IdleBehaviorSpeedInfo = {
        "SpeedFactor",
        "Speed Factor",
        "A factor that can be used to increase or slow down the speed of an applied "
        "idle behavior."
    };

    constexpr openspace::properties::Property::PropertyInfo
        AbortOnCameraInteractionInfo = {
        "AbortOnCameraInteraction",
        "Abort on Camera Interaction",
        "If set to true, the idle behavior is aborted on camera interaction. If false, "
        "the behavior will be reapplied after the interaction. Examples of camera "
        "interaction are: changing the anchor node, starting a camera path or session "
        "recording playback, or navigating manually using an input device."
    };

    constexpr openspace::properties::Property::PropertyInfo
        IdleBehaviorDampenInterpolationTimeInfo = {
        "DampenInterpolationTime",
        "Start/End Dampen Interpolation Time",
        "The time to interpolate to/from full speed when an idle behavior is triggered "
        "or canceled, in seconds."
    };
} // namespace

namespace openspace::interaction {

OrbitalNavigator::Friction::Friction()
    : properties::PropertyOwner({ "Friction" })
    , roll(RollFrictionInfo, true)
    , rotational(RotationalFrictionInfo, true)
    , zoom(ZoomFrictionInfo, true)
    , friction(FrictionInfo, 0.5f, 0.f, 1.f)
{
    addProperty(roll);
    addProperty(rotational);
    addProperty(zoom);
    addProperty(friction);
}

OrbitalNavigator::LinearFlight::LinearFlight()
    : properties::PropertyOwner({ "LinearFlight" })
    , apply(ApplyLinearFlightInfo, false)
    , destinationDistance(FlightDestinationDistInfo, 2e8f, 10.f, 1e10f)
    , destinationFactor(FlightDestinationFactorInfo, 1E-4, 1E-6, 0.5, 1E-3)
    , velocitySensitivity(VelocityZoomControlInfo, 3.5f, 0.001f, 20.f)
{
    addProperty(apply);
    addProperty(velocitySensitivity);
    destinationDistance.setExponent(5.f);
    addProperty(destinationDistance);
    addProperty(destinationFactor);
}

OrbitalNavigator::IdleBehavior::IdleBehavior()
    : properties::PropertyOwner({ "IdleBehavior" })
    , apply(ApplyIdleBehaviorInfo, false)
    , chosenBehavior(IdleBehaviorInfo)
    , speedScale(IdleBehaviorSpeedInfo, 1.f, 0.01f, 5.f)
    , abortOnCameraInteraction(AbortOnCameraInteractionInfo, true)
    , dampenInterpolationTime(IdleBehaviorDampenInterpolationTimeInfo, 0.5f, 0.f, 10.f)
{
    addProperty(apply);
    chosenBehavior.addOptions({
        { IdleBehavior::Behavior::Orbit, "Orbit" },
        { IdleBehavior::Behavior::OrbitAtConstantLat, "OrbitAtConstantLatitude" },
        { IdleBehavior::Behavior::OrbitAroundUp, "OrbitAroundUp" }
    });
    chosenBehavior = IdleBehavior::Behavior::Orbit;
    addProperty(chosenBehavior);
    addProperty(speedScale);
    addProperty(abortOnCameraInteraction);
    addProperty(dampenInterpolationTime);
}

OrbitalNavigator::OrbitalNavigator()
    : properties::PropertyOwner({ "OrbitalNavigator" })
    , _anchor(AnchorInfo)
    , _aim(AimInfo)
    , _retargetAnchor(RetargetAnchorInfo)
    , _retargetAim(RetargetAimInfo)
    , _followAnchorNodeRotation(FollowAnchorNodeInfo, true)
    , _followAnchorNodeRotationDistance(FollowAnchorNodeDistanceInfo, 5.f, 0.f, 20.f)
    , _minimumAllowedDistance(MinimumDistanceInfo, 10.0f, 0.0f, 10000.f)
    , _mouseSensitivity(MouseSensitivityInfo, 15.f, 1.f, 50.f)
    , _joystickSensitivity(JoystickSensitivityInfo, 10.f, 1.0f, 50.f)
    , _websocketSensitivity(WebsocketSensitivityInfo, 5.f, 1.0f, 50.f)
    , _useAdaptiveStereoscopicDepth(UseAdaptiveStereoscopicDepthInfo, true)
    , _stereoscopicDepthOfFocusSurface(
        StereoscopicDepthOfFocusSurfaceInfo,
        21500,
        0.25,
        500000
    )
    , _staticViewScaleExponent(StaticViewScaleExponentInfo, 0.f, -30, 10)
    , _retargetInterpolationTime(RetargetInterpolationTimeInfo, 2.0, 0.0, 10.0)
    , _stereoInterpolationTime(StereoInterpolationTimeInfo, 8.0, 0.0, 10.0)
    , _followRotationInterpolationTime(FollowRotationInterpTimeInfo, 1.0, 0.0, 10.0)
    , _invertMouseButtons(InvertMouseButtons, false)
    , _mouseStates(_mouseSensitivity * 0.0001, 1 / (_friction.friction + 0.0000001))
    , _joystickStates(_joystickSensitivity * 0.1, 1 / (_friction.friction + 0.0000001))
    , _websocketStates(_websocketSensitivity, 1 / (_friction.friction + 0.0000001))
{
    _anchor.onChange([this]() {
        if (_anchor.value().empty()) {
            return;
        }
        SceneGraphNode* node = sceneGraphNode(_anchor.value());
        if (node) {
            const SceneGraphNode* previousAnchor = _anchorNode;
            setAnchorNode(node);
            global::eventEngine->publishEvent<events::EventFocusNodeChanged>(
                previousAnchor,
                node
            );
        }
        else {
            LERROR(fmt::format(
                "No scenegraph node with identifier {} exists.", _anchor.value()
            ));
        }
    });

    _aim.onChange([this]() {
        if (_aim.value().empty()) {
            setAimNode(nullptr);
            return;
        }
        SceneGraphNode* node = sceneGraphNode(_aim.value());
        if (node) {
            setAimNode(node);
        }
        else {
            LERROR(fmt::format(
                "No scenegraph node with identifier {} exists.", _aim.value()
            ));
        }
    });

    _retargetAnchor.onChange([this]() {
        startRetargetAnchor();
    });

    _retargetAim.onChange([this]() {
        if (_aimNode && _aimNode != _anchorNode) {
            startRetargetAim();
        }
        else {
            startRetargetAnchor();
        }
    });

    _followRotationInterpolator.setTransferFunction([](double t) {
        const double res = 3.0 * t*t - 2.0 * t*t*t;
        return glm::clamp(res, 0.0, 1.0);
    });

    // The transfer function is used here to get a different interpolation than the one
    // obtained from newValue = lerp(0, currentValue, dt). That one will result in an
    // exponentially decreasing value but we want to be able to control it. Either as
    // a linear interpolation or a smooth step interpolation. Therefore we use
    // newValue = lerp(0, currentValue * f(t) * dt) where f(t) is the transfer function
    // and lerp is a linear iterpolation
    // lerp(endValue, startValue, interpolationParameter).
    //
    // The transfer functions are derived from:
    // f(t) = d/dt ( ln(1 / f_orig(t)) ) where f_orig is the transfer function that would
    // be used if the interpolation was sinply linear between a start value and an end
    // value instead of current value and end value (0) as we use it when inerpoláting.
    // As an example f_orig(t) = 1 - t yields f(t) = 1 / (1 - t) which results in a linear
    // interpolation from 1 to 0.
    auto smoothStepDerivedTranferFunction = [](double t) {
        return (6 * (t + t*t) / (1 - 3 * t*t + 2 * t*t*t));
    };
    _retargetAnchorInterpolator.setTransferFunction(smoothStepDerivedTranferFunction);
    _retargetAimInterpolator.setTransferFunction(smoothStepDerivedTranferFunction);
    _cameraToSurfaceDistanceInterpolator.setTransferFunction(
        smoothStepDerivedTranferFunction
    );

    // Define callback functions for changed properties
    _friction.roll.onChange([&]() {
        _mouseStates.setRotationalFriction(_friction.roll);
        _joystickStates.setRotationalFriction(_friction.roll);
        _websocketStates.setRotationalFriction(_friction.roll);
    });
    _friction.rotational.onChange([&]() {
        _mouseStates.setHorizontalFriction(_friction.rotational);
        _joystickStates.setHorizontalFriction(_friction.rotational);
        _websocketStates.setHorizontalFriction(_friction.rotational);
    });
    _friction.zoom.onChange([&]() {
        _mouseStates.setVerticalFriction(_friction.zoom);
        _joystickStates.setVerticalFriction(_friction.zoom);
        _websocketStates.setVerticalFriction(_friction.zoom);
    });
    _friction.friction.onChange([&]() {
        _mouseStates.setVelocityScaleFactor(1 / (_friction.friction + 0.0000001));
        _joystickStates.setVelocityScaleFactor(1 / (_friction.friction + 0.0000001));
        _websocketStates.setVelocityScaleFactor(1 / (_friction.friction + 0.0000001));
    });

    _mouseSensitivity.onChange([&]() {
        _mouseStates.setSensitivity(_mouseSensitivity * pow(10.0, -4));
    });
    _joystickSensitivity.onChange([&]() {
        _joystickStates.setSensitivity(_joystickSensitivity * 0.1);
    });
    _websocketSensitivity.onChange([&]() {
        _websocketStates.setSensitivity(_websocketSensitivity);
    });

    addPropertySubOwner(_friction);
    addPropertySubOwner(_linearFlight);
    addPropertySubOwner(_idleBehavior);

    _idleBehaviorDampenInterpolator.setTransferFunction(
        ghoul::quadraticEaseInOut<double>
    );
    _idleBehavior.dampenInterpolationTime.onChange([&]() {
        _idleBehaviorDampenInterpolator.setInterpolationTime(
            _idleBehavior.dampenInterpolationTime
        );
     });
    _idleBehavior.apply.onChange([&]() {
        if (_idleBehavior.apply) {
            // Reset velocities to ensure that abort on interaction works correctly
            resetVelocities();
            _invertIdleBehaviorInterpolation = false;
        }
        else {
            _invertIdleBehaviorInterpolation = true;
        }
        _idleBehaviorDampenInterpolator.start();
        _idleBehaviorDampenInterpolator.setInterpolationTime(
            _idleBehavior.dampenInterpolationTime
        );
    });
    addProperty(_anchor);
    addProperty(_aim);
    addProperty(_retargetAnchor);
    addProperty(_retargetAim);
    addProperty(_followAnchorNodeRotation);
    addProperty(_followAnchorNodeRotationDistance);
    addProperty(_minimumAllowedDistance);

    addProperty(_useAdaptiveStereoscopicDepth);
    addProperty(_staticViewScaleExponent);
    _stereoscopicDepthOfFocusSurface.setExponent(3.f);
    addProperty(_stereoscopicDepthOfFocusSurface);

    addProperty(_retargetInterpolationTime);
    addProperty(_stereoInterpolationTime);

    _followRotationInterpolationTime.onChange([&]() {
        _followRotationInterpolator.setInterpolationTime(
            _followRotationInterpolationTime
        );
    });
    _followRotationInterpolator.setInterpolationTime(_followRotationInterpolationTime);
    addProperty(_followRotationInterpolationTime);

    _invertMouseButtons.onChange(
        [this]() { _mouseStates.setInvertMouseButton(_invertMouseButtons); }
    );
    addProperty(_invertMouseButtons);

    addProperty(_mouseSensitivity);
    addProperty(_joystickSensitivity);
    addProperty(_websocketSensitivity);
}

glm::dvec3 OrbitalNavigator::anchorNodeToCameraVector() const {
    return _camera->positionVec3() - anchorNode()->worldPosition();
}

glm::quat OrbitalNavigator::anchorNodeToCameraRotation() const {
    glm::dmat4 invWorldRotation = glm::dmat4(
        glm::inverse(anchorNode()->worldRotationMatrix())
    );
    return glm::quat(invWorldRotation) * glm::quat(_camera->rotationQuaternion());
}

void OrbitalNavigator::resetVelocities() {
    _mouseStates.resetVelocities();
    _joystickStates.resetVelocities();
    _websocketStates.resetVelocities();
    _scriptStates.resetVelocities();

    if (shouldFollowAnchorRotation(_camera->positionVec3())) {
        _followRotationInterpolator.end();
    }
    else {
        _followRotationInterpolator.start();
    }
}

void OrbitalNavigator::updateStatesFromInput(const MouseInputState& mouseInputState,
                                             const KeyboardInputState& keyboardInputState,
                                             double deltaTime)
{
    _mouseStates.updateStateFromInput(mouseInputState, keyboardInputState, deltaTime);
    _joystickStates.updateStateFromInput(*global::joystickInputStates, deltaTime);
    _websocketStates.updateStateFromInput(*global::websocketInputStates, deltaTime);
    _scriptStates.updateStateFromInput(deltaTime);

    bool interactionHappened = _mouseStates.hasNonZeroVelocities() ||
        _joystickStates.hasNonZeroVelocities() ||
        _websocketStates.hasNonZeroVelocities() ||
        _scriptStates.hasNonZeroVelocities();

    if (interactionHappened) {
        updateOnCameraInteraction();
    }
}

void OrbitalNavigator::updateCameraStateFromStates(double deltaTime) {
    if (!_anchorNode) {
        // Bail out if the anchor node is not set
        return;
    }

    const glm::dvec3 anchorPos = _anchorNode->worldPosition();

    const glm::dvec3 prevCameraPosition = _camera->positionVec3();
    const glm::dvec3 anchorDisplacement = _previousAnchorNodePosition.has_value() ?
        (anchorPos - *_previousAnchorNodePosition) :
        glm::dvec3(0.0);

    CameraPose pose = {
        _camera->positionVec3() + anchorDisplacement,
        _camera->rotationQuaternion()
    };

    if (_linearFlight.apply) {
        // Calculate a position handle based on the camera position in world space
        glm::dvec3 camPosToAnchorPosDiff = prevCameraPosition - anchorPos;
        // Use the interaction sphere to get an approximate distance to the node surface
        double nodeRadius = _anchorNode->interactionSphere();
        double distFromCameraToFocus =
            glm::distance(prevCameraPosition, anchorPos) - nodeRadius;

        // Make the approximation delta size depending on the flight distance
        double arrivalThreshold =
            _linearFlight.destinationDistance * _linearFlight.destinationFactor;

        const double distToDestination =
            std::fabs(distFromCameraToFocus - _linearFlight.destinationDistance);

        // Fly towards the flight destination distance. When getting closer than
        // arrivalThreshold terminate the flight
        if (distToDestination > arrivalThreshold) {
            pose.position = moveCameraAlongVector(
                pose.position,
                distFromCameraToFocus,
                camPosToAnchorPosDiff,
                _linearFlight.destinationDistance,
                deltaTime
            );
        }
        else {
            _linearFlight.apply = false;
        }
    }

    const bool hasPreviousPositions =
        _previousAnchorNodePosition.has_value() &&
        _previousAimNodePosition.has_value();

    if (_aimNode && _aimNode != _anchorNode && hasPreviousPositions) {
        const glm::dvec3 aimPos = _aimNode->worldPosition();
        const glm::dvec3 cameraToAnchor =
            *_previousAnchorNodePosition - prevCameraPosition;

        Displacement anchorToAim = {
            *_previousAimNodePosition - *_previousAnchorNodePosition,
            aimPos - anchorPos
        };

        anchorToAim = interpolateRetargetAim(
            deltaTime,
            pose,
            cameraToAnchor,
            anchorToAim
        );

        pose = followAim(pose, cameraToAnchor, anchorToAim);

        _previousAimNodePosition = _aimNode->worldPosition();
        _previousAnchorNodeRotation = _anchorNode->worldRotationMatrix();
    }

    _previousAnchorNodePosition = _anchorNode->worldPosition();

    // Calculate a position handle based on the camera position in world space
    SurfacePositionHandle posHandle =
        calculateSurfacePositionHandle(*_anchorNode, pose.position);

    // Decompose camera rotation so that we can handle global and local rotation
    // individually. Then we combine them again when finished.
    // Compensate for relative movement of aim node,
    // in order to maintain the old global/local rotation
    CameraRotationDecomposition camRot =
        decomposeCameraRotationSurface(pose, *_anchorNode);

    // Rotate with the object by finding a differential rotation from the previous
    // to the current rotation
    glm::dquat anchorRotation =
        glm::quat_cast(_anchorNode->worldRotationMatrix());

    glm::dquat anchorNodeRotationDiff = _previousAnchorNodeRotation.has_value() ?
        *_previousAnchorNodeRotation * glm::inverse(anchorRotation) :
        glm::dquat(1.0, 0.0, 0.0, 0.0);

    _previousAnchorNodeRotation = anchorRotation;

    // Interpolate rotation differential so that the camera rotates with the object
    // only if close enough
    anchorNodeRotationDiff = interpolateRotationDifferential(
        deltaTime,
        pose.position,
        anchorNodeRotationDiff
    );

    // Update local rotation based on user input
    camRot.localRotation = roll(deltaTime, camRot.localRotation);
    camRot.localRotation = interpolateLocalRotation(deltaTime, camRot.localRotation);
    camRot.localRotation = rotateLocally(deltaTime, camRot.localRotation);

    // Horizontal translation based on user input
    pose.position = translateHorizontally(
        deltaTime,
        pose.position,
        anchorPos,
        camRot.globalRotation,
        posHandle
    );

    // Apply any automatic idle behavior. Note that the idle behavior is aborted if there
    // is no input from interaction. So, it assumes that all the other effects from
    // user input results in no change
    applyIdleBehavior(
        deltaTime,
        pose.position,
        camRot.localRotation,
        camRot.globalRotation
    );

    // Horizontal translation by focus node rotation
    pose.position = followAnchorNodeRotation(
        pose.position,
        anchorPos,
        anchorNodeRotationDiff
    );

    // Recalculate posHandle since horizontal position changed
    posHandle = calculateSurfacePositionHandle(*_anchorNode, pose.position);

    // Rotate globally to keep camera rotation fixed
    // in the rotating reference frame of the anchor object
    camRot.globalRotation = rotateGlobally(
        camRot.globalRotation,
        anchorNodeRotationDiff,
        posHandle
    );

    // Rotate around the surface out direction based on user input
    camRot.globalRotation = rotateHorizontally(
        deltaTime,
        camRot.globalRotation,
        posHandle
    );

    // Perform the vertical movements based on user input
    pose.position = translateVertically(deltaTime, pose.position, anchorPos, posHandle);
    pose.position = pushToSurface(
        _minimumAllowedDistance,
        pose.position,
        anchorPos,
        posHandle
    );

    // Update the camera state
    _camera->setPositionVec3(pose.position);
    _camera->setRotation(composeCameraRotation(camRot));
}

void OrbitalNavigator::updateCameraScalingFromAnchor(double deltaTime) {
    if (_useAdaptiveStereoscopicDepth) {
        const glm::dvec3 anchorPos = _anchorNode->worldPosition();
        const glm::dvec3 cameraPos = _camera->positionVec3();

        SurfacePositionHandle posHandle =
            calculateSurfacePositionHandle(*_anchorNode, cameraPos);

        double targetCameraToSurfaceDistance = glm::length(
            cameraToSurfaceVector(cameraPos, anchorPos, posHandle)
        );

        if (_aimNode) {
            targetCameraToSurfaceDistance = std::min(
                targetCameraToSurfaceDistance,
                glm::distance(cameraPos, _aimNode->worldPosition())
            );
        }

        if (_directlySetStereoDistance) {
            _currentCameraToSurfaceDistance = targetCameraToSurfaceDistance;
            _directlySetStereoDistance = false;
        }
        else {
            _currentCameraToSurfaceDistance = interpolateCameraToSurfaceDistance(
                deltaTime,
                _currentCameraToSurfaceDistance,
                targetCameraToSurfaceDistance);
        }

        _camera->setScaling(
            _stereoscopicDepthOfFocusSurface /
            static_cast<float>(_currentCameraToSurfaceDistance)
        );
    }
    else {
        _camera->setScaling(glm::pow(10.f, _staticViewScaleExponent));
    }
}

void OrbitalNavigator::updateOnCameraInteraction() {
    // Disable idle behavior if camera interaction happened
    if (_idleBehavior.apply && _idleBehavior.abortOnCameraInteraction) {
        _idleBehavior.apply = false;
        // Prevent interpolating stop, to avoid weirdness when changing anchor, etc
        _idleBehaviorDampenInterpolator.setInterpolationTime(0.f);
    }
}

glm::dquat OrbitalNavigator::composeCameraRotation(
                                        const CameraRotationDecomposition& decomposition)
{
    return decomposition.globalRotation * decomposition.localRotation;
}

Camera* OrbitalNavigator::camera() const {
    return _camera;
}

void OrbitalNavigator::setCamera(Camera* camera) {
    _camera = camera;
}

glm::dvec3 OrbitalNavigator::cameraToSurfaceVector(const glm::dvec3& cameraPos,
                                                   const glm::dvec3& centerPos,
                                                   const SurfacePositionHandle& posHandle)
{
    glm::dmat4 modelTransform = _anchorNode->modelTransform();
    glm::dvec3 posDiff = cameraPos - centerPos;
    glm::dvec3 centerToActualSurfaceModelSpace =
        posHandle.centerToReferenceSurface +
        posHandle.referenceSurfaceOutDirection * posHandle.heightToSurface;

    glm::dvec3 centerToActualSurface =
        glm::dmat3(modelTransform) * centerToActualSurfaceModelSpace;

    return centerToActualSurface - posDiff;
}

void OrbitalNavigator::setFocusNode(const SceneGraphNode* focusNode,
                                    bool resetVelocitiesOnChange)
{
    const SceneGraphNode* previousAnchor = _anchorNode;
    setAnchorNode(focusNode, resetVelocitiesOnChange);
    setAimNode(nullptr);

    global::eventEngine->publishEvent<events::EventFocusNodeChanged>(
        previousAnchor,
        focusNode
    );
}

void OrbitalNavigator::setFocusNode(const std::string& focusNode, bool) {
    _anchor.set(focusNode);
    _aim.set(std::string(""));
}

void OrbitalNavigator::setAnchorNode(const SceneGraphNode* anchorNode,
                                     bool resetVelocitiesOnChange)
{
    if (!_anchorNode) {
        _directlySetStereoDistance = true;
    }

    const bool changedAnchor = _anchorNode != anchorNode;
    _anchorNode = anchorNode;

    // Need to reset velocities after the actual switch in anchor node,
    // since the reset behavior depends on the anchor node.
    if (changedAnchor && resetVelocitiesOnChange) {
        resetVelocities();
    }

    // Mark a changed anchor node as a camera interaction
    if (changedAnchor) {
        updateOnCameraInteraction();
    }

    if (_anchorNode) {
        _previousAnchorNodePosition = _anchorNode->worldPosition();
        _previousAnchorNodeRotation = glm::quat_cast(_anchorNode->worldRotationMatrix());
    }
    else {
        _previousAnchorNodePosition.reset();
        _previousAnchorNodeRotation.reset();
    }
}

void OrbitalNavigator::clearPreviousState() {
    _previousAnchorNodePosition.reset();
    _previousAnchorNodeRotation.reset();
    _previousAimNodePosition.reset();
}

void OrbitalNavigator::setAimNode(const SceneGraphNode* aimNode) {
    _retargetAimInterpolator.end();
    _aimNode = aimNode;

    if (_aimNode) {
        _previousAimNodePosition = _aimNode->worldPosition();
    }
}

void OrbitalNavigator::setAnchorNode(const std::string& anchorNode) {
    _anchor.set(anchorNode);
}

void OrbitalNavigator::setAimNode(const std::string& aimNode) {
    _aim.set(aimNode);
}

void OrbitalNavigator::resetNodeMovements() {
    if (_anchorNode) {
        _previousAnchorNodePosition = _anchorNode->worldPosition();
        _previousAnchorNodeRotation = glm::quat_cast(_anchorNode->worldRotationMatrix());
    }
    else {
        _previousAnchorNodePosition = glm::dvec3(0.0);
        _previousAnchorNodeRotation = glm::dquat();
    }

    _previousAimNodePosition = _aimNode ? _aimNode->worldPosition() : glm::dvec3(0.0);
}

void OrbitalNavigator::startRetargetAnchor() {
    if (!_anchorNode) {
        return;
    }
    const glm::dvec3 camPos = _camera->positionVec3();
    const glm::dvec3 camDir = _camera->viewDirectionWorldSpace();

    const glm::dvec3 centerPos = _anchorNode->worldPosition();
    const glm::dvec3 directionToCenter = glm::normalize(centerPos - camPos);

    const double angle = glm::angle(camDir, directionToCenter);

    // Minimum is _rotateInterpolationTime seconds. Otherwise proportional to angle.
    _retargetAnchorInterpolator.setInterpolationTime(static_cast<float>(
        glm::max(angle, 1.0) * _retargetInterpolationTime
    ));
    _retargetAnchorInterpolator.start();

    _cameraToSurfaceDistanceInterpolator.setInterpolationTime(_stereoInterpolationTime);
    _cameraToSurfaceDistanceInterpolator.start();
}

void OrbitalNavigator::startRetargetAim() {
    if (!_aimNode) {
        return;
    }

    const glm::dvec3 camPos = _camera->positionVec3();
    const glm::dvec3 camDir = _camera->viewDirectionWorldSpace();
    const glm::dvec3 centerPos = _aimNode->worldPosition();
    const glm::dvec3 directionToCenter = glm::normalize(centerPos - camPos);

    const double angle = glm::angle(camDir, directionToCenter);

    // Minimum is _rotateInterpolationTime seconds. Otherwise proportional to angle.
    _retargetAimInterpolator.setInterpolationTime(static_cast<float>(
        glm::max(angle, 1.0) * _retargetInterpolationTime
    ));
    _retargetAimInterpolator.start();

    _cameraToSurfaceDistanceInterpolator.setInterpolationTime(_stereoInterpolationTime);
    _cameraToSurfaceDistanceInterpolator.start();
}


float OrbitalNavigator::retargetInterpolationTime() const {
    return _retargetInterpolationTime;
}

void OrbitalNavigator::setRetargetInterpolationTime(float durationInSeconds) {
    _retargetInterpolationTime = durationInSeconds;
}

bool OrbitalNavigator::shouldFollowAnchorRotation(const glm::dvec3& cameraPosition) const
{
    if (!_anchorNode || !_followAnchorNodeRotation) {
        return false;
    }

    const glm::dmat4 modelTransform = _anchorNode->modelTransform();
    const glm::dmat4 inverseModelTransform = glm::inverse(modelTransform);
    const glm::dvec3 cameraPositionModelSpace = glm::dvec3(inverseModelTransform *
        glm::dvec4(cameraPosition, 1.0));

    const SurfacePositionHandle positionHandle =
        _anchorNode->calculateSurfacePositionHandle(cameraPositionModelSpace);

    const double maximumDistanceForRotation = glm::length(
        glm::dmat3(modelTransform) * positionHandle.centerToReferenceSurface
    ) * _followAnchorNodeRotationDistance;

    const double distanceToCamera =
        glm::distance(cameraPosition, _anchorNode->worldPosition());
    bool shouldFollow = distanceToCamera < maximumDistanceForRotation;
    return shouldFollow;
}

bool OrbitalNavigator::followingAnchorRotation() const {
    if (_aimNode != nullptr && _aimNode != _anchorNode) {
        return false;
    }
    return _followRotationInterpolator.value() >= 1.0;
}

const SceneGraphNode* OrbitalNavigator::anchorNode() const {
    return _anchorNode;
}

const SceneGraphNode* OrbitalNavigator::aimNode() const {
    return _aimNode;
}

bool OrbitalNavigator::hasRotationalFriction() const {
    return _friction.rotational;
}

bool OrbitalNavigator::hasZoomFriction() const {
    return _friction.zoom;
}

bool OrbitalNavigator::hasRollFriction() const {
    return _friction.roll;
}

OrbitalNavigator::CameraRotationDecomposition
    OrbitalNavigator::decomposeCameraRotationSurface(CameraPose cameraPose,
                                                     const SceneGraphNode& reference)
{
    const glm::dvec3 cameraUp = cameraPose.rotation * Camera::UpDirectionCameraSpace;
    const glm::dvec3 cameraViewDirection = ghoul::viewDirection(cameraPose.rotation);

    const glm::dmat4 modelTransform = reference.modelTransform();
    const glm::dmat4 inverseModelTransform = glm::inverse(modelTransform);
    const glm::dvec3 cameraPositionModelSpace = glm::dvec3(inverseModelTransform *
                                                glm::dvec4(cameraPose.position, 1));

    const SurfacePositionHandle posHandle =
        reference.calculateSurfacePositionHandle(cameraPositionModelSpace);

    const glm::dvec3 directionFromSurfaceToCameraModelSpace =
        posHandle.referenceSurfaceOutDirection;
    const glm::dvec3 directionFromSurfaceToCamera = glm::normalize(
        glm::dmat3(modelTransform) * directionFromSurfaceToCameraModelSpace
    );

    // To avoid problem with lookup in up direction we adjust is with the view direction
    const glm::dquat globalCameraRotation = ghoul::lookAtQuaternion(
        glm::dvec3(0.0),
        -directionFromSurfaceToCamera,
        normalize(cameraViewDirection + cameraUp)
    );

    const glm::dquat localCameraRotation = glm::inverse(globalCameraRotation) *
        cameraPose.rotation;

    return { localCameraRotation, globalCameraRotation };
}

OrbitalNavigator::CameraRotationDecomposition
    OrbitalNavigator::decomposeCameraRotation(CameraPose cameraPose,
                                              glm::dvec3 reference)
{
    const glm::dvec3 cameraUp = cameraPose.rotation * glm::dvec3(0.0, 1.0, 0.0);
    const glm::dvec3 cameraViewDirection = ghoul::viewDirection(cameraPose.rotation);

    // To avoid problem with lookup in up direction we adjust is with the view direction
    const glm::dquat globalCameraRotation = ghoul::lookAtQuaternion(
        glm::dvec3(0.0),
        reference - cameraPose.position,
        normalize(cameraViewDirection + cameraUp)
    );

    const glm::dquat localCameraRotation = glm::inverse(globalCameraRotation) *
        cameraPose.rotation;

    return { localCameraRotation, globalCameraRotation };
}

CameraPose OrbitalNavigator::followAim(CameraPose pose, glm::dvec3 cameraToAnchor,
                                       Displacement anchorToAim)
{
    CameraRotationDecomposition anchorDecomp =
        decomposeCameraRotation(pose, pose.position + cameraToAnchor);

    const glm::dvec3 prevCameraToAim = cameraToAnchor + anchorToAim.first;
    const double distanceRatio =
        glm::length(anchorToAim.second) / glm::length(prevCameraToAim);

    // Make sure that the anchor and aim nodes are numerically distinguishable,
    // otherwise, don't follow the aim.
    if (distanceRatio > DistanceRatioAimThreshold) {
        // Divide the action of following the aim into two actions:
        // 1. Rotating camera around anchor, based on the aim's projection onto a sphere
        //    around the anchor, with radius = distance(camera, anchor)
        // 2. Adjustment of the camera to account for radial displacement of the aim

        // Step 1 (Rotation around anchor based on aim's projection)
        glm::dvec3 newAnchorToProjectedAim =
            glm::length(anchorToAim.first) * glm::normalize(anchorToAim.second);
        const double spinRotationAngle = glm::angle(
            glm::normalize(anchorToAim.first), glm::normalize(newAnchorToProjectedAim)
        );

        if (spinRotationAngle > AngleEpsilon) {
            const glm::dvec3 spinRotationAxis =
                glm::cross(anchorToAim.first, newAnchorToProjectedAim);

            const glm::dquat spinRotation =
                glm::angleAxis(spinRotationAngle, glm::normalize(spinRotationAxis));

            pose.position =
                _anchorNode->worldPosition() - spinRotation * cameraToAnchor;

            anchorDecomp.globalRotation = spinRotation * anchorDecomp.globalRotation;
        }

        // Step 2 (Adjustment for radial displacement)
        const glm::dvec3 projectedAim =
            _anchorNode->worldPosition() + newAnchorToProjectedAim;

        const glm::dvec3 intermediateCameraToAnchor =
            _anchorNode->worldPosition() - pose.position;

        const glm::dvec3 intermediateCameraToProjectedAim =
            projectedAim - pose.position;

        const double anchorAimAngle = glm::angle(
            glm::normalize(intermediateCameraToAnchor),
            glm::normalize(intermediateCameraToProjectedAim)
        );
        double ratio =
            glm::sin(anchorAimAngle) * glm::length(intermediateCameraToAnchor) /
            glm::length(anchorToAim.second);

        // Equation has no solution if ratio > 1.
        // To avoid a discontinuity in the camera behavior,
        // fade out the distance correction influence when ratio approaches 1.
        // CorrectionFactorExponent = 50.0 is picked arbitrarily,
        // and gives a smooth result.
        ratio = glm::clamp(ratio, -1.0, 1.0);
        const double CorrectionFactorExponent = 50.0;
        const double correctionFactor =
            glm::clamp(1.0 - glm::pow(ratio, CorrectionFactorExponent), 0.0, 1.0);

        // newCameraAnchorAngle has two solutions, depending on whether the camera is
        // in the half-space closest to the anchor or aim.
        double newCameraAnchorAngle = glm::asin(ratio);
        if (glm::dot(intermediateCameraToAnchor, anchorToAim.second) <= 0 &&
            glm::dot(intermediateCameraToProjectedAim, anchorToAim.second) <= 0)
        {
            newCameraAnchorAngle = -glm::asin(ratio) + glm::pi<double>();
        }

        const double prevCameraAimAngle = glm::angle(
            glm::normalize(-intermediateCameraToAnchor),
            glm::normalize(newAnchorToProjectedAim)
        );

        const double newCameraAimAngle =
            glm::pi<double>() - anchorAimAngle - newCameraAnchorAngle;

        double distanceRotationAngle = correctionFactor *
                                       (newCameraAimAngle - prevCameraAimAngle);

        if (glm::abs(distanceRotationAngle) > AngleEpsilon) {
            glm::dvec3 distanceRotationAxis = glm::normalize(
                glm::cross(intermediateCameraToAnchor, newAnchorToProjectedAim)
            );
            const glm::dquat orbitRotation =
                glm::angleAxis(distanceRotationAngle, distanceRotationAxis);

            pose.position =
                _anchorNode->worldPosition() -
                orbitRotation * intermediateCameraToAnchor;

            const glm::dquat aimAdjustRotation =
                glm::angleAxis(distanceRotationAngle, distanceRotationAxis);

            anchorDecomp.globalRotation = aimAdjustRotation * anchorDecomp.globalRotation;
        }
        // End of step 2.

        pose.rotation = composeCameraRotation(anchorDecomp);
    }

    return pose;
}

glm::dquat OrbitalNavigator::roll(double deltaTime,
                                  const glm::dquat& localCameraRotation) const
{
    const glm::dquat mouseRollQuat = glm::angleAxis(
        _mouseStates.localRollVelocity().x * deltaTime +
        _joystickStates.localRollVelocity().x * deltaTime +
        _websocketStates.localRollVelocity().x * deltaTime +
        _scriptStates.localRollVelocity().x * deltaTime,
        glm::dvec3(0.0, 0.0, 1.0)
    );
    return localCameraRotation * mouseRollQuat;
}

glm::dquat OrbitalNavigator::rotateLocally(double deltaTime,
                                           const glm::dquat& localCameraRotation) const
{
    const glm::dquat mouseRotationDiff = glm::dquat(glm::dvec3(
        _mouseStates.localRotationVelocity().y,
        _mouseStates.localRotationVelocity().x,
        0.0
    ) * deltaTime);

    const glm::dquat joystickRotationDiff = glm::dquat(glm::dvec3(
        _joystickStates.localRotationVelocity().y,
        _joystickStates.localRotationVelocity().x,
        0.0
    ) * deltaTime);

    const glm::dquat websocketRotationDiff = glm::dquat(glm::dvec3(
        _websocketStates.localRotationVelocity().y,
        _websocketStates.localRotationVelocity().x,
        0.0
    ) * deltaTime);

    const glm::dquat scriptRotationDiff = glm::dquat(glm::dvec3(
        _scriptStates.localRotationVelocity().y,
        _scriptStates.localRotationVelocity().x,
        0.0
    ) * deltaTime);

    return localCameraRotation * joystickRotationDiff * mouseRotationDiff *
           websocketRotationDiff * scriptRotationDiff;
}

glm::dquat OrbitalNavigator::interpolateLocalRotation(double deltaTime,
                                                   const glm::dquat& localCameraRotation)
{
    if (_retargetAnchorInterpolator.isInterpolating()) {
        const double t = _retargetAnchorInterpolator.value();
        _retargetAnchorInterpolator.setDeltaTime(static_cast<float>(deltaTime));
        _retargetAnchorInterpolator.step();

        const glm::dvec3 localUp =
            localCameraRotation * Camera::UpDirectionCameraSpace;

        const glm::dquat targetRotation = ghoul::lookAtQuaternion(
            glm::dvec3(0.0),
            Camera::ViewDirectionCameraSpace,
            normalize(localUp)
        );

        const glm::dquat result = glm::slerp(
            localCameraRotation,
            targetRotation,
            glm::min(t * _retargetAnchorInterpolator.deltaTimeScaled(), 1.0));

        // Retrieving the angle of a quaternion uses acos on the w component, which can
        // have numerical instability for values close to 1.0
        constexpr double Epsilon = 1.0e-13;
        if (std::fabs((std::fabs(result.w) - 1.0)) < Epsilon || angle(result) < 0.01) {
            _retargetAnchorInterpolator.end();
        }
        return result;
    }
    else {
        return localCameraRotation;
    }
}

OrbitalNavigator::Displacement
OrbitalNavigator::interpolateRetargetAim(double deltaTime, CameraPose pose,
                                         glm::dvec3 prevCameraToAnchor,
                                         Displacement anchorToAim)
{
    if (_retargetAimInterpolator.isInterpolating()) {
        double t = _retargetAimInterpolator.value();
        _retargetAimInterpolator.setDeltaTime(static_cast<float>(deltaTime));
        _retargetAimInterpolator.step();

        const glm::dvec3 prevCameraToAim = prevCameraToAnchor + anchorToAim.first;
        const double aimDistance = glm::length(prevCameraToAim);
        const glm::dquat prevRotation = pose.rotation;

        // Introduce a virtual aim - a position straight ahead of the camera,
        // that should be rotated around the camera, until it reaches the aim node.

        const glm::dvec3 prevCameraToVirtualAim =
            aimDistance * (prevRotation * Camera::ViewDirectionCameraSpace);

        // Max angle: the maximum possible angle between anchor and aim, given that
        // the camera orbits the anchor on a fixed distance.
        const double maxAngle =
            glm::atan(glm::length(anchorToAim.first), glm::length(prevCameraToAnchor));

        // Requested angle: The angle between the vector straight ahead from the
        // camera and the vector from camera to anchor should remain constant, in
        // order for the anchor not to move in screen space.
        const double requestedAngle = glm::angle(
            glm::normalize(prevCameraToVirtualAim),
            glm::normalize(prevCameraToAnchor)
        );

        if (requestedAngle <= maxAngle) {
            glm::dvec3 aimPos = pose.position + prevCameraToAnchor + anchorToAim.second;
            CameraRotationDecomposition aimDecomp = decomposeCameraRotation(pose, aimPos);

            const glm::dquat interpolatedRotation = glm::slerp(
                prevRotation,
                aimDecomp.globalRotation,
                glm::min(t * _retargetAimInterpolator.deltaTimeScaled(), 1.0)
            );

            const glm::dvec3 recomputedCameraToVirtualAim =
                aimDistance * (interpolatedRotation * Camera::ViewDirectionCameraSpace);

            return {
                prevCameraToVirtualAim - prevCameraToAnchor,
                recomputedCameraToVirtualAim - prevCameraToAnchor
            };
        }
        else {
            // Bail out.
            // Cannot put aim node in center without moving anchor in screen space.
            // Future work: Rotate as much as possible,
            // or possibly use some other DOF to find solution, like moving the camera.
            _retargetAimInterpolator.end();
        }
    }
    return anchorToAim;
}


double OrbitalNavigator::interpolateCameraToSurfaceDistance(double deltaTime,
                                                            double currentDistance,
                                                            double targetDistance
) {
    if (!_cameraToSurfaceDistanceInterpolator.isInterpolating()) {
        return targetDistance;
    }

    double t = _cameraToSurfaceDistanceInterpolator.value();
    _cameraToSurfaceDistanceInterpolator.setDeltaTime(static_cast<float>(deltaTime));
    _cameraToSurfaceDistanceInterpolator.step();

    // Interpolate distance logarithmically.
    double result = glm::exp(glm::mix(
        glm::log(currentDistance),
        glm::log(targetDistance),
        glm::min(t * _cameraToSurfaceDistanceInterpolator.deltaTimeScaled(), 1.0))
    );

    double ratio = currentDistance / targetDistance;
    if (glm::abs(ratio - 1.0) < 0.000001) {
        _cameraToSurfaceDistanceInterpolator.end();
    }

    return result;
}

glm::dvec3 OrbitalNavigator::translateHorizontally(double deltaTime,
                                                   const glm::dvec3& cameraPosition,
                                                   const glm::dvec3& objectPosition,
                                                   const glm::dquat& globalCameraRotation,
                                        const SurfacePositionHandle& positionHandle) const
{
    const glm::dmat4 modelTransform = _anchorNode->modelTransform();

    const glm::dvec3 outDirection = glm::normalize(glm::dmat3(modelTransform) *
                                    positionHandle.referenceSurfaceOutDirection);

    // Vector logic
    const glm::dvec3 posDiff = cameraPosition - objectPosition;
    const glm::dvec3 centerToActualSurfaceModelSpace =
        positionHandle.centerToReferenceSurface +
        positionHandle.referenceSurfaceOutDirection * positionHandle.heightToSurface;

    const glm::dvec3 centerToActualSurface = glm::dmat3(modelTransform) *
                                             centerToActualSurfaceModelSpace;
    const glm::dvec3 actualSurfaceToCamera = posDiff - centerToActualSurface;
    const double distFromSurfaceToCamera = glm::length(actualSurfaceToCamera);

    // Final values to be used
    const double distFromCenterToSurface = glm::length(centerToActualSurface);
    const double distFromCenterToCamera = glm::length(posDiff);

    const double speedScale =
        distFromCenterToSurface > 0.0 ?
        glm::clamp(distFromSurfaceToCamera / distFromCenterToSurface, 0.0, 1.0) :
        1.0;

    // Get rotation in camera space
    const glm::dquat mouseRotationDiffCamSpace = glm::dquat(glm::dvec3(
        -_mouseStates.globalRotationVelocity().y * deltaTime,
        -_mouseStates.globalRotationVelocity().x * deltaTime,
        0.0) * speedScale);

    const glm::dquat joystickRotationDiffCamSpace = glm::dquat(glm::dvec3(
        -_joystickStates.globalRotationVelocity().y * deltaTime,
        -_joystickStates.globalRotationVelocity().x * deltaTime,
        0.0) * speedScale
    );

    const glm::dquat scriptRotationDiffCamSpace = glm::dquat(glm::dvec3(
        -_scriptStates.globalRotationVelocity().y * deltaTime,
        -_scriptStates.globalRotationVelocity().x * deltaTime,
        0.0) * speedScale
    );

    const glm::dquat websocketRotationDiffCamSpace = glm::dquat(glm::dvec3(
        -_websocketStates.globalRotationVelocity().y * deltaTime,
        -_websocketStates.globalRotationVelocity().x * deltaTime,
        0.0) * speedScale
    );

    // Transform to world space
    const glm::dquat rotationDiffWorldSpace = globalCameraRotation *
        joystickRotationDiffCamSpace * mouseRotationDiffCamSpace *
        websocketRotationDiffCamSpace * scriptRotationDiffCamSpace *
        glm::inverse(globalCameraRotation);

    // Rotate and find the difference vector
    const glm::dvec3 rotationDiffVec3 =
        (distFromCenterToCamera * outDirection) * rotationDiffWorldSpace -
        (distFromCenterToCamera * outDirection);

    // Add difference to position
    return cameraPosition + rotationDiffVec3;
}

glm::dvec3 OrbitalNavigator::moveCameraAlongVector(const glm::dvec3& camPos,
                                                  double distFromCameraToFocus,
                                                  const glm::dvec3& camPosToAnchorPosDiff,
                                                  double destination,
                                                  double deltaTime) const
{
    // This factor adapts the velocity so it slows down when getting closer
    // to our final destination
    double velocity = 0.0;

    if (destination < distFromCameraToFocus) { // When flying towards anchor
        velocity = 1.0 - destination / distFromCameraToFocus;
    }
    else { // When flying away from anchor
        velocity = distFromCameraToFocus / destination - 1.0;
    }
    velocity *= _linearFlight.velocitySensitivity * deltaTime;

    // Return the updated camera position
    return camPos - velocity * camPosToAnchorPosDiff;
}

glm::dvec3 OrbitalNavigator::followAnchorNodeRotation(const glm::dvec3& cameraPosition,
                                                     const glm::dvec3& objectPosition,
                                            const glm::dquat& focusNodeRotationDiff) const
{
    const glm::dvec3 posDiff = cameraPosition - objectPosition;

    const glm::dvec3 rotationDiffVec3AroundCenter =
        posDiff * focusNodeRotationDiff - posDiff;

    return cameraPosition + rotationDiffVec3AroundCenter;
}

glm::dquat OrbitalNavigator::rotateGlobally(const glm::dquat& globalCameraRotation,
                                            const glm::dquat& focusNodeRotationDiff,
                                        const SurfacePositionHandle& positionHandle) const
{
    const glm::dmat4 modelTransform = _anchorNode->modelTransform();

    const glm::dvec3 directionFromSurfaceToCamera =
        glm::dmat3(modelTransform) * positionHandle.referenceSurfaceOutDirection;

    const glm::dvec3 cameraUpWhenFacingSurface = glm::inverse(focusNodeRotationDiff) *
        globalCameraRotation * Camera::UpDirectionCameraSpace;

    return ghoul::lookAtQuaternion(
        glm::dvec3(0.0),
        -directionFromSurfaceToCamera,
        cameraUpWhenFacingSurface
    );
}

glm::dvec3 OrbitalNavigator::translateVertically(double deltaTime,
                                                 const glm::dvec3& cameraPosition,
                                                 const glm::dvec3& objectPosition,
                                        const SurfacePositionHandle& positionHandle) const
{
    const glm::dmat4 modelTransform = _anchorNode->modelTransform();

    const glm::dvec3 posDiff = cameraPosition - objectPosition;

    const glm::dvec3 centerToActualSurfaceModelSpace =
        positionHandle.centerToReferenceSurface +
        positionHandle.referenceSurfaceOutDirection * positionHandle.heightToSurface;

    const glm::dvec3 centerToActualSurface = glm::dmat3(modelTransform) *
                                             centerToActualSurfaceModelSpace;
    const glm::dvec3 actualSurfaceToCamera = posDiff - centerToActualSurface;

    const double totalVelocity = _joystickStates.truckMovementVelocity().y +
                                 _mouseStates.truckMovementVelocity().y +
                                 _websocketStates.truckMovementVelocity().y +
                                 _scriptStates.truckMovementVelocity().y;

    return cameraPosition - actualSurfaceToCamera * totalVelocity * deltaTime;
}

glm::dquat OrbitalNavigator::rotateHorizontally(double deltaTime,
                                                const glm::dquat& globalCameraRotation,
                                        const SurfacePositionHandle& positionHandle) const
{
    const glm::dmat4 modelTransform = _anchorNode->modelTransform();

    const glm::dvec3 directionFromSurfaceToCameraModelSpace =
        positionHandle.referenceSurfaceOutDirection;
    const glm::dvec3 directionFromSurfaceToCamera = glm::normalize(
        glm::dmat3(modelTransform) * directionFromSurfaceToCameraModelSpace
    );

    const glm::dquat mouseCameraRollRotation = glm::angleAxis(
        _mouseStates.globalRollVelocity().x * deltaTime +
        _joystickStates.globalRollVelocity().x * deltaTime +
        _websocketStates.globalRollVelocity().x * deltaTime +
        _scriptStates.globalRollVelocity().x * deltaTime,
        directionFromSurfaceToCamera
    );
    return mouseCameraRollRotation * globalCameraRotation;
}

glm::dvec3 OrbitalNavigator::pushToSurface(double minHeightAboveGround,
                                           const glm::dvec3& cameraPosition,
                                           const glm::dvec3& objectPosition,
                                        const SurfacePositionHandle& positionHandle) const
{
    const glm::dmat4 modelTransform = _anchorNode->modelTransform();

    const glm::dvec3 posDiff = cameraPosition - objectPosition;
    const glm::dvec3 referenceSurfaceOutDirection = glm::dmat3(modelTransform) *
                                              positionHandle.referenceSurfaceOutDirection;

    const glm::dvec3 centerToActualSurfaceModelSpace =
        positionHandle.centerToReferenceSurface +
        positionHandle.referenceSurfaceOutDirection * positionHandle.heightToSurface;

    const glm::dvec3 centerToActualSurface = glm::dmat3(modelTransform) *
                                             centerToActualSurfaceModelSpace;
    const glm::dvec3 actualSurfaceToCamera = posDiff - centerToActualSurface;
    const double surfaceToCameraSigned = glm::length(actualSurfaceToCamera) *
        glm::sign(dot(actualSurfaceToCamera, referenceSurfaceOutDirection));

    return cameraPosition + referenceSurfaceOutDirection *
        glm::max(minHeightAboveGround - surfaceToCameraSigned, 0.0);
}

glm::dquat OrbitalNavigator::interpolateRotationDifferential(double deltaTime,
                                                          const glm::dvec3 cameraPosition,
                                                           const glm::dquat& rotationDiff)
{
    // Interpolate with a negative delta time if distance is too large to follow
    const double interpolationSign =
        shouldFollowAnchorRotation(cameraPosition) ? 1.0 : -1.0;

    _followRotationInterpolator.setDeltaTime(static_cast<float>(
        interpolationSign * deltaTime
    ));
    _followRotationInterpolator.step();

    return glm::slerp(
        glm::dquat(glm::dvec3(0.0)),
        rotationDiff,
        _followRotationInterpolator.value()
    );
}

SurfacePositionHandle OrbitalNavigator::calculateSurfacePositionHandle(
                                                const SceneGraphNode& node,
                                                const glm::dvec3 cameraPositionWorldSpace)
{
    const glm::dmat4 inverseModelTransform = glm::inverse(node.modelTransform());
    const glm::dvec3 cameraPositionModelSpace =
        glm::dvec3(inverseModelTransform * glm::dvec4(cameraPositionWorldSpace, 1.0));
    const SurfacePositionHandle posHandle =
        node.calculateSurfacePositionHandle(cameraPositionModelSpace);

    return posHandle;
}

JoystickCameraStates& OrbitalNavigator::joystickStates() {
    return _joystickStates;
}

const JoystickCameraStates& OrbitalNavigator::joystickStates() const {
    return _joystickStates;
}

WebsocketCameraStates& OrbitalNavigator::websocketStates() {
    return _websocketStates;
}

const WebsocketCameraStates& OrbitalNavigator::websocketStates() const {
    return _websocketStates;
}

ScriptCameraStates& OrbitalNavigator::scriptStates() {
    return _scriptStates;
}

const ScriptCameraStates& OrbitalNavigator::scriptStates() const {
    return _scriptStates;
}

void OrbitalNavigator::applyIdleBehavior(double deltaTime, glm::dvec3& position,
                                         glm::dquat& localRotation,
                                         glm::dquat& globalRotation)
{
    _idleBehaviorDampenInterpolator.setDeltaTime(static_cast<float>(deltaTime));
    _idleBehaviorDampenInterpolator.step();

    if (!(_idleBehavior.apply || _idleBehaviorDampenInterpolator.isInterpolating())) {
        return;
    }

    SurfacePositionHandle posHandle =
        calculateSurfacePositionHandle(*_anchorNode, position);

    const glm::dvec3 centerToActualSurfaceModelSpace =
        posHandle.centerToReferenceSurface +
        posHandle.referenceSurfaceOutDirection * posHandle.heightToSurface;

    const glm::dvec3 centerToActualSurface = glm::dmat3(_anchorNode->modelTransform()) *
        centerToActualSurfaceModelSpace;
    const glm::dvec3 centerToCamera = position - _anchorNode->worldPosition();
    const glm::dvec3 actualSurfaceToCamera = centerToCamera - centerToActualSurface;

    const double distFromSurfaceToCamera = glm::length(actualSurfaceToCamera);
    const double distFromCenterToSurface = glm::length(centerToActualSurface);

    double speedScale =
        distFromCenterToSurface > 0.0 ?
        glm::clamp(distFromSurfaceToCamera / distFromCenterToSurface, 0.0, 1.0) :
        1.0; // same as horizontal translation

    speedScale *= _idleBehavior.speedScale;
    speedScale *= 0.05; // without this scaling, the motion is way too fast

    // Interpolate so that the start and end are smooth
    double s = _idleBehaviorDampenInterpolator.value();
    speedScale *= _invertIdleBehaviorInterpolation ? (1.0 - s) : s;

    // Apply the chosen behavior
    const IdleBehavior::Behavior chosen =
        static_cast<IdleBehavior::Behavior>(_idleBehavior.chosenBehavior.value());

    switch (chosen) {
        case IdleBehavior::Behavior::Orbit:
            orbitAnchor(deltaTime, position, globalRotation, speedScale);
            break;
        case IdleBehavior::Behavior::OrbitAtConstantLat: {
            // Assume that "north" coincides with the local z-direction
            // @TODO (2021-07-09, emmbr) Make each scene graph node aware of its own
            // north/up, so that we can query this information rather than assuming it.
            // The we could also combine this idle behavior with the next
            const glm::dvec3 north = glm::dvec3(0.0, 0.0, 1.0);
            orbitAroundAxis(north, deltaTime, position, globalRotation, speedScale);
            break;
        }
        case IdleBehavior::Behavior::OrbitAroundUp: {
            // Assume that "up" coincides with the local y-direction
            const glm::dvec3 up = glm::dvec3(0.0, 1.0, 0.0);
            orbitAroundAxis(up, deltaTime, position, globalRotation, speedScale);
            break;
        }
        default:
            throw ghoul::MissingCaseException();
    }
}

void OrbitalNavigator::orbitAnchor(double deltaTime, glm::dvec3& position,
                                   glm::dquat& globalRotation, double speedScale)
{
    ghoul_assert(_anchorNode != nullptr, "Node to orbit must be set!");

    // Apply a rotation to the right, in camera space
    // (Maybe we should also let the user decide which direction to rotate?
    // Or provide a few different orbit options)
    const glm::dvec3 eulerAngles = glm::dvec3(0.0, -1.0, 0.0) * deltaTime * speedScale;
    const glm::dquat rotationDiffCameraSpace = glm::dquat(eulerAngles);

    const glm::dquat rotationDiffWorldSpace = globalRotation *
        rotationDiffCameraSpace *
        glm::inverse(globalRotation);

    // Rotate to find the difference in position
    const glm::dvec3 anchorCenterToCamera = position - _anchorNode->worldPosition();
    const glm::dvec3 rotationDiffVec3 =
        anchorCenterToCamera * rotationDiffWorldSpace - anchorCenterToCamera;

    position += rotationDiffVec3;
}

void OrbitalNavigator::orbitAroundAxis(const glm::dvec3 axis, double deltaTime,
                                       glm::dvec3& position, glm::dquat& globalRotation,
                                       double speedScale)
{
    ghoul_assert(_anchorNode != nullptr, "Node to orbit must be set!");

    const glm::dmat4 modelTransform = _anchorNode->modelTransform();
    const glm::dvec3 axisInWorldCoords =
        glm::dmat3(modelTransform) * glm::normalize(axis);

    // Compute rotation to be applied around the axis
    double angle = deltaTime * speedScale;
    const glm::dquat spinRotation = glm::angleAxis(angle, axisInWorldCoords);

    // Rotate the position vector from the center to camera and update position
    const glm::dvec3 anchorCenterToCamera = position - _anchorNode->worldPosition();
    const glm::dvec3 rotationDiffVec3 =
        spinRotation * anchorCenterToCamera - anchorCenterToCamera;

    position += rotationDiffVec3;

    // Also apply the rotation to the global rotation, so the camera up vector is
    // rotated around the axis as well
    globalRotation = spinRotation * globalRotation;
}

} // namespace openspace::interaction
