// c / cpp
#include <assert.h>
//#include <malloc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// hwlib
#include <alt_16550_uart.h>
#include <alt_address_space.h>
#include <alt_bridge_manager.h>
#include <alt_cache.h>
#include <alt_clock_manager.h>
#include <alt_fpga_manager.h>
#include <alt_generalpurpose_io.h>
#include <alt_globaltmr.h>
#include <alt_int_device.h>
#include <alt_interrupt.h>
#include <alt_interrupt_common.h>
#include <alt_mmu.h>
#include <alt_printf.h>
#include <alt_watchdog.h>
#include <hwlib.h>
// socal
#include <alt_gpio.h>
#include <alt_sdmmc.h>
#include <hps.h>
#include <socal.h>
// project
#include "../include/alt_pt.h"
#include "../include/diskio.h"
#include "../include/ff.h"
#include "../include/pio.h"
#include "../include/system.h"
#include "../include/va_sm.h"

// int __auto_semihosting;

#define FREQ 950000
#define XSIZE 256
#define YSIZE 192
#define PI 3.141596

#define BUTTON_IRQ BUTTON_PIO_IRQ+ALT_INT_INTERRUPT_F2S_FPGA_IRQ0
#define fpga_leds ALT_LWFPGASLVS_ADDR + LED_PIO_BASE
//#define BUTTON_IRQ ALT_INT_INTERRUPT_F2S_FPGA_IRQ1

uint8_t red;
uint8_t green;
uint8_t blue;
volatile uint16_t * vid;
volatile uint16_t * screen;
volatile uint16_t * splash;
volatile uint8_t font[256][64];
ALT_16550_HANDLE_t uart;

ALT_STATUS_CODE delay_us(uint32_t us) {
	ALT_STATUS_CODE status = ALT_E_SUCCESS;

	uint64_t start_time = alt_globaltmr_get64();
	uint32_t timer_prescaler = alt_globaltmr_prescaler_get() + 1;
	uint64_t end_time;
	alt_freq_t timer_clock;

	status = alt_clk_freq_get(ALT_CLK_MPU_PERIPH, &timer_clock);
	end_time = start_time + us * ((timer_clock / timer_prescaler) / 1000000);

	while (alt_globaltmr_get64() < end_time) {
	}

	return status;
}

void setcolor(uint8_t r, uint8_t g, uint8_t b) {
	red = r;
	green = g;
	blue = b;
}
void drawpixel(uint16_t x, uint16_t y) {

	vid[(y * 1024 + x) * 4 + 1] = red;
	vid[(y * 1024 + x) * 4 + 0] = blue + green * 256;
}

void drawline(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
	uint16_t steps;
	int cc;
	if (abs(x2 - x1) > abs(y2 - y1))
		steps = abs(x2 - x1);
	else
		steps = abs(y2 - y1);

	for (cc = 0; cc <= steps; cc++) {
		drawpixel(x1 + floor(1.0 * (x2 - x1) / steps * cc),
				y1 + floor(1.0 * (y2 - y1) / steps * cc));
	}

}

double hue2rgb(double p, double q, double tt) {
	double t;
	t = tt;
	if (t < 0)
		t += 1;
	if (t > 1)
		t -= 1;

	if (t < 1.0 / 6.0)
		return p + (q - p) * 6 * t;
	else if (t < 1.0 / 2.0)
		return q;
	else if (t < 2.0 / 3.0)
		return p + (q - p) * (2.0 / 3.0 - t) * 6;
	else
		return p;
}

uint8_t hslToR(double h, double s, double l) {

	if (s == 0) {
		return l;
	} else {

		double q = (l < 0.5) ? (l * (1 + s)) : (l + s - l * s);
		double p = 2 * l - q;
		return floor(255.0 * hue2rgb(p, q, h + 1.0 / 3.0));

	}
}
///

uint8_t hslToG(double h, double s, double l) {

	if (s == 0) {
		return l;
	} else {

		double q = (l < 0.5) ? (l * (1 + s)) : (l + s - l * s);
		double p = 2 * l - q;

		return floor(255.0 * hue2rgb(p, q, h));

	}
}
///
uint8_t hslToB(double h, double s, double l) {

	if (s == 0) {
		return l;
	} else {

		double q = (l < 0.5) ? (l * (1 + s)) : (l + s - l * s);
		double p = 2 * l - q;

		return floor(255.0 * hue2rgb(p, q, h - 1.0 / 3.0));
	}
}

