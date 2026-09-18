# Independent installed consumers must use the producer compiler and instrumentation.
set(QCURL_PUBLIC_API_CONSUMER_CACHE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/consumer-profile.cmake")
set(_qcurl_consumer_profile_variables
    CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_ARG1 CMAKE_BUILD_TYPE
    CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS)
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
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${QCURL_PUBLIC_API_CONSUMER_CACHE}" "${_qcurl_consumer_profile}")
