cmake_minimum_required(VERSION 3.16)
project(DBMiddleWareServer LANGUAGES CXX)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    DBMiddleWareApplication/DBMiddleWareApplication.cpp
    DBMiddleWareApplication/DataHandler.cpp
    DBMiddleWareApplication/AllowedIPManager.cpp
    DBMiddleWareApplication/SessionPool.cpp
    DBMiddleWareApplication/SessionManager.cpp
    DBMiddleWareApplication/MessageDispatcher.cpp
    DBMiddleWareApplication/Logger.cpp
    DBMiddleWareApplication/Utility.cpp
    DBMiddleWareApplication/Server.cpp
    DBMiddleWareApplication/MessageBufferManager.cpp
    DBMiddleWareApplication/MemoryTracker.cpp
    # ↓ DB 소스 추가했다면 주석 해제
    DBMiddleWareApplication/MySqlPool.cpp
)

add_executable(server ${SOURCES})

# 소스 트리 헤더 인클루드
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/DBMiddleWareApplication)

# Boost
find_package(Boost REQUIRED COMPONENTS system thread)
target_link_libraries(server PRIVATE Boost::system Boost::thread)

# nlohmann_json
find_package(nlohmann_json 3.2.0 REQUIRED)
target_link_libraries(server PRIVATE nlohmann_json::nlohmann_json)

# spdlog
find_package(spdlog REQUIRED)
target_link_libraries(server PRIVATE spdlog::spdlog)

# libcurl (Utility.cpp의 Slack 웹훅용)
find_package(CURL REQUIRED)
target_link_libraries(server PRIVATE CURL::libcurl)

# Threads (POSIX)
find_package(Threads REQUIRED)
target_link_libraries(server PRIVATE Threads::Threads)

# ---- MySQL Connector/C++ (크로스플랫폼 자동 감지) ----
# vcpkg 설치 시 제공되는 CMake 패키지 이름(권장 경로)
find_package(unofficial-mysql-connector-cpp CONFIG QUIET)

if(unofficial-mysql-connector-cpp_FOUND)
  # vcpkg 경로: 자동 링크
  target_link_libraries(server PRIVATE unofficial::mysql-connector-cpp)
else()
  # 수동 탐색 (시스템/수동 설치 둘 다 커버)
  # 헤더는 cppconn/driver.h 또는 jdbc/cppconn/driver.h 두 패턴 존재
  find_path(MYSQLCPPCONN_INCLUDE_DIR
    NAMES cppconn/driver.h jdbc/cppconn/driver.h
    HINTS
      "$ENV{VCPKG_ROOT}/installed/x64-windows/include"
      "C:/Program Files/MySQL/Connector C++ 8.4/include"
      "C:/Program Files/MySQL/Connector C++ 8.0/include"
      /usr/include /usr/local/include
      /opt/homebrew/include /opt/homebrew/include/jdbc
  )

  # 라이브러리는 환경별로 mysqlcppconn(레거시) 또는 mysqlcppconn8 이름일 수 있음
  find_library(MYSQLCPPCONN_LIBRARY
    NAMES mysqlcppconn mysqlcppconn8
    HINTS
      "$ENV{VCPKG_ROOT}/installed/x64-windows/lib"
      "C:/Program Files/MySQL/Connector C++ 8.4/lib64/vs16"
      "C:/Program Files/MySQL/Connector C++ 8.0/lib64/vs14"
      /usr/lib /usr/local/lib /opt/homebrew/lib
  )

  if(NOT MYSQLCPPCONN_INCLUDE_DIR OR NOT MYSQLCPPCONN_LIBRARY)
    message(FATAL_ERROR "MySQL Connector/C++ not found. Install it (apt/vcpkg/installer) or set HINTS/CMAKE_PREFIX_PATH.")
  endif()

  # include 경로를 <cppconn/...>로 인식되게 맞춤
  if(EXISTS "${MYSQLCPPCONN_INCLUDE_DIR}/cppconn/driver.h")
    set(MYSQLCPPCONN_PUBLIC_INCLUDE "${MYSQLCPPCONN_INCLUDE_DIR}")
  elseif(EXISTS "${MYSQLCPPCONN_INCLUDE_DIR}/jdbc/cppconn/driver.h")
    set(MYSQLCPPCONN_PUBLIC_INCLUDE "${MYSQLCPPCONN_INCLUDE_DIR}/jdbc")
  else()
    message(FATAL_ERROR "Could not locate cppconn/driver.h under ${MYSQLCPPCONN_INCLUDE_DIR}")
  endif()

  target_include_directories(server PRIVATE ${MYSQLCPPCONN_PUBLIC_INCLUDE})
  target_link_libraries(server PRIVATE ${MYSQLCPPCONN_LIBRARY})
endif()

# ---- OS 별 추가 라이브러리/정의 ----
if (WIN32)
  # Windows 소켓/인증 라이브러리
  target_link_libraries(server PRIVATE ws2_32 crypt32)
  # 기존 전역 add_definitions 대신 타깃에만 부여
  target_compile_definitions(server PRIVATE _WIN32_WINNT=0x0A00)
endif()
