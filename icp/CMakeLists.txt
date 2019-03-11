add_contract(icp icp ${CMAKE_CURRENT_SOURCE_DIR}/icp.cpp)
target_include_directories(icp.wasm
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(icp.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
