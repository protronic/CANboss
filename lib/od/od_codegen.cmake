# OD-Codegen zur Buildzeit: erzeugt die CANopenNode-v4-Dateien
# (<name>.c/.h) aus eds/<name>.eds mit dem EDSSharp-CLI aus dem
# CANopenEditor (github.com/protronic/CANopenEditor, Asset
# EDSSharp-linux-x64.tar.gz).
#
# Fehlt das Tool, laedt CMake auf Linux das aktuelle Release:
#   https://github.com/protronic/CANopenEditor/releases/latest/download/EDSSharp-linux-x64.tar.gz
# (tools/get-edssharp.sh). Abschalten: -DCANBOSS_FETCH_EDSSHARP=OFF
#
# Toolsuche (erste Fundstelle gewinnt):
#   1. -DEDSSHARP=<pfad> bzw. Umgebungsvariable EDSSHARP
#   2. <repo>/tools/edssharp/EDSSharp
#   3. EDSSharp im PATH
#
# Mit Tool wird das OD bei jeder Aenderung der EDS-Datei neu in den
# Build-Baum generiert; ohne Tool bauen die eingecheckten Dateien aus
# lib/od/<name>/ (Fallback, z.B. ohne Netz oder nicht-Linux).
#
# Verwendung (nach find_package(Zephyr), REPO_ROOT gesetzt):
#   include(${REPO_ROOT}/lib/od/od_codegen.cmake)
#   canboss_od_add(canboss_master)

option(CANBOSS_FETCH_EDSSHARP
       "Neues EDSSharp-CLI von GitHub laden, falls nicht vorhanden" ON)

if(NOT EDSSHARP AND DEFINED ENV{EDSSHARP})
    set(EDSSHARP "$ENV{EDSSHARP}")
endif()
if(NOT EDSSHARP)
    find_program(EDSSHARP NAMES EDSSharp HINTS ${REPO_ROOT}/tools/edssharp)
endif()

if(NOT EDSSHARP AND CANBOSS_FETCH_EDSSHARP
   AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "CANboss: lade EDSSharp-CLI (latest linux-x64)")
    execute_process(
        COMMAND sh ${REPO_ROOT}/tools/get-edssharp.sh
        RESULT_VARIABLE _edssharp_fetch
        OUTPUT_VARIABLE _edssharp_out
        ERROR_VARIABLE _edssharp_err
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(_edssharp_out)
        message(STATUS "${_edssharp_out}")
    endif()
    if(_edssharp_err)
        message(STATUS "${_edssharp_err}")
    endif()
    if(NOT _edssharp_fetch EQUAL 0)
        message(STATUS "CANboss: EDSSharp-Download fehlgeschlagen (${_edssharp_fetch})")
    endif()
    unset(EDSSHARP CACHE)
    find_program(EDSSHARP NAMES EDSSharp HINTS ${REPO_ROOT}/tools/edssharp)
endif()

if(EDSSHARP)
    message(STATUS "CANboss: OD-Codegen aus eds/*.eds mit ${EDSSHARP}")
else()
    message(STATUS "CANboss: EDSSharp nicht gefunden - eingecheckte ODs aus "
                   "lib/od/ (Regenerieren: tools/get-edssharp.sh oder EDSSHARP=<pfad>)")
endif()

function(canboss_od_add name)
    if(EDSSHARP)
        set(gen_dir ${CMAKE_BINARY_DIR}/od_gen/${name})
        add_custom_command(
            OUTPUT ${gen_dir}/${name}.c ${gen_dir}/${name}.h
            BYPRODUCTS ${gen_dir}/${name}.json
            COMMAND ${EDSSHARP} --export-project
                    --infile ${REPO_ROOT}/eds/${name}.eds
                    --outdir ${gen_dir} --od ${name} --odname ${name}
                    --canopennode v4
            DEPENDS ${REPO_ROOT}/eds/${name}.eds
            COMMENT "EDSSharp: eds/${name}.eds -> ${name}.c/.h"
        )
        target_sources(app PRIVATE ${gen_dir}/${name}.c)
        target_include_directories(app PRIVATE ${gen_dir})
    else()
        target_sources(app PRIVATE ${REPO_ROOT}/lib/od/${name}/${name}.c)
        target_include_directories(app PRIVATE ${REPO_ROOT}/lib/od/${name})
    endif()
endfunction()
