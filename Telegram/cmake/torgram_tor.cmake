# Downloads the Tor Expert Bundle for the current target platform and stages
# it next to the built Telegram binary so Torgram can ship with a working Tor
# daemon out of the box.

if (DEFINED TORGRAM_TOR_BUNDLE_DONE)
    return()
endif()
set(TORGRAM_TOR_BUNDLE_DONE TRUE)

set(TORGRAM_TOR_VERSION "14.5.9" CACHE STRING
    "Version of Tor Browser / Tor Expert Bundle to bundle with Torgram.")

set(TORGRAM_TOR_BASE_URL
    "https://archive.torproject.org/tor-package-archive/torbrowser/${TORGRAM_TOR_VERSION}"
    CACHE STRING "Base URL of the Tor package archive.")

if (APPLE)
    if (CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(_torgram_tor_platform "macos-aarch64")
    else()
        set(_torgram_tor_platform "macos-x86_64")
    endif()
elseif (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_torgram_tor_platform "windows-x86_64")
    else()
        set(_torgram_tor_platform "windows-i686")
    endif()
else()
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_torgram_tor_platform "linux-aarch64")
    else()
        set(_torgram_tor_platform "linux-x86_64")
    endif()
endif()

set(_torgram_tor_archive
    "tor-expert-bundle-${_torgram_tor_platform}-${TORGRAM_TOR_VERSION}.tar.gz")
set(_torgram_tor_url "${TORGRAM_TOR_BASE_URL}/${_torgram_tor_archive}")
set(_torgram_tor_dir "${CMAKE_BINARY_DIR}/torgram-tor")
set(_torgram_tor_path "${_torgram_tor_dir}/${_torgram_tor_archive}")
set(_torgram_tor_extracted "${_torgram_tor_dir}/extracted")
set(_torgram_tor_stamp "${_torgram_tor_dir}/.stamp-${TORGRAM_TOR_VERSION}")

file(MAKE_DIRECTORY "${_torgram_tor_dir}")

if (NOT EXISTS "${_torgram_tor_stamp}")
    message(STATUS "Torgram: downloading ${_torgram_tor_url}")
    file(DOWNLOAD
        "${_torgram_tor_url}"
        "${_torgram_tor_path}"
        SHOW_PROGRESS
        STATUS _torgram_tor_status
        TLS_VERIFY ON)
    list(GET _torgram_tor_status 0 _torgram_tor_code)
    if (NOT _torgram_tor_code EQUAL 0)
        message(WARNING
            "Torgram: failed to download bundled Tor (${_torgram_tor_status}). "
            "Torgram will require a locally-running Tor on 127.0.0.1:9050.")
        file(REMOVE "${_torgram_tor_path}")
        return()
    endif()

    file(REMOVE_RECURSE "${_torgram_tor_extracted}")
    file(MAKE_DIRECTORY "${_torgram_tor_extracted}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${_torgram_tor_path}"
        WORKING_DIRECTORY "${_torgram_tor_extracted}"
        RESULT_VARIABLE _torgram_tor_extract_result)
    if (NOT _torgram_tor_extract_result EQUAL 0)
        message(WARNING "Torgram: failed to extract ${_torgram_tor_archive}.")
        file(REMOVE_RECURSE "${_torgram_tor_extracted}")
        return()
    endif()
    file(WRITE "${_torgram_tor_stamp}" "${TORGRAM_TOR_VERSION}")
endif()

if (WIN32)
    set(_torgram_tor_binary "${_torgram_tor_extracted}/tor/tor.exe")
    set(_torgram_tor_relative_dir "tor-bundle")
elseif (APPLE)
    set(_torgram_tor_binary "${_torgram_tor_extracted}/tor/tor")
    set(_torgram_tor_relative_dir "tor-bundle")
else()
    set(_torgram_tor_binary "${_torgram_tor_extracted}/tor/tor")
    set(_torgram_tor_relative_dir "tor-bundle")
endif()

if (NOT EXISTS "${_torgram_tor_binary}")
    message(WARNING
        "Torgram: extracted bundle at ${_torgram_tor_extracted} does not "
        "contain expected tor binary (${_torgram_tor_binary}).")
    return()
endif()

if (APPLE)
    set(_torgram_tor_stage "$<TARGET_FILE_DIR:Telegram>/../Resources/${_torgram_tor_relative_dir}")
else()
    set(_torgram_tor_stage "$<TARGET_FILE_DIR:Telegram>/${_torgram_tor_relative_dir}")
endif()

add_custom_command(TARGET Telegram POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_torgram_tor_stage}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${_torgram_tor_extracted}/tor"
        "${_torgram_tor_stage}"
    COMMENT "Staging bundled Tor daemon for Torgram")

target_compile_definitions(Telegram
PRIVATE
    TORGRAM_TOR_BUNDLE_RELATIVE_DIR="${_torgram_tor_relative_dir}"
    TORGRAM_TOR_VERSION="${TORGRAM_TOR_VERSION}")
