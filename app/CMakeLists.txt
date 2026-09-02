cmake_minimum_required(VERSION 3.10)

set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/library)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(SplitScreenHub)
set(VERSION_MAJOR "1")
set(VERSION_MINOR "1")
set(VERSION_ALTER "4")
set(PROJECT_AUTHOR "splitscreen-hub")
set(PROJECT_TITLEID "0x010FF000CC000000")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.jpg)

# Ресурсы приложения и ресурсы самой borealis (шрифты, i18n, служебные xml)
# кладутся в один romfs.
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(BOREALIS_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/resources)

add_subdirectory(${BOREALIS_LIBRARY} borealis)

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

file(GLOB_RECURSE MAIN_SRC source/*.cpp)
list(APPEND APP_PLATFORM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/source)

if (PLATFORM_SWITCH)
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
endif ()

program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
# Та же версия, что уезжает в nacp: по ней приложение решает, обновляться ли.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}")

target_include_directories(${PROJECT_NAME} PRIVATE ${APP_PLATFORM_INCLUDE})
# zlib: записи карточек в details.bin сжаты сырым deflate с общим словарём.
# Лежит в portlibs devkitPro, отдельной сборки не требует.
target_link_libraries(${PROJECT_NAME} PRIVATE borealis z ${APP_PLATFORM_LIB})

if (PLATFORM_SWITCH)
    # скриншоты качаются по сети: обложки лежат в romfs, а 20 тысяч скриншотов
    # туда не влезут
    target_link_libraries(${PROJECT_NAME} PRIVATE
        curl mbedtls mbedx509 mbedcrypto)

    # Трейлеры: libmpv для devkitPro в pacman нет (только сборка из исходников
    # по рецептам nxmp/SwitchWave), поэтому демукс/декод собственный поверх
    # FFmpeg.
    #
    # FFmpeg берём свой, урезанный (tools/build_ffmpeg_slim.sh), а не пакетный:
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
        message(FATAL_ERROR "нет ${FFMPEG_SLIM} — запустите tools/build_ffmpeg_slim.sh")
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

# Ресурсы romfs (или папки рядом с бинарником на десктопе). Из ресурсов
# borealis нужны только переводы её строк, иконки материала, служебные icon и
# img/sys — индикаторы Wi-Fi и батареи в шапке и курсор для тача; без них
# borealis на каждом открытии экрана пишет «Cannot set texture: 0». Демо-картинки
# (1.8 МБ) не берём. Своего шрифта в romfs нет: текст рисуется системным
# шрифтом консоли, который приложение берёт у неё в рантайме
# (source/ui/fonts.cpp) — иначе borealis подставляет китайский системный, и
# кириллица с цифрами рисуются полноширинными формами.
#
# Каталог romfs перед копированием вычищается: copy_directory только
# добавляет, и файл, удалённый из resources/, иначе жил бы в build/ и уезжал
# в каждый следующий .nro — так уже пережил свою отмену шрифт font.ttf.
set(COPY_RESOURCES
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/i18n ${CMAKE_BINARY_DIR}/resources/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/material ${CMAKE_BINARY_DIR}/resources/material
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/icon ${CMAKE_BINARY_DIR}/resources/icon
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/img/sys ${CMAKE_BINARY_DIR}/resources/img/sys
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
)

if (PLATFORM_SWITCH)
    # ALL обязателен: без него обычный `cmake --build .` собирает только .elf и
    # молча оставляет рядом .nro от прошлой сборки.
    add_custom_target(${PROJECT_NAME}.nro ALL DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create "SplitScreen Hub" "${PROJECT_AUTHOR}"
                "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
                --titleid=${PROJECT_TITLEID}
        ${COPY_RESOURCES}
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro
                --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp
                --romfsdir=${CMAKE_BINARY_DIR}/resources
    )
else ()
    add_custom_target(${PROJECT_NAME}.data ${COPY_RESOURCES})
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data)
endif ()
