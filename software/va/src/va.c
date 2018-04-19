/*
 ============================================================================
 Name        : va.c
 Author      : 
 Version     :
 Copyright   : 
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <alt_16550_uart.h>
#include <alt_bridge_manager.h>
#include <alt_mmu.h>


uint16_t video[1024*768*4];
uintptr_t pa;

int main(void) {
	puts("!!!Hello World!!!"); /* prints !!!Hello World!!! */
	pa = alt_mmu_va_to_pa(video, NULL, NULL);
	alt_bridge_init( ALT_BRIDGE_LWH2F, NULL ,NULL );
	return EXIT_SUCCESS;
}