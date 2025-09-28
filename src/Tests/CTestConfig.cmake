# تعريف اسم المشروع (يجب أن يتطابق مع project() في CMakeLists.txt)
set(CTEST_PROJECT_NAME "MyProject")

# تعريف اسم الإصدار (يمكنك استخدام إصدار Git أو رقم بناء)
set(CTEST_NIGHTLY_START_TIME "02:00:00 UTC")

# تعريف موقع الخادم الذي سترسل إليه التقارير (إذا كنت تستخدم CDash)
# (استخدم https://cdash.org/ كخادم مجاني)
set(CTEST_DROP_METHOD "http")
set(CTEST_DROP_SITE "my-cdash-server.com")
set(CTEST_DROP_LOCATION "/submit.php?project=MyProject")
set(CTEST_DROP_SITE_CDASH TRUE)

# (اختياري) إذا كنت لا تستخدم خادمًا، يمكنك حذف الأسطر فوق
# لكن احتفظ بالسطر التالي لتجنب التحذيرات:
set(CTEST_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set(CTEST_BINARY_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")