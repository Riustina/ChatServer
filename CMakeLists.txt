cmake_minimum_required(VERSION 3.21)

project(ChatServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CHATSERVER_COPY_RUNTIME_FILES "Copy config files and MySQL runtime DLLs after build on Windows" ON)

set(MYSQL_CONNECTOR_ROOT "" CACHE PATH "MySQL Connector/C++ root directory")
set(MYSQL_CONNECTOR_INCLUDE_DIR "" CACHE PATH "MySQL Connector/C++ include directory")
set(MYSQL_CONNECTOR_LIBRARY_DIR "" CACHE PATH "MySQL Connector/C++ library directory")

if(MYSQL_CONNECTOR_ROOT)
    if(NOT MYSQL_CONNECTOR_INCLUDE_DIR)
        set(MYSQL_CONNECTOR_INCLUDE_DIR "${MYSQL_CONNECTOR_ROOT}/include")
    endif()
    if(NOT MYSQL_CONNECTOR_LIBRARY_DIR)
        if(WIN32)
            set(MYSQL_CONNECTOR_LIBRARY_DIR "${MYSQL_CONNECTOR_ROOT}/lib64/debug/vs14")
        else()
            if(EXISTS "${MYSQL_CONNECTOR_ROOT}/lib64")
                set(MYSQL_CONNECTOR_LIBRARY_DIR "${MYSQL_CONNECTOR_ROOT}/lib64")
            else()
                set(MYSQL_CONNECTOR_LIBRARY_DIR "${MYSQL_CONNECTOR_ROOT}/lib")
            endif()
        endif()
    endif()
endif()

find_package(Boost REQUIRED COMPONENTS filesystem system)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(Threads REQUIRED)

if(TARGET JsonCpp::JsonCpp)
    set(JSONCPP_LINK_TARGET JsonCpp::JsonCpp)
elseif(TARGET jsoncpp_lib)
    set(JSONCPP_LINK_TARGET jsoncpp_lib)
elseif(TARGET jsoncpp_static)
    set(JSONCPP_LINK_TARGET jsoncpp_static)
else()
    message(FATAL_ERROR "未找到可用的 JsonCpp CMake target。")
endif()

if(MYSQL_CONNECTOR_INCLUDE_DIR)
    set(_mysql_include_hints "${MYSQL_CONNECTOR_INCLUDE_DIR}")
else()
    set(_mysql_include_hints)
endif()

if(MYSQL_CONNECTOR_LIBRARY_DIR)
    set(_mysql_lib_hints "${MYSQL_CONNECTOR_LIBRARY_DIR}")
else()
    set(_mysql_lib_hints)
endif()

find_path(MYSQLCPPCONN_INCLUDE_PATH
    NAMES jdbc/mysql_driver.h
    HINTS ${_mysql_include_hints}
)

find_library(MYSQLCPPCONN_LIBRARY
    NAMES mysqlcppconn
    HINTS ${_mysql_lib_hints}
)

find_library(MYSQLCPPCONNX_LIBRARY
    NAMES mysqlcppconnx
    HINTS ${_mysql_lib_hints}
)

if(NOT MYSQLCPPCONN_INCLUDE_PATH)
    message(FATAL_ERROR "未找到 MySQL Connector/C++ 头文件。请设置 MYSQL_CONNECTOR_ROOT 或 MYSQL_CONNECTOR_INCLUDE_DIR。")
endif()

if(NOT MYSQLCPPCONN_LIBRARY)
    message(FATAL_ERROR "未找到 mysqlcppconn 库。请设置 MYSQL_CONNECTOR_ROOT 或 MYSQL_CONNECTOR_LIBRARY_DIR。")
endif()

set(CHATSERVER_SOURCES
    AsioIOServicePool.cpp
    ChatServer.cpp
    ConfigManager.cpp
    CServer.cpp
    CSession.cpp
    LogicSystem.cpp
    message.grpc.pb.cc
    message.pb.cc
    MsgNode.cpp
    MySqlDao.cpp
    MySqlMgr.cpp
    StatusGrpcClient.cpp
)

set(CHATSERVER_HEADERS
    AsioIOServicePool.h
    ConfigManager.h
    CServer.h
    CSession.h
    data.h
    Defer.h
    global.h
    LogicSystem.h
    message.grpc.pb.h
    message.pb.h
    MsgNode.h
    MySqlDao.h
    MySqlMgr.h
    Singleton.h
    StatusGrpcClient.h
)

add_executable(ChatServer
    ${CHATSERVER_SOURCES}
    ${CHATSERVER_HEADERS}
)

target_include_directories(ChatServer PRIVATE
    ${MYSQLCPPCONN_INCLUDE_PATH}
)

target_link_libraries(ChatServer PRIVATE
    Boost::filesystem
    Boost::system
    protobuf::libprotobuf
    gRPC::grpc++
    ${JSONCPP_LINK_TARGET}
    Threads::Threads
    ${MYSQLCPPCONN_LIBRARY}
)

if(MYSQLCPPCONNX_LIBRARY)
    target_link_libraries(ChatServer PRIVATE ${MYSQLCPPCONNX_LIBRARY})
endif()

if(WIN32)
    target_compile_definitions(ChatServer PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(ChatServer PRIVATE ws2_32)
endif()

if(WIN32 AND CHATSERVER_COPY_RUNTIME_FILES)
    foreach(config_name IN ITEMS config.ini config_8090.ini config_8091.ini start_multi_chatserver.ps1)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${config_name}")
            add_custom_command(TARGET ChatServer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_CURRENT_SOURCE_DIR}/${config_name}"
                        "$<TARGET_FILE_DIR:ChatServer>/${config_name}"
                COMMENT "Copying ${config_name}"
            )
        endif()
    endforeach()

    file(GLOB MYSQL_RUNTIME_DLLS
        "${CMAKE_CURRENT_SOURCE_DIR}/mysqlcppconn*.dll"
    )

    foreach(mysql_runtime_dll IN LISTS MYSQL_RUNTIME_DLLS)
        add_custom_command(TARGET ChatServer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${mysql_runtime_dll}"
                    "$<TARGET_FILE_DIR:ChatServer>/"
            COMMENT "Copying ${mysql_runtime_dll}"
        )
    endforeach()
endif()
