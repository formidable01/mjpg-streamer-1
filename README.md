## mjpg-streamer

mjpg-streamer takes JPGs from Linux-UVC compatible webcams, filesystem or other input plugins and streams them as M-JPEG via HTTP (and now websockets!) to webbrowsers, VLC and other software.

### OpenROV Fork
This is a fork of the mjpg-streamer project, maintained by OpenROV and used primarily in our underwater drone platform for streaming live video to the browser. 
Some plugins and functionality has been trimmed out. Not all plugins are guaranteed to work as we maintain only the subset that we employ in our codebase.

### Usage

When launching mjpg-streamer, you specify one or more input plugins and an output plugin. For example, to stream a V4L compatible webcam via an HTTP server (the most common use case), you
can do something like this:

	mjpg_streamer -i input_uvc.so -o output_http.so

Each plugin supports various options, you can view the plugin's options via its `--help` option:

	mjpg_streamer -i 'input_uvc.so --help'

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
* output_ws

