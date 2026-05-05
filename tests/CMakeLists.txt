include(FetchContent)

FetchContent_Declare(
    Unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.1
)
FetchContent_MakeAvailable(Unity)

add_executable(test_pulse test_pulse.c)
target_link_libraries(test_pulse PRIVATE familib unity)
add_test(NAME pulse COMMAND test_pulse)
