# SPDX-License-Identifier: FSL-1.1-ALv2

# Export model macros for nrf_edgeai generated models
function(export_model_macros model_dir output_h)
    if(NOT IS_DIRECTORY "${model_dir}")
        message(FATAL_ERROR
            "MODEL_DIR must contain an existing nrf_edgeai_generated model directory: (${model_dir})")
    endif()

    if("${output_h}" STREQUAL "")
        message(FATAL_ERROR "OUTPUT_H must name the generated header")
    endif()

    set(MODEL_C "${model_dir}/nrf_edgeai_user_model.c")
    set(USER_TYPES_H "${model_dir}/nrf_edgeai_user_types.h")

    if(NOT EXISTS "${MODEL_C}")
        message(FATAL_ERROR
            "Model directory does not contain nrf_edgeai_user_model.c: ${model_dir}")
    endif()

    if(NOT EXISTS "${USER_TYPES_H}")
        message(FATAL_ERROR
            "Model directory does not contain nrf_edgeai_user_types.h: ${model_dir}")
    endif()

    add_custom_command(
        OUTPUT "${output_h}"
        COMMAND ${CMAKE_COMMAND}
            -DMODEL_C=${MODEL_C}
            -DUSER_TYPES_H=${USER_TYPES_H}
            -DOUTPUT_H=${output_h}
            -P ${CMAKE_CURRENT_FUNCTION_LIST_FILE}
        DEPENDS
            ${MODEL_C}
            ${USER_TYPES_H}
            ${CMAKE_CURRENT_FUNCTION_LIST_FILE}
        VERBATIM
    )
endfunction()

function(generate_model_macros model_c user_types_h output_h)
    set(MODEL_C "${model_c}")
    set(USER_TYPES_H "${user_types_h}")
    set(OUTPUT_H "${output_h}")

    file(READ "${MODEL_C}" model_source)
    file(READ "${USER_TYPES_H}" user_types_source)

    get_filename_component(OUTPUT_DIR "${OUTPUT_H}" DIRECTORY)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    message(STATUS "Generating nrf edge ai model macros header at ${OUTPUT_H}")

    file(WRITE "${OUTPUT_H}"
         "#ifndef GENERATED_MODEL_MACROS_H\n#define GENERATED_MODEL_MACROS_H\n")

    string(REPLACE "\n" ";" lines "${model_source}")

    foreach(line IN LISTS lines)
        if(line MATCHES "^[ \t]*#[ \t]*define[ \t]+(INPUT_UNIQ_FEATURES_USED_NUM|MODEL_OUTPUTS_NUM|INPUT_FEATURE_DATA_TYPE)([ \t]+.*)?$")
            file(APPEND "${OUTPUT_H}" "${line}\n")
        endif()
    endforeach()

    string(REPLACE "\n" ";" user_types_lines "${user_types_source}")
    set(model_output_type "")

    foreach(line IN LISTS user_types_lines)
        if(line MATCHES "^[ \t]*typedef[ \t]+(flt32_t|uint8_t|uint16_t)[ \t]+nrf_user_output_t[ \t]*;?[ \t]*$")
            set(model_output_type "${CMAKE_MATCH_1}")
        endif()
    endforeach()

    if(model_output_type STREQUAL "")
        message(FATAL_ERROR
            "Could not find a supported typedef for nrf_user_output_t in ${USER_TYPES_H}")
    endif()

    if(model_output_type STREQUAL "flt32_t")
        set(model_output_macro "MODEL_OUTPUT_TYPE_FLOAT")
    elseif(model_output_type STREQUAL "uint8_t")
        set(model_output_macro "MODEL_OUTPUT_TYPE_Q8")
    elseif(model_output_type STREQUAL "uint16_t")
        set(model_output_macro "MODEL_OUTPUT_TYPE_Q16")
    endif()

    file(APPEND "${OUTPUT_H}"
     "\n"
     "#ifndef INPUT_UNIQ_FEATURES_USED_NUM\n"
     "#error \"Missing number of inputs\"\n"
     "#endif\n"
     "#ifndef MODEL_OUTPUTS_NUM\n"
     "#error \"Missing number of outputs\"\n"
     "#endif\n"
     "#ifndef INPUT_FEATURE_DATA_TYPE\n"
     "#error \"Missing input feature data type\"\n"
     "#endif\n"
     "\n"
     "#define ${model_output_macro} 1\n"
     "\n"
     "#include <stdint.h>\n"
     "#include <nrf_edgeai/rt/nrf_edgeai_output_types.h>\n"
     "\n"
     "#if defined(MODEL_OUTPUT_TYPE_FLOAT)\n"
     "static inline double get_probability_as_double(\n"
     "    const nrf_edgeai_decoded_output_classif_t *probability,\n"
     "    uint32_t class_idx)\n"
     "{\n"
     "    return probability->probabilities.p_f32[class_idx];\n"
     "}\n"
     "#elif defined(MODEL_OUTPUT_TYPE_Q8)\n"
     "static inline double get_probability_as_double(\n"
     "    const nrf_edgeai_decoded_output_classif_t *probability,\n"
     "    uint32_t class_idx)\n"
     "{\n"
     "    /* Quantized values are interpreted as percentages: 0 - 1. */\n"
     "    return (double)probability->probabilities.p_q8[class_idx] / (float) UINT8_MAX;\n"
     "}\n"
     "#elif defined(MODEL_OUTPUT_TYPE_Q16)\n"
     "static inline double get_probability_as_double(\n"
     "    const nrf_edgeai_decoded_output_classif_t *probability,\n"
     "    uint32_t class_idx)\n"
     "{\n"
     "    /* Quantized values are interpreted as percentages: 0 - 1. */\n"
     "    return (double)probability->probabilities.p_q16[class_idx] / (float) UINT16_MAX;\n"
     "}\n"
     "#else\n"
     "#error \"Unsupported model output type\"\n"
     "#endif\n"
     "\n"
         "#endif\n")
endfunction()

if(DEFINED MODEL_C OR DEFINED USER_TYPES_H OR DEFINED OUTPUT_H)
    if(NOT DEFINED MODEL_C OR NOT DEFINED USER_TYPES_H OR NOT DEFINED OUTPUT_H)
        message(FATAL_ERROR "MODEL_C, USER_TYPES_H, and OUTPUT_H must be specified together")
    endif()

    generate_model_macros("${MODEL_C}" "${USER_TYPES_H}" "${OUTPUT_H}")
endif()
