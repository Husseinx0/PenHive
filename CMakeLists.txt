# إصدار CMake المطلوب (حديث)
cmake_minimum_required(VERSION 3.14)

# اسم المشروع
project(MyProject LANGUAGES CXX)

# إعداد خيارات البناء (مثلاً: Release/Debug)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# تفعيل التجميع في مجلد خارجي (out-of-source build)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)

# تفعيل التحذيرات والتحسينات
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# تضمين المجلدات الفرعية (الموديولات)
add_subdirectory(lib)
add_subdirectory(src)
add_subdirectory(tests)