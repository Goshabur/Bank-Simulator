cmake_minimum_required(VERSION 3.10)

project(bank CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(../../default-options.cmake)

find_package(Threads)
find_package(Boost 1.71 REQUIRED system)

set(NETWORKING_LIBS)
if (WIN32 AND NOT MSVC AND NOT BORLAND)
    # Boost::Asio automatically links with necessary
    # libraries when using Visual C++ or Borland C++,
    # see BOOST_ASIO_NO_DEFAULT_LINKED_LIBS.
    set(NETWORKING_LIBS "ws2_32")
endif ()

add_executable(bank-test doctest_main.cpp bank_test.cpp bank.cpp)
target_link_libraries(bank-test ${CMAKE_THREAD_LIBS_INIT})

add_executable(bank-server bank_server.cpp bank.cpp)
target_include_directories(bank-server PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(bank-server ${CMAKE_THREAD_LIBS_INIT} ${Boost_LIBRARIES} ${NETWORKING_LIBS})
