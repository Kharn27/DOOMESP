set(DOOM_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../linuxdoom-1.10"
)

file(GLOB DOOM_SOURCES CONFIGURE_DEPENDS "${DOOM_SOURCE_DIR}/*.c")

list(REMOVE_ITEM DOOM_SOURCES
    "${DOOM_SOURCE_DIR}/i_main.c"
    "${DOOM_SOURCE_DIR}/i_net.c"
    "${DOOM_SOURCE_DIR}/i_sound.c"
    "${DOOM_SOURCE_DIR}/i_video.c"
)

idf_component_register(
    SRCS ${DOOM_SOURCES}
    INCLUDE_DIRS "${DOOM_SOURCE_DIR}"
    PRIV_REQUIRES esp_common
)

# LinuxDoom embeds historical RCS strings in every translation unit. Modern
# GCC versions diagnose those unused constants under ESP-IDF's warning policy.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-const-variable
)
