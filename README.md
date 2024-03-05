### For on-hands learning, with no third-party lib dependency

For all kind of images, we treat the data as two parts. ```format meta``` and ```raw coded data```.
The format meta typically is a file header or mixed with raw coded data in certain structure.
The raw coded data is usualy data that has been quantized, transformed, and entropy coded.

There is no secret in all the image compression tech. With knowledge about the above two parts, we can decode a image all by ourself.

We should learn the picture format following the order:
```
bmp -> png -> jpeg -> webp -> heif
```
See the tutorial on [Wiki](https://github.com/junka/ffpic/wiki)

If you would like to know gif/pnm/tiff/tga, this could be learned before jpeg.

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
| HEIF  |    y     |         heif/hevc image          |

[Format reference](http://www.martinreddy.net/gfx/2d-hi.html)

### display rgb data
Tips on display:
After decoding, usually we get rgb data from least significant byte, But for sdl display, the data need to be  ```BGRA``` from least significant byte. So, a reorder is need for display.

Or we can use YUV data to display directly via sdl api.

### Samples

take bmp, gif, etc samples from [filesamples](https://filesamples.com/categories/image)
