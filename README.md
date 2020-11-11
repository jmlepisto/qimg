# Qimg
#### Quick Image Display - Display images in terminal sessions

Stuck in a terminal session and wish to quickly inspect some images? Qimg's got you covered!

Without any windowing systems or external dependencies Qimg can be used to display images on every modern(ish) Linux build.

#### How?
Linux has provided generic gramebuffer support since kernel 2.1.109. The Linux framebuffer (**fbdev**) is an abstraction layer providing 
access to hardware-independent graphics. Userspace access is also supported via */dev/fb** device files.

Qimg takes advantage of Linux framebuffer to simply draw out the images as raw pixels on the screen - no tricks, treats or windowing contexts included.

#### Why?
Mostly, for fun. Though I've sincerely had a few occansions where it would've been nice to inspect some images and the only access to the system was via direct terminal.

#### Cool, how do I use Qimg?
`qimg -h` will teach you the basics. There aren't any fancy features (yet) so basic usage is `qimg [option]... input...`.
- `-b <framebuffer>` selects which frambuffer to use. Defaults to the first one found on the system.
- `-c` will try to hide the terminal cursor and prevent it from refreshing on top of the image.
- `-r` will repaint the image continuously to prevent anything else from refreshing on top of the image.
- `-pos <position>` is used set image position.
- `-bg <color>` is used to set background color.
Please note that Qimg will ***NOT*** work if an Xorg session is active on the current TTY. 

Exits via `SIGINT` or `SIGTERM` will trigger cleanups and restore terminal cursor visibility.

#### How to get Qimg
Building Qimg is easy as everything needed for the build is provided in this repository. 
However, I will be uploading some prebuilt binaries to [releases](https://github.com/jjstoo/qimg/releases).


#### Future plans

- Image centering **done**
- Image resizing 
- Image slideshows **done**


A huge thank you to [stb developers](https://github.com/nothings/stb) for their excellent stb_image library.
