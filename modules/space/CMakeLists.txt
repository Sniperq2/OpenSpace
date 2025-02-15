##########################################################################################
#                                                                                        #
# OpenSpace                                                                              #
#                                                                                        #
# Copyright (c) 2014-2022                                                                #
#                                                                                        #
# Permission is hereby granted, free of charge, to any person obtaining a copy of this   #
# software and associated documentation files (the "Software"), to deal in the Software  #
# without restriction, including without limitation the rights to use, copy, modify,     #
# merge, publish, distribute, sublicense, and/or sell copies of the Software, and to     #
# permit persons to whom the Software is furnished to do so, subject to the following    #
# conditions:                                                                            #
#                                                                                        #
# The above copyright notice and this permission notice shall be included in all copies  #
# or substantial portions of the Software.                                               #
#                                                                                        #
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,    #
# INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A          #
# PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT     #
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF   #
# CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE   #
# OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                          #
##########################################################################################

include(${OPENSPACE_CMAKE_EXT_DIR}/module_definition.cmake)

set(HEADER_FILES
  speckloader.h
  rendering/planetgeometry.h
  rendering/renderableconstellationbounds.h
  rendering/renderablefluxnodes.h
  rendering/renderablehabitablezone.h
  rendering/renderablerings.h
  rendering/renderableorbitalkepler.h
  rendering/renderablesatellites.h
  rendering/renderablesmallbody.h
  rendering/renderablestars.h
  rendering/renderabletravelspeed.h
  rendering/simplespheregeometry.h
  translation/keplertranslation.h
  translation/spicetranslation.h
  translation/tletranslation.h
  translation/horizonstranslation.h
  rotation/spicerotation.h
)
source_group("Header Files" FILES ${HEADER_FILES})

set(SOURCE_FILES
  spacemodule_lua.inl
  speckloader.cpp
  rendering/planetgeometry.cpp
  rendering/renderableconstellationbounds.cpp
  rendering/renderablefluxnodes.cpp
  rendering/renderablehabitablezone.cpp
  rendering/renderablerings.cpp
  rendering/renderableorbitalkepler.cpp
  rendering/renderablesatellites.cpp
  rendering/renderablesmallbody.cpp
  rendering/renderablestars.cpp
  rendering/renderabletravelspeed.cpp
  rendering/simplespheregeometry.cpp
  translation/keplertranslation.cpp
  translation/spicetranslation.cpp
  translation/tletranslation.cpp
  translation/horizonstranslation.cpp
  rotation/spicerotation.cpp
)
source_group("Source Files" FILES ${SOURCE_FILES})

set(SHADER_FILES
  shaders/constellationbounds_fs.glsl
  shaders/constellationbounds_vs.glsl
  shaders/debrisViz_fs.glsl
  shaders/debrisViz_vs.glsl
  shaders/fluxnodes_fs.glsl
  shaders/fluxnodes_vs.glsl
  shaders/habitablezone_vs.glsl
  shaders/habitablezone_fs.glsl
  shaders/rings_vs.glsl
  shaders/rings_fs.glsl
  shaders/star_fs.glsl
  shaders/star_ge.glsl
  shaders/star_vs.glsl
  shaders/travelspeed_fs.glsl
  shaders/travelspeed_vs.glsl
)
source_group("Shader Files" FILES ${SHADER_FILES})

create_new_module(
  "Space"
  space_module
  STATIC
  ${HEADER_FILES} ${SOURCE_FILES} ${SHADER_FILES}
)
