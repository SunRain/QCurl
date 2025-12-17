include(ExternalProject)
include(ProcessorCount)

option(QCURL_LC_BUILD_NGHTTPX_H3
       "Build h3-capable nghttpx from source for libcurl_consistency"
       ON)

set(QCURL_LC_NGHTTP2_VERSION
    "1.68.0"
    CACHE STRING "nghttp2 release version used to build nghttpx")

set(QCURL_LC_NGHTTP2_ARCHIVE
    ""
    CACHE FILEPATH "Optional local nghttp2-<ver>.tar.gz archive for offline builds")

set(QCURL_LC_NGHTTPX_H3_PREFIX
    "${CMAKE_BINARY_DIR}/libcurl_consistency/nghttpx-h3"
    CACHE PATH "Install prefix for the h3-capable nghttpx build")

if(NOT QCURL_LC_BUILD_NGHTTPX_H3)
    add_custom_target(qcurl_nghttpx_h3
                      COMMAND "${CMAKE_COMMAND}" -E echo
                              "QCURL_LC_BUILD_NGHTTPX_H3=OFF (skip building nghttpx)")
else()
    find_program(QCURL_LC_MAKE_PROGRAM NAMES gmake make REQUIRED)

    ProcessorCount(_qcurl_lc_nproc)
    if(_qcurl_lc_nproc EQUAL 0)
        set(_qcurl_lc_nproc 4)
    endif()

    set(_qcurl_lc_nghttp2_url
        "https://github.com/nghttp2/nghttp2/releases/download/v${QCURL_LC_NGHTTP2_VERSION}/nghttp2-${QCURL_LC_NGHTTP2_VERSION}.tar.gz")
    if(QCURL_LC_NGHTTP2_ARCHIVE)
        set(_qcurl_lc_nghttp2_url "${QCURL_LC_NGHTTP2_ARCHIVE}")
    endif()

    set(_qcurl_lc_nghttpx_bin "${QCURL_LC_NGHTTPX_H3_PREFIX}/bin/nghttpx")

    ExternalProject_Add(qcurl_nghttpx_h3_ep
                        URL "${_qcurl_lc_nghttp2_url}"
                        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
                        UPDATE_DISCONNECTED TRUE
                        INSTALL_DIR "${QCURL_LC_NGHTTPX_H3_PREFIX}"
                        CONFIGURE_COMMAND
                            <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --enable-http3
                        BUILD_COMMAND
                            "${QCURL_LC_MAKE_PROGRAM}" -j "${_qcurl_lc_nproc}"
                        INSTALL_COMMAND
                            "${QCURL_LC_MAKE_PROGRAM}" install
                        BUILD_BYPRODUCTS
                            "${_qcurl_lc_nghttpx_bin}")

    add_custom_target(qcurl_nghttpx_h3 DEPENDS qcurl_nghttpx_h3_ep)
endif()
