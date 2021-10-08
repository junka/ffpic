

# FFPic Layers

	-----------------------
	|   file format        |
	-----------------------
	|   decode/encode      |
	-----------------------
	|   dislay buffer      |
	-----------------------

- first layer for file operations like open/read/write/close/callback.
this will include format probe 

- second layer for data decoding and encoding.

- third layer for real buffer maniplate. for now, choose sdl2 as a frame for dispaly


# ffpic
