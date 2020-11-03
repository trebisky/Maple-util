/* maple-util
 * Tom Trebisky  11-2-2020
 *
 * Tool to do DFU downloads over USB to my devices
 * that have Maple bootloaders.
 */
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>

#include <libusb.h>

#define MAPLE_VENDOR	0x1eaf
#define MAPLE_PROD_LOADER	3
#define MAPLE_PROD_SERIAL	4

/* To allow this script to get access to the Maple DFU loader
 * without having to run as root all the time, put the following
 * into a file named 45-maple.rules and put that file
 * into /etc/udev/rules.d
ATTRS{idProduct}=="1001", ATTRS{idVendor}=="0110", MODE="664", GROUP="plugdev"
ATTRS{idProduct}=="1002", ATTRS{idVendor}=="0110", MODE="664", GROUP="plugdev"
ATTRS{idProduct}=="0003", ATTRS{idVendor}=="1eaf", MODE="664", GROUP="plugdev" SYMLINK+="maple"
ATTRS{idProduct}=="0004", ATTRS{idVendor}=="1eaf", MODE="664", GROUP="plugdev" SYMLINK+="maple"
 */

int verbose = 0;

void list_maple ( libusb_context * );
char *find_maple_serial ( void );

void
error ( char *msg )
{
	fprintf ( stderr, "%s\n", msg );
	exit ( 1 );
}

int
main ( int argc, char **argv )
{
	libusb_context *context;
	int s;
	char *ser;

	s = libusb_init(&context);
	if ( s )
	    error ( "Cannot init libusb" );

	list_maple ( context );

	ser = find_maple_serial ();
	if ( ! ser ) 
	    printf ( "No maple device found\n" );
	else
	    printf ( "Found maple device: %s\n", ser );

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

/* The idea here is to open
 * /sys/class/tty/ttyACM0/device/uevent
 * And read something like this:
DEVTYPE=usb_interface
DRIVER=cdc_acm
PRODUCT=1eaf/4/200
TYPE=2/0/0
INTERFACE=2/2/1
MODALIAS=usb:v1EAFp0004d0200dc02dsc00dp00ic02isc02ip01in00
 *
 * The PRODUCT line is the thing, if it contains "1eaf/4" you got it.
 */
static int
serial_is_maple ( char *dev )
{
	char path[100];
	char line[100];
	FILE *fp;
	char *p;
	int rv = 0;

	sprintf ( path, "/sys/class/tty/%s/device/uevent", dev );

	fp = fopen ( path, "r" );
	if ( ! fp )
	    return rv;

	while ( fgets(line,100,fp) ) {
	    if ( line[0] != 'P' )
		continue;
	    if ( line[1] != 'R' )
		continue;
	    if ( line[3] != 'D' )
		continue;
	    p = line;
	    while ( *p && *p != '=' )
		p++;
	    printf ( "%s", p );
	    if ( strncmp ( p, "=1eaf/4", 7 ) == 0 )
		rv = 1;
	    break;
	}
	fclose ( fp );
	return rv;
}

/* Call this and expect something like "/dev/ttyACM0" to get returned.
 *  in fact that is the usual thing at this time.
 *  A person could add some trickery to the udev rules to generate
 *  a symlink like /dev/maple to make all this unnecessary.
 */
char *
find_maple_serial ( void )
{
	static char dev[20];
	char dev2[20];
	int i;
	int fd;

	for ( i=0; i<10; i++ ) {
	    sprintf ( dev, "/dev/ttyACM%d", i );
	    fd = open ( dev, O_RDWR );
	    if ( fd < 0 )
		break;
	    printf ( "Serial: %s\n", dev );
	    sprintf ( dev2, "ttyACM%d", i );
	    if ( serial_is_maple ( dev2 ) )
		return dev;
	}

	return NULL;
}

/* ============================================================= */

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
