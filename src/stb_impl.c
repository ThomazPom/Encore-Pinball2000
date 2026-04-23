/* stb single-header implementations — compiled once */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
/* No HDR / no GIF — keeps the binary smaller. */
#define STBI_NO_HDR
#define STBI_NO_GIF
#include "stb_image.h"
