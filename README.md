

### FFPic Layers

	-----------------------
	|   file format        |
	-----------------------
	|   decode/encode      |
	-----------------------
	|   dislay buffer      |
	-----------------------

- first layer for file operations like load/free/info callback.
this will include format probe 

- second layer for data decoding and encoding.

- third layer for real buffer maniplate. for now, choose sdl2 as a frame for dispaly

#### File Format
supported file formart is:
| Name | Decoding| Comments |
|:----:|:-------:|:--------:|
| GIF | y | Graphaic Interchange Format |
| PNG | y | Portable Network Graphics|
| BMP |	y |	Microsoft BMP image |
| PBM | y | Portable BitMap image |
| PGM | y | Portable GrayMap image |
| PPM | y | Portable PixelMap image |
| PAM | * | Portable Arbitrary image |

### display rgb data
Tips on display:
after decoding, usually we get rgb data from least significant byte, But
for sdl display, the data need to be  ```BGRA``` from least significant byte.

So, a reorder is need for display.

### Samples

take bmp/gif samples from [https://filesamples.com/formats/$formt] format is bmp/png etc
