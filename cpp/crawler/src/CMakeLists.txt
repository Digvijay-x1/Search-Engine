cmake_minimum_required(VERSION 3.10)
project(Crawler)

set(CMAKE_CXX_STANDARD 17)

# Find Packages (Optional but good practice)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

# Include Directories
include_directories(${CURL_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})

add_executable(crawler main.cpp warc_writer.cpp)

# LINK THE LIBRARIES
# curl: Networking
# pqxx: Postgres C++ Wrapper
# pq: Postgres C Backend
# hiredis: Redis C Backend
# z: Zlib
target_link_libraries(crawler curl pqxx pq hiredis z)

# Testing
enable_testing()

add_executable(test_crawler ../tests/test_warc_writer.cpp warc_writer.cpp)
target_link_libraries(test_crawler curl pqxx pq hiredis z)

add_test(NAME WarcWriterTest COMMAND test_crawler)

