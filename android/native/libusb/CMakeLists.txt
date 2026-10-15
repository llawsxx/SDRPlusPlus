cmake_minimum_required(VERSION 3.22)
project(sdrpp_libusb C)

add_library(usb1.0 SHARED
    "${LIBUSB_SOURCE}/libusb/core.c"
    "${LIBUSB_SOURCE}/libusb/descriptor.c"
    "${LIBUSB_SOURCE}/libusb/hotplug.c"
    "${LIBUSB_SOURCE}/libusb/io.c"
    "${LIBUSB_SOURCE}/libusb/sync.c"
    "${LIBUSB_SOURCE}/libusb/strerror.c"
    "${LIBUSB_SOURCE}/libusb/os/linux_usbfs.c"
    "${LIBUSB_SOURCE}/libusb/os/events_posix.c"
    "${LIBUSB_SOURCE}/libusb/os/threads_posix.c"
    "${LIBUSB_SOURCE}/libusb/os/linux_netlink.c"
)
target_include_directories(usb1.0 PRIVATE
    "${LIBUSB_SOURCE}/android"
    "${LIBUSB_SOURCE}/libusb"
    "${LIBUSB_SOURCE}/libusb/os"
)
target_compile_options(usb1.0 PRIVATE -fvisibility=hidden -pthread)
target_link_libraries(usb1.0 PRIVATE log)
