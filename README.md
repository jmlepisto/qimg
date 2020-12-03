# Qimg

[![CodeFactor](https://www.codefactor.io/repository/github/jjstoo/qimg/badge/main)](https://www.codefactor.io/repository/github/jjstoo/qimg/overview/main)

[![CMake build](https://github.com/jjstoo/qimg/workflows/CMake/badge.svg)](https://github.com/jjstoo/qimg/actions?query=workflow%3ACMake)

Please see [**generated qimg.c docs**](https://jjstoo.github.io/qimg/html/qimg_8c.html) for detailed usage info and function reference.

#### Quick Image Display - Display images in terminal sessions

Stuck in a terminal session and wish to quickly inspect some images? Qimg's got you covered!

Without any windowing systems or external dependencies Qimg can be used to display images on every modern(ish) Linux build.

#### How?
Linux has provided generic gramebuffer support since kernel 2.1.109. The Linux framebuffer (**fbdev**) is an abstraction layer providing 
access to hardware-independent graphics. Userspace access is also supported via */dev/fb** device files.

Qimg takes advantage of Linux framebuffer to simply draw out the images as raw pixels on the screen - no tricks, treats or windowing contexts included.

#### Why?
Mostly for fun but I've had a few occasions on some terminal-only
systems where it would've been nice to view images and I found most of the
existing solutions too complex and heavyweight for such a simple task.

#### Cool, how do I use Qimg?
`qimg -h` will teach you the basics. Usage is outlined by `qimg [option]... input...`.
- `-b <framebuffer index>` selects which frambuffer to use. Defaults to the first one found on the system.
- `-c` will try to hide the terminal cursor and prevent it from refreshing on top of the image.
- `-r` will repaint the image continuously to prevent anything else from refreshing on top of the image.
- `-d <delay>` will set slideshow delay.
- `-pos <position>` is used set image position.
- `-bg <color>` is used to set background color.
- `-scale <scale style>` is used to set scale style. Useful for scaling images to fullscreen resolution.

Example usage:

`qimg -c -d 2 -pos c -scale fit input.jpg input2.jpg`

This will show the two files as a slideshow on the default framebuffer, scaled to fit the screen and centered. 
`-c` will cause the cursor to be hidden which prevents the terminal of refreshing on top of the images.

Exits via `SIGINT` or `SIGTERM` will trigger cleanups and restore terminal cursor visibility.

Please note that Qimg will ***NOT*** work if an Xorg session or any other framebuffer-overriding services are active on the current TTY. 

#### How to get Qimg
Building Qimg is easy as everything needed for the build is provided in this repository. 
However, I will be uploading some prebuilt binaries to [releases](https://github.com/jjstoo/qimg/releases)
and continuous build artifacts can be downloaded from repository [actions](https://github.com/jjstoo/qimg/actions?query=workflow%3ACMake).


A huge thank you to [stb developers](https://github.com/nothings/stb) for their excellent image libraries.
