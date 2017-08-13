mjpg-streamer input plugin: input_http
======================================

This plugin provides JPEG data from HTTP MJPEG webcams.

Usage
=====

    mjpg_streamer [input plugin options] -i 'input_http.so [options]'

```
---------------------------------------------------------------
The following parameters can be passed to this plugin:

[-v | --version ]........: current SVN Revision
[-h | --help]............: show this message
[-H | --host]............: select host to data from, localhost is default
[-p | --port]............: port, defaults to 8080
[-u | --path]............: path, defaults to /?action=stream
---------------------------------------------------------------
```

Upon startup, this plugin will make an HTTP 1.0 GET request to `http://<host>:<port><path>`.  It will then parse the response headers, extracting the MIME multipart delimiter string after `boundary=`.  Each time a boundary delimiter is encountered in the TCP stream, the preceding image file will be copied and sent to the appropriate output plugin.

