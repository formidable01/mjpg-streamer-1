MJPG_STREAMER_PLUGIN_OPTION(output_ws "Websocket output plugin")

include_directories( "/usr/lib" "/usr/local/lib" )
link_directories( "/usr/include" "/usr/local/include" )
set(CMAKE_CXX_FLAGS "-std=c++11")
MJPG_STREAMER_PLUGIN_COMPILE(output_ws output_ws.cpp)
target_link_libraries(output_ws uWS uv z pthread)    
