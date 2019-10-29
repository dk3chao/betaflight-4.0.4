/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * "Note that the timing on the WS2812/WS2812B LEDs has changed as of batches from WorldSemi
 * manufactured made in October 2013, and timing tolerance for approx 10-30% of parts is very small.
 * Recommendation from WorldSemi is now: 0 = 400ns high/850ns low, and 1 = 850ns high, 400ns low"
 *
 * Currently the timings are 0 = 350ns high/800ns and 1 = 700ns high/650ns low.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_LED_STRIP

#include "build/build_config.h"

#include "common/color.h"
#include "common/colorconversion.h"

#include "drivers/dma.h"
#include "drivers/io.h"

#include "light_ws2811strip.h"

#if defined(STM32F1) || defined(STM32F3)
uint8_t ledStripDMABuffer[WS2811_DMA_BUFFER_SIZE];
#elif defined(STM32F7)
FAST_RAM_ZERO_INIT uint32_t ledStripDMABuffer[WS2811_DMA_BUFFER_SIZE];
#else
uint32_t ledStripDMABuffer[WS2811_DMA_BUFFER_SIZE];
#endif

static ioTag_t ledStripIoTag;
static bool ws2811Initialised = false;
volatile bool ws2811LedDataTransferInProgress = false;

uint16_t BIT_COMPARE_1 = 0;
uint16_t BIT_COMPARE_0 = 0;

static hsvColor_t ledColorBuffer[WS2811_DATA_BUFFER_SIZE];

#if !defined(USE_WS2811_SINGLE_COLOUR)
void setLedHsv(uint16_t index, const hsvColor_t *color)
{
    ledColorBuffer[index] = *color;
}

void getLedHsv(uint16_t index, hsvColor_t *color)
{
    *color = ledColorBuffer[index];
}

void setLedValue(uint16_t index, const uint8_t value)
{
    ledColorBuffer[index].v = value;
}

void scaleLedValue(uint16_t index, const uint8_t scalePercent)
{
    ledColorBuffer[index].v = ((uint16_t)ledColorBuffer[index].v * scalePercent / 100);
}
#endif

void setStripColor(const hsvColor_t *color)
{
    for (unsigned index = 0; index < WS2811_DATA_BUFFER_SIZE; index++) {
        ledColorBuffer[index] = *color;
    }
}

void setStripColors(const hsvColor_t *colors)
{
    for (unsigned index = 0; index < WS2811_DATA_BUFFER_SIZE; index++) {
        setLedHsv(index, colors++);
    }
}

void ws2811LedStripInit(ioTag_t ioTag)
{
    memset(ledStripDMABuffer, 0, sizeof(ledStripDMABuffer));

    ledStripIoTag = ioTag;
}

void ws2811LedStripEnable(void)
{
    if (!ws2811Initialised) {
        if (!ws2811LedStripHardwareInit(ledStripIoTag)) {
            return;
        }

        const hsvColor_t hsv_black = { 0, 0, 0 };
        setStripColor(&hsv_black);
        // RGB or GRB ordering doesn't matter for black
        ws2811UpdateStrip(LED_RGB);

        ws2811Initialised = true;
    }
}

bool isWS2811LedStripReady(void)
{
    return ws2811Initialised && !ws2811LedDataTransferInProgress;
}

STATIC_UNIT_TESTED void updateLEDDMABuffer(ledStripFormatRGB_e ledFormat, rgbColor24bpp_t *color, unsigned ledIndex)
{
    uint32_t packed_colour;

    switch (ledFormat) {
        case LED_RGB: // WS2811 drivers use RGB format
            packed_colour = (color->rgb.r << 16) | (color->rgb.g << 8) | (color->rgb.b);
            break;

        case LED_GRB: // WS2812 drivers use GRB format
        default:
            packed_colour = (color->rgb.g << 16) | (color->rgb.r << 8) | (color->rgb.b);
        break;
    }

    unsigned dmaBufferOffset = 0;
    for (int index = 23; index >= 0; index--) {
        ledStripDMABuffer[ledIndex * WS2811_BITS_PER_LED + dmaBufferOffset++] = (packed_colour & (1 << index)) ? BIT_COMPARE_1 : BIT_COMPARE_0;
    }
}

/*
 * This method is non-blocking unless an existing LED update is in progress.
 * it does not wait until all the LEDs have been updated, that happens in the background.
 */
void ws2811UpdateStrip(ledStripFormatRGB_e ledFormat)
{
    // don't wait - risk of infinite block, just get an update next time round
    if (!ws2811Initialised || ws2811LedDataTransferInProgress) {
        return;
    }

    unsigned ledIndex = 0;              // reset led index

    // fill transmit buffer with correct compare values to achieve
    // correct pulse widths according to color values
    while (ledIndex < WS2811_DATA_BUFFER_SIZE) {
        rgbColor24bpp_t *rgb24 = hsvToRgb24(&ledColorBuffer[ledIndex]);

        updateLEDDMABuffer(ledFormat, rgb24, ledIndex++);
    }

    ws2811LedDataTransferInProgress = true;
    ws2811LedStripDMAEnable();
}

#endif
