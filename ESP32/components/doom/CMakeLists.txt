file(GLOB DOOM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../../../linuxdoom-1.10/*.c")

list(REMOVE_ITEM DOOM_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../../linuxdoom-1.10/i_main.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../linuxdoom-1.10/i_net.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../linuxdoom-1.10/i_sound.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../linuxdoom-1.10/i_video.c"
)

idf_component_register(
    SRCS ${DOOM_SOURCES}
    INCLUDE_DIRS "../../../linuxdoom-1.10"
    PRIV_REQUIRES esp_common
)

# LinuxDoom embeds historical RCS strings in every translation unit. Modern
# GCC versions diagnose those unused constants under ESP-IDF's warning policy.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-const-variable
)
