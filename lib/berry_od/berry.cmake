# Berry-VM + od_*-Bindings (lib/berry_od) in das Zephyr-app-Target
# einbauen. Erwartet REPO_ROOT (Monorepo-Wurzel); definiert CANBOSS_BERRY
# fuer #ifdef-Gates im App-Code.
#
#   include(${REPO_ROOT}/lib/berry_od/berry.cmake)

set(BERRY_DIR ${REPO_ROOT}/modules/berry)
set(BERRY_OD_DIR ${REPO_ROOT}/lib/berry_od)
set(BERRY_GEN ${BERRY_DIR}/generate)

file(GLOB berry_core_sources ${BERRY_DIR}/src/*.c)

# Konstanten-Tabellen der Berry-VM generieren (tools/coc, Python).
# Ergebnis liegt wie beim Berry-Makefile in <berry>/generate/.
add_custom_command(
    OUTPUT ${BERRY_GEN}/be_const_strtab.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BERRY_GEN}
    COMMAND ${PYTHON_EXECUTABLE} ${BERRY_DIR}/tools/coc/coc
            ${BERRY_DIR}/src ${BERRY_DIR}/default
            -o ${BERRY_GEN}
            -c ${BERRY_OD_DIR}/berry_conf.h
    DEPENDS ${BERRY_OD_DIR}/berry_conf.h ${berry_core_sources}
    WORKING_DIRECTORY ${BERRY_DIR}
    COMMENT "Berry: Konstanten-Tabellen generieren (tools/coc)"
)
add_custom_target(berry_gen DEPENDS ${BERRY_GEN}/be_const_strtab.h)
add_dependencies(app berry_gen)

target_sources(app PRIVATE
    ${BERRY_OD_DIR}/canboss_berry.c
    ${BERRY_OD_DIR}/berry_port.c
    ${BERRY_OD_DIR}/berry_repl.c
    ${berry_core_sources}
    ${BERRY_DIR}/default/be_modtab.c
)

# Reihenfolge wichtig: zuerst unsere berry_conf.h, dann berry/src
target_include_directories(app BEFORE PRIVATE ${BERRY_OD_DIR})
target_include_directories(app PRIVATE ${BERRY_DIR}/src)

target_compile_definitions(app PRIVATE CANBOSS_BERRY=1)
