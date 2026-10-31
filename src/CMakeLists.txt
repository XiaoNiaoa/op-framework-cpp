target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/component.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/pointers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/mysql.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/log/log.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/hash/hash.cpp"
    
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/utils/command.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/utils/dialog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/utils/kick.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/account/account.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/account/example.cpp"
    
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/email/email.cpp"
)

add_subdirectory(core/mysql)
