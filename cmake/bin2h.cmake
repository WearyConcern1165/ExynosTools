file(READ "${INPUT_FILE}" HEX_DATA HEX)
string(REGEX MATCHALL "........" HEX_WORDS "${HEX_DATA}")

set(OUT_CONTENT "#pragma once\n")
set(OUT_CONTENT "${OUT_CONTENT}#include <cstdint>\n")
set(OUT_CONTENT "${OUT_CONTENT}namespace bcn { namespace spirv {\n")
set(OUT_CONTENT "${OUT_CONTENT}const uint32_t ${VAR_NAME}[] = {\n")

set(COUNTER 0)
foreach(WORD IN LISTS HEX_WORDS)
    # Convertir el orden de bytes debido al endianness (SPIR-V es little-endian en 32 bits)
    string(SUBSTRING ${WORD} 6 2 BYTE0)
    string(SUBSTRING ${WORD} 4 2 BYTE1)
    string(SUBSTRING ${WORD} 2 2 BYTE2)
    string(SUBSTRING ${WORD} 0 2 BYTE3)
    
    set(OUT_CONTENT "${OUT_CONTENT}0x${BYTE0}${BYTE1}${BYTE2}${BYTE3}, ")
    
    math(EXPR COUNTER "${COUNTER} + 1")
    if(COUNTER EQUAL 8)
        set(OUT_CONTENT "${OUT_CONTENT}\n    ")
        set(COUNTER 0)
    endif()
endforeach()

set(OUT_CONTENT "${OUT_CONTENT}\n}\;\n")
set(OUT_CONTENT "${OUT_CONTENT}const size_t ${VAR_NAME}_SIZE = sizeof(${VAR_NAME})\;\n")
set(OUT_CONTENT "${OUT_CONTENT}} } // namespace bcn::spirv\n")

file(WRITE ${OUTPUT_FILE} ${OUT_CONTENT})
