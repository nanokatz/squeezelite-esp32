#pragma once

#include <stdint.h>
#include "esp_err.h"

// no progressive JPEG handling

struct GDS_Device;

enum { GDS_RGB565, GDS_RGB555, GDS_RGB444, GDS_RGB332, GDS_GRAYSCALE };

// Fit options for GDS_DrawJPEG
#define GDS_IMAGE_LEFT		0x00
#define GDS_IMAGE_CENTER_X	0x01
#define GDS_IMAGE_RIGHT		0x04
#define GDS_IMAGE_TOP		0x00
#define GDS_IMAGE_BOTTOM	0x08
#define GDS_IMAGE_CENTER_Y	0x02
#define GDS_IMAGE_CENTER	(GDS_IMAGE_CENTER_X | GDS_IMAGE_CENTER_Y)
#define GDS_IMAGE_FIT		0x10	// re-scale by a factor of 2^N (up to 3)

// Width and Height can be NULL if you already know them (actual scaling is closest ^2)
uint16_t* 	GDS_DecodeJPEG(uint8_t *Source, int *Width, int *Height, float Scale);
void	 	GDS_GetJPEGSize(uint8_t *Source, int *Width, int *Height);
bool 		GDS_DrawJPEG( struct GDS_Device* Device, uint8_t *Source, int x, int y, int Fit);
void 		GDS_DrawRGB16( struct GDS_Device* Device, uint16_t *Image, int x, int y, int Width, int Height, int RGB_Mode );
void 		GDS_DrawRGB8( struct GDS_Device* Device, uint8_t *Image, int x, int y, int Width, int Height, int RGB_Mode );