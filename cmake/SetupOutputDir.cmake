# 1. 检测操作系统
if(WIN32)
    set(OS_NAME "windows")
elseif(ANDROID)
    set(OS_NAME "android")
elseif(UNIX AND NOT APPLE)  # Linux
    set(OS_NAME "linux")
else()
    set(OS_NAME "unknown")
endif()

# 2. 检测架构（32/64位）
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_NAME "x64")
    set(ARCH_DIR "x64")
else()
    set(ARCH_NAME "x86")
    set(ARCH_DIR "x86")
endif()

# 3. 处理 Android 特殊架构（ABI）
if(ANDROID)
    if(ANDROID_ABI)
        # 使用 Android 工具链提供的 ABI 信息
        set(ARCH_NAME "${ANDROID_ABI}")
        set(ARCH_DIR "${ANDROID_ABI}")
    else()
        # 默认处理
        if(CMAKE_ANDROID_ARCH_ABI)
            set(ARCH_NAME "${CMAKE_ANDROID_ARCH_ABI}")
            set(ARCH_DIR "${CMAKE_ANDROID_ARCH_ABI}")
        endif()
    endif()
endif()

# 4. 获取当前构建类型（Debug/Release等）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")  # 默认构建类型
endif()
string(TOLOWER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_LOWER)

# 5. 创建基础输出路径
set(BASE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output/${OS_NAME}/${ARCH_DIR}/${BUILD_TYPE_LOWER}")

# 6. 设置全局输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${BASE_OUTPUT_DIR}/lib")   # 静态库
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BASE_OUTPUT_DIR}/lib")   # 共享库
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BASE_OUTPUT_DIR}/bin")   # 可执行文件

# 7. 确保目录存在
file(MAKE_DIRECTORY ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
file(MAKE_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# 8. 多配置生成器支持（Visual Studio, Xcode等）
foreach(CONFIG IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER ${CONFIG} CONFIG_UPPER)
    string(TOLOWER ${CONFIG} CONFIG_LOWER)

    # 为每个配置创建独立目录
    set(CONFIG_DIR "${CMAKE_SOURCE_DIR}/output/${OS_NAME}/${ARCH_DIR}/${CONFIG_LOWER}")

    # 设置每个配置的输出目录
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CONFIG_DIR}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CONFIG_DIR}/lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CONFIG_DIR}/bin")

    # 确保目录存在
    file(MAKE_DIRECTORY ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER}})
    file(MAKE_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER}})
    file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER}})
endforeach()

# 9. 打印最终输出路径
message(STATUS "Output directory structure:")
message(STATUS "  OS: ${OS_NAME}")
message(STATUS "  Arch: ${ARCH_NAME}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Libraries: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "  Binaries: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")