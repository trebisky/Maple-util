/* maple-util
 * Tom Trebisky  11-2-2020
 *
 * Tool to do DFU downloads over USB to my devices
 * that have Maple bootloaders.
 */
#include <stdlib.h>
#include <stdio.h>

#include <libusb.h>

#define MAPLE_VENDOR	0x1eaf
#define MAPLE_PROD_LOADER	3
#define MAPLE_PROD_SERIAL	4

int verbose = 0;

void list_maple ( libusb_context * );

void
error ( char *msg )
{
	fprintf ( stderr, "%s\n", msg );
	exit ( 1 );
}

int
main ( int argc, char **argv )
{
	int s;
	libusb_context *context;

	s = libusb_init(&context);
	if ( s )
	    error ( "Cannot init libusb" );

	list_maple ( context );

	libusb_exit(context);
	return 0;
}

#ifdef notdef
/* This works, but the strings are not particularly interesting
 * and are only rarely provided.
 * In fact on my system, the only device providing these strings
 * was my STlink V2 device, which yielded the following:
 * Vendor:Device = 0483:3748 -- STMicroelectronics STMicroelectronics
 */
char *
get_string ( struct libusb_device *dev, int index )
{
	static char str[50];
	struct libusb_device_handle *devh;

	str[0] = '\0';
	if ( ! index )
	    return str;

	if ( libusb_open ( dev, &devh ) == 0 ) {
	    libusb_get_string_descriptor_ascii ( devh, index, str, 50 );
	    libusb_close ( devh );
	}
	return str;
}
#endif

/*
 * 1eaf:0003 is my Maple r5 in boot loader mode
 *  lsusb: Bus 001 Device 046: ID 1eaf:0003 Leaflabs Maple DFU interface
 * 1eaf:0004 is my Maple r5 in application mode
 *  lsusb: Bus 001 Device 043: ID 1eaf:0004 Leaflabs Maple serial interface
 *
 * In lieu of the following, a person could just run lsusb and write
 *  a python script to capture and parse the output.
 */
void
list_maple ( libusb_context *context )
{
	struct libusb_device_descriptor desc;
	struct libusb_device *dev;
	libusb_device **list;
	ssize_t ndev;
	int i;
	int s;

	ndev = libusb_get_device_list ( context, &list );
	printf ( "%d USB devices in list\n", ndev );
	for ( i=0; i<ndev; i++ ) {
	    dev = list[i];
	    s = libusb_get_device_descriptor(dev, &desc);
	    if ( s ) {
		printf ( "device %2d, no descriptor\n", i );
		continue;
	    }
#ifdef notdef
	    printf("Vendor:Device = %04x:%04x -- %s %s\n", 
		desc.idVendor, desc.idProduct,
		get_string (dev, desc.iManufacturer),
		get_string (dev, desc.iProduct) );
#endif
	    printf("Vendor:Device = %04x:%04x -- %s %s\n", 
		desc.idVendor, desc.idProduct );
	}
	libusb_free_device_list(list, 0);
}

/* This assumes we have nanosleep()
 *  and we do on a linux system.
 */
# include <time.h>

void
milli_sleep ( int msec )
{
	struct timespec ns_delay;

	if ( msec <= 0 )
	    return;

	ns_delay.tv_sec = msec / 1000;
	msec %= 1000;
	ns_delay.tv_nsec = msec * 1000000;

	nanosleep ( &ns_delay, NULL);
}

/* Stub for now */
void
dfu_progress_bar(const char *desc, unsigned long long curr,
                unsigned long long max)
{
}

/* THE END */
