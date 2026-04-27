project(uos-ai-ocr-process)

# 查找必要的包
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui)

# 查找 OCR 包
find_package(PkgConfig REQUIRED)
pkg_check_modules(dtkocr QUIET IMPORTED_TARGET dtk6ocr)
if (NOT dtkocr_FOUND)
    pkg_check_modules(dtkocr REQUIRED IMPORTED_TARGET dtkocr)
endif()

# 源文件
set(OCR_SOURCES
    ocr_process.cpp
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${OCR_SOURCES})

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${dtkocr_INCLUDE_DIRS}
)

# 链接库
target_link_libraries(${PROJECT_NAME}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    PkgConfig::dtkocr
)

# 安装可执行文件
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/uos-ai-assistant)
