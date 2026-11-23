cmake_minimum_required(VERSION 3.16)
project(UIInputTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Test)
include_directories(${CMAKE_SOURCE_DIR}/../client/ui ${CMAKE_SOURCE_DIR}/../common/include)
add_executable(ui_login_test
    ui_login_test.cpp
    ${CMAKE_SOURCE_DIR}/../client/ui/LoginWindow.cpp
    ${CMAKE_SOURCE_DIR}/../client/ui/LoginWindow.h
)
target_link_libraries(ui_login_test PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test)
