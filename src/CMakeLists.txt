FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources})

target_add_binary_data(${COMPONENT_TARGET} "GillSans24.vlw" BINARY)
target_add_binary_data(${COMPONENT_TARGET} "icomoon24.vlw" BINARY)
target_add_binary_data(${COMPONENT_TARGET} "icomoon31.vlw" BINARY)
target_add_binary_data(${COMPONENT_TARGET} "index.html" TEXT)
