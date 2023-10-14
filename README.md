### For on-hands learning, with no third-party lib dependency

We should learn the picture format following the order:
```
bmp -> png -> jpeg -> webp
```
if you would like to know gif/pnm/tiff/tga, this could be learned before jpeg.

### FFPic Layers

    -----------------------
    |   file format        |
    -----------------------
    |   decode/            |
    -----------------------
    |   dislay buffer      |
    -----------------------

- first layer for file operations like load/free/info callback.
this will include format probe 

- second layer for data decoding and encoding.

- third layer for real buffer maniplate. for now, choose sdl2 as a frame for dispaly

#### File Format
supported file formart is:
| Name  | Decoding |             Comments             |
| :---: | :------: | :------------------------------: |
|  GIF  |    y     |   Graphaic Interchange Format    |
|  PNG  |    y     |    Portable Network Graphics     |
|  BMP  |    y     |       Microsoft BMP image        |
|  PBM  |    y     |      Portable BitMap image       |
|  PGM  |    y     |      Portable GrayMap image      |
|  PPM  |    y     |     Portable PixelMap image      |
|  PAM  |    y     |     Portable Arbitrary image     |
|  TGA  |    y     | Truevision raster graphic format |
| TIFF  |    y     |      Tag Image File Format       |
|  JPG  |    y     |         jpeg lossy image         |
|  ICO  |    y     |        icon bitmap image         |
|  EXR  |    y     |          OpenEXR image           |
| WEBP  |    y     |         WEBP lossy image         |

[Format reference](http://www.martinreddy.net/gfx/2d-hi.html)

### display rgb data
Tips on display:
after decoding, usually we get rgb data from least significant byte, But
for sdl display, the data need to be  ```BGRA``` from least significant byte.

So, a reorder is need for display.

### Samples

take bmp, gif, etc samples from [filesamples](https://filesamples.com/categories/image)
