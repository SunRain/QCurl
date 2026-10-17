# Independent installed consumers use the producer compiler, flags and dependencies.
set(QCURL_PRODUCER_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
set(QCURL_PRODUCER_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
set(QCURL_PRODUCER_QT_VERSION "${Qt6_VERSION}")
set(QCURL_PRODUCER_CURL_VERSION "${QCURL_LIBCURL_VERSION_STRING}")
set(QCURL_PRODUCER_VERSION "${PROJECT_VERSION}")
set(QCURL_PRODUCER_ABI_VERSION "${QCURL_ABI_VERSION}")
set(QCURL_PRODUCER_WEBSOCKET_SUPPORT "${QCURL_WEBSOCKET_SUPPORT}")
set(QCURL_PRODUCER_SHARED "${QCURL_BUILD_SHARED_LIBS}")
if(CMAKE_CXX_COMPILER_LOADED)
    include(CheckCXXSourceCompiles)
    foreach(abi IN ITEMS 1 0)
        unset(_qcurl_glibcxx_abi_${abi} CACHE)
        check_cxx_source_compiles("#include <string>
#if !defined(_GLIBCXX_USE_CXX11_ABI) || _GLIBCXX_USE_CXX11_ABI != ${abi}
#error unexpected libstdc++ ABI
#endif
int main() { return 0; }" _qcurl_glibcxx_abi_${abi})
    endforeach()
    if(_qcurl_glibcxx_abi_1)
        set(QCURL_PRODUCER_GLIBCXX_USE_CXX11_ABI 1)
    elseif(_qcurl_glibcxx_abi_0)
        set(QCURL_PRODUCER_GLIBCXX_USE_CXX11_ABI 0)
    else()
        unset(_qcurl_other_standard_library CACHE)
        check_cxx_source_compiles("#include <string>
#ifdef _GLIBCXX_USE_CXX11_ABI
#error unrecognized libstdc++ ABI
#endif
int main() { return 0; }" _qcurl_other_standard_library)
        if(NOT _qcurl_other_standard_library)
            message(FATAL_ERROR "Cannot determine producer standard library ABI")
        endif()
        set(QCURL_PRODUCER_GLIBCXX_USE_CXX11_ABI "not-libstdc++")
    endif()
endif()
set(QCURL_CONSUMER_PROFILE "${CMAKE_BINARY_DIR}/generated/consumer-profile.cmake")
set(_qcurl_consumer_profile_variables
    CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_ARG1 CMAKE_BUILD_TYPE
    CMAKE_CXX_STANDARD CMAKE_CXX_STANDARD_REQUIRED CMAKE_CXX_EXTENSIONS
    CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_PREFIX_PATH
    Qt6_DIR Qt6Core_DIR Qt6Network_DIR
    QCURL_PRODUCER_COMPILER_ID QCURL_PRODUCER_COMPILER_VERSION
    QCURL_PRODUCER_QT_VERSION QCURL_PRODUCER_CURL_VERSION
    QCURL_PRODUCER_VERSION QCURL_PRODUCER_ABI_VERSION
    QCURL_PRODUCER_WEBSOCKET_SUPPORT QCURL_PRODUCER_SHARED
    QCURL_PRODUCER_GLIBCXX_USE_CXX11_ABI)
foreach(variable IN ITEMS CURL_ROOT CURL_DIR CURL_INCLUDE_DIR
        CURL_LIBRARY_RELEASE CURL_LIBRARY_DEBUG CMAKE_POSITION_INDEPENDENT_CODE)
    if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
        list(APPEND _qcurl_consumer_profile_variables "${variable}")
    endif()
endforeach()
set(_qcurl_consumer_configurations
    Debug Release RelWithDebInfo MinSizeRel ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
list(REMOVE_DUPLICATES _qcurl_consumer_configurations)
foreach(configuration IN LISTS _qcurl_consumer_configurations)
    string(TOUPPER "${configuration}" configuration)
    list(APPEND _qcurl_consumer_profile_variables
        "CMAKE_CXX_FLAGS_${configuration}" "CMAKE_EXE_LINKER_FLAGS_${configuration}")
endforeach()

set(_qcurl_consumer_profile "")
foreach(variable IN LISTS _qcurl_consumer_profile_variables)
    # Bracket arguments preserve flag quoting, list separators and literal dollar signs.
    set(delimiter "=")
    while("${${variable}}" MATCHES "\\]${delimiter}\\]")
        string(APPEND delimiter "=")
    endwhile()
    string(APPEND _qcurl_consumer_profile
        "set(${variable} [${delimiter}[${${variable}}]${delimiter}] CACHE STRING \"Producer consumer profile\" FORCE)\n")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(WRITE "${QCURL_CONSUMER_PROFILE}" "${_qcurl_consumer_profile}")
