# Infuse-IoT SDK compilation

set(INFUSE_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE PATH "Infuse-IoT SDK root directory")

zephyr_library_named(infuse)

if(CONFIG_FILE_SYSTEM_LIB_LINK)
  zephyr_library_link_libraries(FS)
endif()

function(infuse_sources)
  foreach(arg ${ARGV})
    if(IS_DIRECTORY ${arg})
      message(FATAL_ERROR "infuse_sources() was called on a directory")
    endif()
    target_sources(infuse PRIVATE ${arg})
  endforeach()
endfunction()

function(infuse_sources_ifdef feature_toggle)
  if(${${feature_toggle}})
    infuse_sources(${ARGN})
  endif()
endfunction()

function(infuse_sources_ifndef feature_toggle)
  if(NOT ${feature_toggle})
    infuse_sources(${ARGN})
  endif()
endfunction()

zephyr_include_directories(include)
if(NOT CONFIG_INFUSE_DEFS_GENERATED_DOWNSTREAM)
  zephyr_include_directories(generated/include)
endif()

add_subdirectory(drivers)
add_subdirectory(lib)
add_subdirectory(subsys)

if(CONFIG_INFUSE_SDK)
  if(CONFIG_BUILD_WITH_TFM)
    # PSA algorithm support
    set(INFUSE_TFM_MBEDTLS_PATH ${INFUSE_DIR}/include/infuse/tfm/mbedtls_config_extra.h)
    set_property(TARGET zephyr_property_target
      APPEND PROPERTY TFM_CMAKE_OPTIONS
      -DTFM_MBEDCRYPTO_PLATFORM_EXTRA_CONFIG_PATH=${INFUSE_TFM_MBEDTLS_PATH}
    )
  endif()
endif()

# Required for PSA headers
zephyr_link_libraries_ifdef(CONFIG_MBEDTLS mbedTLS)

include(cmake/sdk_nrf.cmake)
