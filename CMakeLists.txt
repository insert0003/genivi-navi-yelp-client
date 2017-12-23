cmake_minimum_required(VERSION 2.8.11)
INCLUDE(FindPkgConfig)

# usefull flags:
# -DCMAKE_BUILD_TYPE=Debug
# -DCMAKE_BUILD_TYPE=Release
# -DCMAKE_INSTALL_PREFIX=install

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

project(yelp-client)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fPIC ")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
find_package(Qt5Widgets REQUIRED)

pkg_check_modules(QT5_CORE REQUIRED Qt5Core)
pkg_check_modules(JSON REQUIRED json-c)
pkg_check_modules(QT5_WIDGETS REQUIRED Qt5Widgets)
pkg_check_modules(QT5_NETWORK REQUIRED Qt5Network)
pkg_check_modules(AGL_NAVIAPI REQUIRED libnaviapi-agl)
pkg_check_modules(AFBWSC REQUIRED libafbwsc)
pkg_check_modules(QLIBWIN REQUIRED qlibwindowmanager)
pkg_check_modules(LIBHS REQUIRED libhomescreen)



include_directories( ${PROJECT_SOURCE_DIR}/ ${AFBWSC_INCLUDE_DIRS} ${AGL_NAVIAPI_INCLUDE_DIRS} ${QT5_CORE_INCLUDE_DIRS} ${QT5_WIDGETS_INCLUDE_DIRS} ${QT5_NETWORK_INCLUDE_DIRS} ${JSON_INCLUDE_DIRS} ${QLIBWIN_INCLUDE_DIRS} ${LIBHS_INCLUDE_DIRS} )

qt5_wrap_cpp(yelp_client_SRC MainApp.h InfoPanelLabel.h ClickableLabel.h Keyboard.h)

QT5_ADD_RESOURCES(yelp_client_QRC yelp-client.qrc)
add_executable( yelp-client main.cpp MainApp.cpp InfoPanel.cpp InfoPanelLabel.cpp Keyboard.cpp yelp-client.qrc ${yelp_client_SRC} ${yelp_client_QRC})
#target_link_libraries( yelp-client binderclient )
target_link_libraries( yelp-client ${AFBWSC_LIBRARIES} ${AGL_NAVIAPI_LIBRARIES} ${JSON_LIBRARIES} ${QLIBWIN_LIBRARIES} ${LIBHS_LIBRARIES} )
qt5_use_modules(yelp-client Core Widgets Network)

install (TARGETS yelp-client DESTINATION bin)

##########################################################################
# AGL binding
configure_file(config.xml.in config.xml)

add_custom_command(
	OUTPUT poi.wgt
	DEPENDS yelp-client
	COMMAND rm -rf package
	COMMAND mkdir -p package/root
	COMMAND cp config.xml package/root/
	COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/HMI_AppLauncher_POI_Active-01.png package/root/icon.png
	COMMAND cp yelp-client package/root/poi
	COMMAND wgtpkg-pack -f -o package/poi.wgt package/root
)
add_custom_target(widget ALL DEPENDS poi.wgt)
