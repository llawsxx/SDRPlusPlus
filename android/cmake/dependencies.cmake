option(SDRPP_ANDROID_BUILD_DEPS "Build Android dependencies from pinned sources" OFF)

if(SDRPP_ANDROID_BUILD_DEPS)
    set(SDRPP_ANDROID_DEPS_CACHE "${CMAKE_SOURCE_DIR}/android/deps" CACHE PATH "Android dependency cache")
    set(SDRPP_ANDROID_DEPS_ROOT "${SDRPP_ANDROID_DEPS_CACHE}/install/${ANDROID_ABI}" CACHE PATH "Android dependency prefix")
else()
    set(SDRPP_ANDROID_DEPS_ROOT "/sdr-kit/${ANDROID_ABI}" CACHE PATH "Android dependency prefix")
endif()

function(sdrpp_prepare_android_dependencies)
    file(MAKE_DIRECTORY "${SDRPP_ANDROID_DEPS_CACHE}")
    file(LOCK "${SDRPP_ANDROID_DEPS_CACHE}/build.lock" GUARD FUNCTION TIMEOUT 3600)
    if(CMAKE_HOST_WIN32)
        set(python_directory "${SDRPP_ANDROID_DEPS_CACHE}/tools/python")
        set(python "${python_directory}/python.exe")
        if(NOT EXISTS "${python_directory}/.complete")
            file(MAKE_DIRECTORY "${python_directory}")
            message(STATUS "Downloading portable Python for Android dependency code generation")
            set(download_verified OFF)
            foreach(attempt RANGE 1 3)
                file(DOWNLOAD
                    "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip"
                    "${python_directory}/python.zip"
                    TLS_VERIFY ON
                    TIMEOUT 180
                    INACTIVITY_TIMEOUT 30
                    STATUS download_status
                )
                list(GET download_status 0 download_result)
                if(download_result EQUAL 0)
                    file(SHA256 "${python_directory}/python.zip" python_hash)
                    if(python_hash STREQUAL "4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3")
                        set(download_verified ON)
                        break()
                    endif()
                endif()
                execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
            endforeach()
            if(NOT download_verified)
                message(FATAL_ERROR "Portable Python download/checksum failed: ${download_status}. Check HTTPS_PROXY.")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${python_directory}/python.zip" DESTINATION "${python_directory}")
            file(WRITE "${python_directory}/.complete" "3.12.10")
        endif()
        file(WRITE "${python_directory}/python312._pth" "python312.zip\n.\nimport site\n")
    else()
        find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
        set(python "${Python3_EXECUTABLE}")
    endif()
    set(components zstd fftw volk)
    if(OPT_BUILD_AIRSPY_SOURCE OR OPT_BUILD_AIRSPYHF_SOURCE OR OPT_BUILD_HYDRASDR_SOURCE OR OPT_BUILD_HACKRF_SOURCE OR OPT_BUILD_RTL_SDR_SOURCE)
        list(APPEND components libusb)
    endif()
    foreach(driver AIRSPY AIRSPYHF HYDRASDR HACKRF)
        if(OPT_BUILD_${driver}_SOURCE)
            string(TOLOWER "${driver}" dependency)
            list(APPEND components "${dependency}")
        endif()
    endforeach()
    if(OPT_BUILD_RTL_SDR_SOURCE)
        list(APPEND components rtlsdr)
    endif()
    if(OPT_BUILD_M17_DECODER OR OPT_BUILD_DAB_DECODER)
        list(APPEND components codec2)
    endif()
    if(OPT_BUILD_PLUTOSDR_SOURCE)
        list(APPEND components libxml2 libiio libad9361)
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "PYTHONUTF8=1"
            "${python}" "${CMAKE_SOURCE_DIR}/android/native/build.py"
            --cache "${SDRPP_ANDROID_DEPS_CACHE}"
            --prefix "${SDRPP_ANDROID_DEPS_ROOT}"
            --ndk "${ANDROID_NDK}"
            --cmake "${CMAKE_COMMAND}"
            --ninja "${CMAKE_MAKE_PROGRAM}"
            --abi "${ANDROID_ABI}"
            --platform "${ANDROID_PLATFORM}"
            --components ${components}
        RESULT_VARIABLE dependency_result
    )
    if(NOT dependency_result EQUAL 0)
        message(FATAL_ERROR "Android dependency build failed. See android/deps/build/${ANDROID_ABI}/*/build.log.")
    endif()
endfunction()

if(SDRPP_ANDROID_BUILD_DEPS)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/android/native/build.py"
        "${CMAKE_SOURCE_DIR}/android/native/codebook.py"
        "${CMAKE_SOURCE_DIR}/android/native/sources.json"
        "${CMAKE_SOURCE_DIR}/android/native/unversioned.cmake"
        "${CMAKE_SOURCE_DIR}/android/native/libusb/CMakeLists.txt"
    )
    sdrpp_prepare_android_dependencies()
endif()

function(sdrpp_android_import name filename)
    if(SDRPP_ANDROID_BUILD_DEPS)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SDRPP_ANDROID_DEPS_ROOT}/lib/${filename}")
    endif()
    add_library(sdrpp_android::${name} SHARED IMPORTED GLOBAL)
    set_target_properties(sdrpp_android::${name} PROPERTIES
        IMPORTED_LOCATION "${SDRPP_ANDROID_DEPS_ROOT}/lib/${filename}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDRPP_ANDROID_DEPS_ROOT}/include"
    )
endfunction()

sdrpp_android_import(volk libvolk.so)
sdrpp_android_import(fftw libfftw3f.so)
sdrpp_android_import(zstd libzstd.so)
if(OPT_BUILD_AIRSPY_SOURCE OR OPT_BUILD_AIRSPYHF_SOURCE OR OPT_BUILD_HYDRASDR_SOURCE OR OPT_BUILD_HACKRF_SOURCE OR OPT_BUILD_RTL_SDR_SOURCE)
    sdrpp_android_import(usb libusb1.0.so)
endif()
foreach(driver AIRSPY AIRSPYHF HYDRASDR HACKRF)
    if(OPT_BUILD_${driver}_SOURCE)
        string(TOLOWER "${driver}" dependency)
        sdrpp_android_import("${dependency}" "lib${dependency}.so")
        target_link_libraries(sdrpp_android::${dependency} INTERFACE sdrpp_android::usb)
    endif()
endforeach()
if(OPT_BUILD_RTL_SDR_SOURCE)
    sdrpp_android_import(rtlsdr librtlsdr.so)
    target_link_libraries(sdrpp_android::rtlsdr INTERFACE sdrpp_android::usb)
endif()
if(OPT_BUILD_M17_DECODER OR OPT_BUILD_DAB_DECODER)
    sdrpp_android_import(codec2 libcodec2.so)
endif()
if(OPT_BUILD_PLUTOSDR_SOURCE)
    sdrpp_android_import(xml2 libxml2.so)
    sdrpp_android_import(iio libiio.so)
    sdrpp_android_import(ad9361 libad9361.so)
    target_link_libraries(sdrpp_android::iio INTERFACE sdrpp_android::xml2)
    target_link_libraries(sdrpp_android::ad9361 INTERFACE sdrpp_android::iio)
endif()
