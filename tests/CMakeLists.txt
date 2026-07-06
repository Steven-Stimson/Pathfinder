add_executable(PathfinderTests bandagetests.cpp)
add_test(NAME PathfinderTests COMMAND PathfinderTests)

target_link_libraries(PathfinderTests PRIVATE PathfinderCLI PathfinderLib OGDF Qt6::Widgets Qt6::Test CLI11::CLI11)
