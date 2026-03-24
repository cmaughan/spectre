add_library(draxul-app-support INTERFACE)

target_link_libraries(draxul-app-support INTERFACE
    draxul-config
    draxul-runtime-support
    draxul-render-test
)
