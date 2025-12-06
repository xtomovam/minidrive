add_executable(minidrive_server
    src/main.cpp
    src/simple_server.cpp
    src/session.cpp
    src/flows/flow.cpp
    src/flows/authenticate_flow.cpp
    src/flows/upload_flow.cpp
    src/access_control.cpp
    src/flows/upload_resume_flow.cpp
)

target_include_directories(minidrive_server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(minidrive_server
    PRIVATE
        minidrive_shared
        minidrive_warnings
)

set_target_properties(minidrive_server PROPERTIES OUTPUT_NAME server)

