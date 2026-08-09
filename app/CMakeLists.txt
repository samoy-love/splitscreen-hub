cmake_minimum_required(VERSION 3.10)

set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/library)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(CouchCoop)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_ALTER "0")
set(PROJECT_AUTHOR "couch-coop")
set(PROJECT_TITLEID "0x010FF000CC000000")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.jpg)

# Ресурсы приложения и ресурсы самой borealis (шрифты, i18n, служебные xml)
# кладутся в один romfs.
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(BOREALIS_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/resources)

add_subdirectory(${BOREALIS_LIBRARY} borealis)

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

# SQLite собирается из amalgamation: порта switch-sqlite3 в devkitPro нет,
# а один .c подключается проще, чем внешняя зависимость.
set(SQLITE_SRC lib/sqlite/sqlite3.c)
if (PLATFORM_SWITCH)
    list(APPEND SQLITE_SRC lib/sqlite/nx_compat.c)
endif ()
add_library(sqlite3 STATIC ${SQLITE_SRC})
target_include_directories(sqlite3 PUBLIC lib/sqlite)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5=1
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED=1
    # в libnx нет sys/mman.h, а база лежит в romfs только на чтение —
    # ни mmap, ни журнал WAL нам не нужны
    SQLITE_MAX_MMAP_SIZE=0
    SQLITE_OMIT_WAL=1
)

file(GLOB_RECURSE MAIN_SRC source/*.cpp)
list(APPEND APP_PLATFORM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/source)

if (PLATFORM_SWITCH)
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
endif ()

program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_include_directories(${PROJECT_NAME} PRIVATE ${APP_PLATFORM_INCLUDE})
target_link_libraries(${PROJECT_NAME} PRIVATE borealis sqlite3 ${APP_PLATFORM_LIB})

if (PLATFORM_SWITCH)
    # скриншоты качаются по сети: обложки лежат в romfs, а 20 тысяч скриншотов
    # туда не влезут
    target_link_libraries(${PROJECT_NAME} PRIVATE
        curl mbedtls mbedx509 mbedcrypto)

    # Трейлеры: libmpv для devkitPro в pacman нет (только сборка из исходников
    # по рецептам nxmp/SwitchWave), поэтому демукс/декод собственный поверх
    # FFmpeg.
    #
    # FFmpeg берём свой, урезанный (build_ffmpeg_slim.sh), а не пакетный:
    # switch-ffmpeg собран «на всё», его реестр кодеков ссылается на каждый
    # декодер, и линкер тянет их целиком. Трейлеры Cloudinary — это ровно
    # h264 + aac в mp4, поэтому своя сборка ужимает libavcodec с 17.1 до
    # 2.6 МБ, а libavformat с 3.9 до 0.9 МБ. Аппаратное декодирование
    # (nvtegra из патча devkitPro) сохранено, dav1d больше не нужен — AV1
    # отключён.
    #
    # z/bz2 нужны статическим либам ffmpeg и требуются линкеру после
    # avformat/avcodec, а не перед, иначе символы не резолвятся.
    set(FFMPEG_SLIM ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-slim)
    if (NOT EXISTS ${FFMPEG_SLIM}/lib/libavcodec.a)
        message(FATAL_ERROR "нет ${FFMPEG_SLIM} — запустите build_ffmpeg_slim.sh")
    endif ()

    target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_SLIM}/include)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${FFMPEG_SLIM}/lib/libavformat.a
        ${FFMPEG_SLIM}/lib/libavcodec.a
        ${FFMPEG_SLIM}/lib/libswscale.a
        ${FFMPEG_SLIM}/lib/libswresample.a
        ${FFMPEG_SLIM}/lib/libavutil.a
        SDL2
        z bz2)
endif ()

if (PLATFORM_SWITCH)
    add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create "Couch co-op" "${PROJECT_AUTHOR}"
                "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
                --titleid=${PROJECT_TITLEID}
        # из ресурсов borealis нужны только переводы её собственных строк,
        # иконки материала и служебные icon — демо-картинки в релиз не тащим,
        # а шрифт на Switch берётся системный
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/i18n ${CMAKE_BINARY_DIR}/resources/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/material ${CMAKE_BINARY_DIR}/resources/material
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/icon ${CMAKE_BINARY_DIR}/resources/icon
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro
                --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp
                --romfsdir=${CMAKE_BINARY_DIR}/resources
    )
else ()
    # отладочная сборка на десктопе: ресурсы просто кладём рядом с бинарником
    add_custom_target(${PROJECT_NAME}.data
        # из ресурсов borealis нужны только переводы её собственных строк,
        # иконки материала и служебные icon — демо-картинки в релиз не тащим,
        # а шрифт на Switch берётся системный
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/i18n ${CMAKE_BINARY_DIR}/resources/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/material ${CMAKE_BINARY_DIR}/resources/material
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/icon ${CMAKE_BINARY_DIR}/resources/icon
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
    )
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data)
endif ()
