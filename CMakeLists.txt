# -------- clang-format 自动格式化 --------
find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
	file(GLOB_RECURSE ALL_SOURCE_FILES
		"${CMAKE_SOURCE_DIR}/src/*.c"
		"${CMAKE_SOURCE_DIR}/src/*.h"
		"${CMAKE_SOURCE_DIR}/example/**/*.c"
		"${CMAKE_SOURCE_DIR}/example/**/*.h"
	)
	# 排除 cJSON
	list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "/cJSON\\.c$")
	list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "/cJSON\\.h$")
	add_custom_target(format
		COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${ALL_SOURCE_FILES}
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		COMMENT "Running clang-format on all source files"
	)
	# 每次构建前自动格式化
	add_custom_target(autoformat ALL DEPENDS format)
endif()

cmake_minimum_required(VERSION 3.10)

project(mjsonrpc)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output)

add_subdirectory(example/batch_invoke)
add_subdirectory(example/custom_error_info)
add_subdirectory(example/del_funcs)
add_subdirectory(example/invoke_a_func)
add_subdirectory(example/rpc_with_params)
add_subdirectory(example/notif_req)

# 添加CTest支持
enable_testing()

# 注册example下的可执行文件为ctest测试
add_test(NAME batch_invoke COMMAND mjsonrpc-example-batch-invoke)
add_test(NAME custom_error_info COMMAND mjsonrpc-example-custom-error-info)
add_test(NAME del_funcs COMMAND mjsonrpc-example-del-funcs)
add_test(NAME invoke_a_func COMMAND mjsonrpc-example-invoke-a-func)
add_test(NAME rpc_with_params COMMAND mjsonrpc-example-rpc-with-params)
add_test(NAME notify_example COMMAND mjsonrpc-example-notif-req)