void fpgaprepare() {
	uintptr_t pa;

	alt_write_word(ALT_LWFPGASLVS_OFST+HDMI_PIO_READY_BASE, 0x0000);
	delay_us(1);

	pa = alt_mmu_va_to_pa(screen, NULL, NULL);

	alt_write_word(ALT_LWFPGASLVS_OFST+HDMI_PIO_BASE, pa / 8);
	alt_write_word(ALT_LWFPGASLVS_OFST+HDMI_PIO_READY_BASE, 0x0001);

}

void drawtext(const char * string, int size, uint16_t x, uint16_t y) {
	int counter;
	int i;
	int j;

	for (counter = 0; counter < size; counter++) {
		for (i = 0; i < 8; i++) {
			for (j = 0; j < 8; j++) {
				if (font[string[counter]][j * 8 + i] == 0)
					drawpixel(x + counter * 8 + i, y + j);
			}
		}
	}
}

void clrscr() {

//	memset(vid,0,1024*768*4*sizeof(uint16_t));
	memcpy(vid, splash, 1024 * 768 * 4 * sizeof(uint16_t));

}

/* Interrupt service routine for the buttons */
void fpga_pb_isr_callback(uint32_t icciar, void *context) {
	int ALT_RESULT;

	/* Read the captured edges */
	uint32_t edges = pio_get_edgecapt(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE);

	/* Clear the captured edges */
	pio_set_edgecapt(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE, edges);

	/* Increase blinking speed if requested */
	if (edges & 0x1) {
		ALT_RESULT = alt_16550_fifo_write_safe(&uart, "INTERRUPT!\n\r", 12,
		true);
	}

}

void init(void) {
	ALT_STATUS_CODE ALT_RESULT = ALT_E_SUCCESS;
	ALT_STATUS_CODE ALT_RESULT2 = ALT_E_SUCCESS;
	ALT_STATUS_CODE status;

	ALT_RESULT = alt_globaltmr_init();
	ALT_RESULT2 = alt_bridge_init(ALT_BRIDGE_F2S, NULL, NULL);

	status = alt_fpga_init();
	if (alt_fpga_state_get() != ALT_FPGA_STATE_USER_MODE) {
		ALT_RESULT = alt_16550_fifo_write_safe(&uart, "FPGA ERROR\n\r", 12,
		true);
		status = ALT_E_ERROR;
	}

	ALT_RESULT = alt_bridge_init(ALT_BRIDGE_LWH2F, NULL, NULL);
	status = alt_addr_space_remap(ALT_ADDR_SPACE_MPU_ZERO_AT_BOOTROM,
			ALT_ADDR_SPACE_NONMPU_ZERO_AT_OCRAM, ALT_ADDR_SPACE_H2F_ACCESSIBLE,
			ALT_ADDR_SPACE_LWH2F_ACCESSIBLE);

	status = alt_int_global_init();
	status = alt_int_cpu_init();
	status = alt_pt_init();
	status = alt_cache_system_enable();

	//	ALT_RESULT = alt_wdog_reset(ALT_WDOG0);
	//	ALT_RESULT = alt_wdog_reset(ALT_WDOG0_INIT);

	status = alt_int_dist_target_set(BUTTON_IRQ, 0x3);
	status = alt_int_dist_trigger_set(BUTTON_IRQ, ALT_INT_TRIGGER_EDGE);
	status = alt_int_dist_enable(BUTTON_IRQ);
	status = alt_int_isr_register(BUTTON_IRQ, fpga_pb_isr_callback, NULL);

	/* Clear button presses already detected */
	pio_set_edgecapt(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE, 0x1);
	/* Enable the button interrupts */
	pio_set_intmask(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE, 0x1);

	status = alt_int_cpu_enable();
	status = alt_int_global_enable();

	ALT_RESULT = alt_clk_is_enabled(ALT_CLK_L4_SP);
	if (ALT_RESULT == ALT_E_FALSE)
		ALT_RESULT = alt_clk_clock_enable(ALT_CLK_L4_SP);

	ALT_RESULT = alt_16550_init(ALT_16550_DEVICE_SOCFPGA_UART0, NULL, 0, &uart);
	ALT_RESULT = alt_16550_baudrate_set(&uart, 115200);
	ALT_RESULT = alt_16550_line_config_set(&uart, ALT_16550_DATABITS_8,
			ALT_16550_PARITY_DISABLE, ALT_16550_STOPBITS_1);
	ALT_RESULT = alt_16550_fifo_enable(&uart);
	ALT_RESULT = alt_16550_enable(&uart);
	//ALT_RESULT = alt_gpio_init();
	//ALT_RESULT = alt_gpio_group_config(led_gpio_init, 24);
	ALT_RESULT = alt_16550_fifo_write_safe(&uart, "Program START\n\r", 15,
	true);

	if (ALT_RESULT2 == ALT_E_SUCCESS)
		ALT_RESULT = alt_16550_fifo_write_safe(&uart, "F2S Bridge init!!\n\r",
				19, true);
}

