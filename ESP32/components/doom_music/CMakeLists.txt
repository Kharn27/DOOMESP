idf_component_register(
    SRCS
        "musplayer.c"
        "woody_opl.c"
    INCLUDE_DIRS "."
)

# The OPL emulator runs for every audio sample. Optimising this component for
# speed keeps it from stealing frame time from DOOM's renderer. LittleMUS uses
# both OPL register banks, so build Woody-OPL in OPL3 mode.
target_compile_definitions(${COMPONENT_LIB} PRIVATE OPLTYPE_IS_OPL3=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fsingle-precision-constant)
