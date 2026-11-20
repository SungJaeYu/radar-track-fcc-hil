# One backend per image. Host and Zephyr builds use the same selection.
set(RADAR_ESTIMATOR "polar_cv" CACHE STRING "Tracking estimator backend")
set_property(CACHE RADAR_ESTIMATOR PROPERTY STRINGS polar_cv)

function(target_add_radar_estimator target)
    set(src "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src")
    if(RADAR_ESTIMATOR STREQUAL "polar_cv")
        target_sources(${target} PRIVATE
            "${src}/tracking/estimators/polar_cv.c"
            "${src}/tracking/kalman_cv.c")
        target_compile_definitions(${target} PRIVATE
            RADAR_ESTIMATOR_HEADER="tracking/estimators/polar_cv.h")
    else()
        message(FATAL_ERROR "Unknown RADAR_ESTIMATOR='${RADAR_ESTIMATOR}'. Supported: polar_cv")
    endif()
endfunction()