void videoinit(void) {
	FIL Fil;
	uint8_t header[54];
	UINT bytes_read;
	int io_x = 0;
	int io_y = 0;
	int io_sub_x;
	int i, j;
	volatile uint8_t io_buff[128 * 3];
	ALT_STATUS_CODE ALT_RESULT;
	uint8_t fontimage[256 * 128];
	int char_x;
	int char_y;

	red = 255;
	green = 255;
	blue = 255;

	memset(splash, 0, 1024 * 768 * 4 * sizeof(uint16_t));

	if (f_open(&Fil, "splash.bmp", FA_READ) == FR_OK) {
		f_read(&Fil, header, 54, &bytes_read);
		for (io_y = 0; io_y < 768; io_y++)
			for (io_x = 0; io_x < 8; io_x++)//8 = 1024/128 - number of times buffer fit into line
					{
				f_read(&Fil, io_buff, 128 * 3, &bytes_read);

				for (io_sub_x = 0; io_sub_x < 128; io_sub_x++) {
					splash[((767 - io_y) * 1024 + io_x * 128 + io_sub_x) * 4 + 1] =
							io_buff[io_sub_x * 3 + 2];
					splash[((767 - io_y) * 1024 + io_x * 128 + io_sub_x) * 4 + 0] =
							io_buff[io_sub_x * 3]
									+ io_buff[io_sub_x * 3 + 1] * 256;
				}
			}
		f_close(&Fil);
	}

	memset(font, 1, 256 * 64 * sizeof(uint8_t));
	if (f_open(&Fil, "font.bmp", FA_READ) == FR_OK) {
		f_read(&Fil, header, 54, &bytes_read);

		for (io_y = 0; io_y < 128; io_y++) // read all, but actually we need only the top half
			// io_y from 64 to 127
			for (io_x = 0; io_x < 2; io_x++)//2 = 256/128 - number of times buffer fit into line
					{
				f_read(&Fil, io_buff, 128 * 3, &bytes_read);
				for (io_sub_x = 0; io_sub_x < 128; io_sub_x++) {
					fontimage[(127 - io_y) * 256 + io_x * 128 + io_sub_x] =
							io_buff[io_sub_x * 3];
				}
			}

		f_close(&Fil);
	}

	for (char_x = 0; char_x < 32; char_x++)
		for (char_y = 0; char_y < 8; char_y++) {
			for (i = 0; i < 8; i++)
				for (j = 0; j < 8; j++)
					font[char_y * 32 + char_x][j * 8 + i] = fontimage[char_x * 8
							+ i + (char_y * 8 + j) * 256];
		}

	memset(screen, 65535, 1024 * 768 * 4 * sizeof(uint16_t));
	memset(vid, 0, 1024 * 768 * 4 * sizeof(uint16_t));
}

void setup_fpga_leds() {
	// Switch on first LED only
	alt_write_word(fpga_leds, 0x1);
}

void handle_fpga_leds() {
	uint32_t leds_mask = alt_read_word(fpga_leds);

	if (leds_mask != (0x01 << (LED_PIO_DATA_WIDTH - 1))) {
		// rotate leds
		leds_mask <<= 1;
	} else {
		// reset leds
		leds_mask = 0x1;
	}

	alt_write_word(fpga_leds, leds_mask);
}

