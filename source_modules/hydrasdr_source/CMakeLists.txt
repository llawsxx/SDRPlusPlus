cmake_minimum_required(VERSION 3.13)
project(hydrasdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    target_link_directories(hydrasdr_source PRIVATE "C:/Program Files/hydrasdr-host/lib/")

    target_include_directories(hydrasdr_source PUBLIC "C:/Program Files/hydrasdr-host/include/libhydrasdr")

    target_link_libraries(hydrasdr_source PRIVATE hydrasdr)
elseif (ANDROID)
    target_include_directories(hydrasdr_source PUBLIC
    "${SDRPP_ANDROID_DEPS_ROOT}/include/libhydrasdr"
    )

    target_link_libraries(hydrasdr_source PUBLIC
    sdrpp_android::usb
    sdrpp_android::hydrasdr
)
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(LIBHYDRASDR REQUIRED libhydrasdr)

    target_include_directories(hydrasdr_source PRIVATE ${LIBHYDRASDR_INCLUDE_DIRS})
    target_link_directories(hydrasdr_source PRIVATE ${LIBHYDRASDR_LIBRARY_DIRS})
    target_link_libraries(hydrasdr_source PRIVATE ${LIBHYDRASDR_LIBRARIES})

    # Include it because for some reason pkgconfig doesn't look here?
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(hydrasdr_source PRIVATE "/usr/local/include")
    endif()
endif ()
