project(unit_test)

aux_source_directory(. src_files)

add_executable(${PROJECT_NAME} ${src_files}) 

if (MSVC)
    target_compile_options(${PROJECT_NAME}
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
    )

    target_link_libraries(${PROJECT_NAME} hashtable zookeeper gtest-lib ws2_32)
else ()
    target_link_libraries(${PROJECT_NAME} hashtable zookeeper gtest-lib -static-libgcc -static-libstdc++ dl pthread)
endif ()