int main(void) {
	ALT_STATUS_CODE ALT_RESULT = ALT_E_SUCCESS;
	ALT_STATUS_CODE ALT_RESULT2 = ALT_E_SUCCESS;

	ALT_STATUS_CODE status;
	volatile uint16_t video[1024 * 768 * 4 + 4];
	volatile uint16_t buffer[1024 * 768 * 4 + 4];
	volatile uint16_t splashscreen[1024 * 768 * 4 + 4];

	double hue = 0;
	FATFS *fs;
	int res;

	uint16_t x1 = 0;
	uint16_t y1 = 0;
	uint16_t x2 = 0;
	uint16_t y2 = 0;
	uint16_t x3 = 0;
	uint16_t y3 = 0;
	uint16_t radius = 100;
	uint16_t centerx = 1024 / 2;
	uint16_t centery = 768 / 2;
	char fpsstring[3];

	char string1[255];
	int sinlut[360];
	int coslut[360];
	int i, j = 0;
	uint64_t secstart;
	uint64_t secend;
	uint16_t frames;
	uint16_t fps;
	uint32_t timer_prescaler;
	alt_freq_t timer_clock;

	init();

	fs = malloc(sizeof(FATFS));
	res = f_mount(fs, "0:", 0);

	// alias for video memory itself
	screen = video;
	// alias for buffer to store updates
	vid = buffer;
	// alias for splashscreen buffer
	splash = splashscreen;

	videoinit();
	fpgaprepare();

	//alt_write_word(ALT_LWFPGASLVS_OFST+LED_PIO_BASE,0xAA);

	for (i = 0; i < 360; i++) {
		sinlut[i] = floor(radius * sin(PI * i / 180.0));
		coslut[i] = floor(radius * cos(PI * i / 180.0));
	}

	timer_prescaler = alt_globaltmr_prescaler_get() + 1;
	status = alt_clk_freq_get(ALT_CLK_MPU_PERIPH, &timer_clock);

	ALT_RESULT = alt_16550_fifo_write_safe(&uart, "Ready to go!\n\r", 14, true);

	// NOW WE READY
	setup_fpga_leds();
	va_sm_init();

	va_sm_set_reg(VASM_ADDR_FREQ, 0xAABBCCDD);
	va_sm_set_reg(VASM_ADDR_LATENCY, 0x12345678);
	va_sm_set_reg(VASM_ADDR_RAVERAGE, 0xF0F0F0F0);
	va_sm_run();

	i = 0;

	frames = 0;
	fps = 0;

	secstart = alt_globaltmr_get64();

	for (;;) {
		//main loop

		//do something
		//delay_us(25);
		clrscr();

		//angle = angle + PI/180;
		//if (angle>2*PI) angle -= 2*PI;

		i = (i + 1) % 360;
		hue = hue + 0.002;
		if (hue > 1.0)
			hue -= 1.0;
		x1 = centerx + sinlut[i];
		y1 = centery + coslut[i];

		x2 = centerx + sinlut[(i + 120) % 360];
		y2 = centery + coslut[(i + 120) % 360];

		x3 = centerx + sinlut[(i + 240) % 360];
		y3 = centery + coslut[(i + 240) % 360];

		setcolor(hslToR(hue, 0.6, 0.5), hslToG(hue, 0.6, 0.5),
				hslToB(hue, 0.6, 0.5));
		//setcolor (255,0,255);

		drawline(x1, y1, x2, y2);
		drawline(x2, y2, x3, y3);
		drawline(x3, y3, x1, y1);
		// ALT_RESULT = alt_16550_fifo_write_safe(&uart,"Pum\n\r",5,true);
		alt_sprintf(string1, "Hello world. i = %3d", i);
		drawtext(string1, 20, 100 + abs(180 - i), 100 + abs(180 - i));
		alt_sprintf(fpsstring, "%d", fps);
		drawtext(fpsstring, 2, 10, 10);
		//swap buffers
		memcpy(screen, buffer, 1024 * 768 * 4 * sizeof(uint16_t));
		secend = alt_globaltmr_get64();
		frames++;

		if ((secend - secstart) > (timer_clock / timer_prescaler)) {
			fps = frames;
			frames = 0;
			secstart = secend;
		}
	}

	// virtually never
	ALT_RESULT = alt_16550_fifo_write_safe(&uart, "Program end. Why?\n\r", 3,
	true);
//	ALT_RESULT = alt_int_global_uninit	();
	ALT_RESULT = alt_bridge_uninit(ALT_BRIDGE_F2S, NULL, NULL);
	ALT_RESULT = alt_bridge_uninit(ALT_BRIDGE_LWH2F, NULL, NULL);
	ALT_RESULT = alt_16550_uninit(&uart);

	return ALT_RESULT;
}
