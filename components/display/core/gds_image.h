#pragma once

#include <stdint.h>
#include "esp_err.h"

// no progressive JPEG handling

struct GDS_Device;

enum { GDS_RGB565, GDS_RGB555, GDS_RGB444 };

#define GDS_IMAGE_TOP		0x00
#define GDS_IMAGE_CENTER_X	0x01
#define GDS_IMAGE_CENTER_Y	0x02
#define GDS_IMAGE_CENTER	(GDS_IMAGE_CENTER_X | GDS_IMAGE_CENTER_Y)
#define GDS_IMAGE_FIT		0x10

// Width and Height can be NULL if you already know them (actual scaling is closest ^2)
uint16_t* 	GDS_DecodeJPEG(uint8_t *Source, int *Width, int *Height, float Scale);
void	 	GDS_GetJPEGSize(uint8_t *Source, int *Width, int *Height);
void 		GDS_DrawRGB16( struct GDS_Device* Device, uint16_t *Image, int x, int y, int Width, int Height, int RGB_Mode );
// set DisplayWidth and DisplayHeight to non-zero if you want autoscale to closest factor ^2 from 0..3
bool 		GDS_DrawJPEG( struct GDS_Device* Device, uint8_t *Source, int x, int y, int Fit);