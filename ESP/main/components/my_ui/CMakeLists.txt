# Keep this file stable even when SquareLine regenerates UI sources.
# 1) 使用 filelist.txt 明确指定所有 UI 源文件
# 2) 提供 lvgl 头文件兼容路径

set(UI_COMPAT_DIR "${CMAKE_BINARY_DIR}/ui_compat/my_ui")
set(LVGL_HEADER_SRC "${COMPONENT_DIR}/../../../managed_components/lvgl__lvgl/lvgl.h")

# 读取 filelist.txt 作为源文件列表
file(STRINGS "${COMPONENT_DIR}/filelist.txt" UI_FILELIST)
list(TRANSFORM UI_FILELIST PREPEND "${COMPONENT_DIR}/")
set(UI_SRCS ${UI_FILELIST})

file(MAKE_DIRECTORY "${UI_COMPAT_DIR}/lvgl")
if(EXISTS "${LVGL_HEADER_SRC}")
    configure_file("${LVGL_HEADER_SRC}" "${UI_COMPAT_DIR}/lvgl/lvgl.h" COPYONLY)
else()
    message(FATAL_ERROR "Cannot find lvgl.h: ${LVGL_HEADER_SRC}")
endif()

idf_component_register(
    SRCS ${UI_SRCS}
    INCLUDE_DIRS
        "."
        "${UI_COMPAT_DIR}"
        "../my_screen/inc"
    REQUIRES
        lvgl my_screen
)