# =============================================================================
#   Copyright 2023 Roland Metivier <metivier.roland@chlorophyt.us>
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
# =============================================================================
#   AxolotlSD for C++ CMakeFile, generates a static and a dynamic library
# Minimum version is CMake 3.26
cmake_minimum_required(VERSION 3.26)

# Export compile commands for the language server
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project instantiation
project(axolotlsd LANGUAGES CXX
	VERSION 0.4.0.6
	DESCRIPTION "A C++20 implementation of the AxolotlSD wavetable sound driver"
	HOMEPAGE_URL "https://github.com/Chlorophytus/AxolotlSD"
)

include(GNUInstallDirs)

# Configure the project header
configure_file(include/${PROJECT_NAME}_configuration.txt
		${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}_configuration.hpp)

# For good measure we could use PkgConfig...just in case
configure_file(share/pkgconfig/${PROJECT_NAME}.pc.in
	${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc @ONLY)

# Build our main library
add_library(${PROJECT_NAME}_s STATIC src/axolotlsd.cpp)
add_library(${PROJECT_NAME}	SHARED src/axolotlsd.cpp)
# Use C++20 on target too
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED TRUE)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
set_property(TARGET ${PROJECT_NAME}_s PROPERTY CXX_STANDARD_REQUIRED TRUE)
set_property(TARGET ${PROJECT_NAME}_s PROPERTY CXX_STANDARD 20)

# Set target library properties
set_target_properties(${PROJECT_NAME}
    PROPERTIES
    VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}
		SOVERSION 0
    PUBLIC_HEADER "include/${PROJECT_NAME}.hpp;include/${PROJECT_NAME}_configuration.hpp") 
set_target_properties(${PROJECT_NAME}_s
    PROPERTIES
    VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}
    PUBLIC_HEADER "include/${PROJECT_NAME}.hpp;include/${PROJECT_NAME}_configuration.hpp")

# Finally link
target_link_libraries(${PROJECT_NAME})

# Allow installation
install(
    TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_s 

    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}

    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install PkgConfig data
install(
		FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
		DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
