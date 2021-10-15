

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



### Samples

take bmp/gif samples from [https://filesamples.com/formats/bmp] etc
