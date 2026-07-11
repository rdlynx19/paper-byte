/*****************************************************************************
* | File      	:   DEV_Config.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2020-02-19
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "DEV_Config.h"
#include <SPI.h>

static SPISettings epd_spi(4000000, MSBFIRST, SPI_MODE0);

void GPIO_Config(void)
{
#if D_9PIN
    pinMode(EPD_PWR_PIN,  OUTPUT);
    digitalWrite(EPD_PWR_PIN , HIGH);
#endif

    pinMode(EPD_BUSY_PIN,  INPUT);
    pinMode(EPD_RST_PIN , OUTPUT);
    pinMode(EPD_DC_PIN  , OUTPUT);
    
    pinMode(EPD_SCK_PIN, OUTPUT);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    pinMode(EPD_CS_PIN , OUTPUT);

    digitalWrite(EPD_CS_PIN , HIGH);
    digitalWrite(EPD_SCK_PIN, LOW);
}

void GPIO_Mode(UWORD GPIO_Pin, UWORD Mode)
{
    if(Mode == 0) {
        pinMode(GPIO_Pin , INPUT);
	} else {
		pinMode(GPIO_Pin , OUTPUT);
	}
}
/******************************************************************************
function:	Module Initialize, the BCM2835 library and initialize the pins, SPI protocol
parameter:
Info:
******************************************************************************/
UBYTE DEV_Module_Init(void)
{
	GPIO_Config();
	Serial.begin(115200);
	SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN); // SCK, MISO (none), MOSI
	return 0;
}

/******************************************************************************
function:
			SPI read and write
******************************************************************************/
void DEV_SPI_WriteByte(UBYTE data)
{
    SPI.beginTransaction(epd_spi);
    digitalWrite(EPD_CS_PIN, LOW);
    SPI.transfer(data);
    digitalWrite(EPD_CS_PIN, HIGH);
    SPI.endTransaction();
}

UBYTE DEV_SPI_ReadByte()
{
    return 0xFF; // EPD is write-only, read not used
}

void DEV_SPI_Write_nByte(UBYTE *pData, UDOUBLE len)
{
    SPI.beginTransaction(epd_spi);
    digitalWrite(EPD_CS_PIN, LOW);
    SPI.transferBytes(pData, nullptr, len);
    digitalWrite(EPD_CS_PIN, HIGH);
    SPI.endTransaction();
}


void DEV_Module_Exit(void)
{
#if D_9PIN
    digitalWrite(EPD_PWR_PIN , LOW);
#endif
    digitalWrite(EPD_RST_PIN , LOW);
}
