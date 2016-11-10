## mjpg-streamer

mjpg-streamer takes JPGs from Linux-UVC compatible webcams, filesystem or other input plugins and streams them as M-JPEG via HTTP (and now websockets!) to webbrowsers, VLC and other software.

### OpenROV Fork
This is a fork of the mjpg-streamer project, maintained by OpenROV and used primarily in our underwater drone platform for streaming live video to the browser. 
Some plugins and functionality has been trimmed out. Not all plugins are guaranteed to work as we maintain only the subset that we employ in our codebase.

### Build Instructions

Standard cmake build process:

```sh
mkdir build
cd build
cmake ..
make
make install
```

### Dependencies

#### build
- CMake 3.x
- GCC (version supports C++11 and onwards)

#### input_uvc
- v4l2 (optional)
- libjpeg (optional)

#### output_ws
- uWebSockets
- libuv 1.3+
- OpenSSL 1.0.x
- zLib 1.x

### Usage

When launching mjpg-streamer, you specify one or more input plugins and an output plugin. For example, to stream a V4L compatible webcam via a websocket server, you can do something like this:

```sh
mjpg_streamer -i "input_uvc.so -d /dev/video0 -r 1280x720 -f 30" -o "output_ws.so --port 8200"
```

Each plugin supports various options, you can view the plugin's options via its `--help` option:

```sh
mjpg_streamer -i 'input_uvc.so --help'
```

### Plugin documentation

Input plugins:

* input_file
* input_http
* input_opencv ([documentation](plugins/input_opencv/README.md))
* input_raspicam ([documentation](plugins/input_raspicam/README.md))
* input_uvc ([documentation](plugins/input_uvc/README.md))

Output plugins:

* output_file
* output_http ([documentation](plugins/output_http/README.md))
* output_rtsp
* output_udp
* output_ws (Uses uWebSockets: https://github.com/uWebSockets/uWebSockets)

