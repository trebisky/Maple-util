/* maple-util
 * Tom Trebisky  11-2-2020
 *
 * Tool to do DFU downloads over USB to my devices
 * that have Maple bootloaders.
 *
 * This replaces the use of dfu-util and reset.py for Maple boards.
 * Why do this?
 *  1) A chance for me to learn about libusb
 *  2) dfu-util error messages are terrible.
 *  3) This is specific and streamlined for maple devices
 *  4) If I ever add the ability to read elf files, it will be
 *     a step forward in avoiding stupid errors.
 *
 * TODO - 
 * - read elf file, verify link address 08005000
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <libusb.h>

#include "maple.h"
// #include "usb_dfu.h"

#define MAPLE_VENDOR		0x1eaf
#define MAPLE_PROD_LOADER	3
#define MAPLE_PROD_SERIAL	4

#define MAPLE_XFER_SIZE		1024

static char *blink_file = "blink.bin";
// static char *blink_file = "bogus.bin";

/* To allow this script to get access to the Maple DFU loader
 * without having to run as root all the time, put the following
 * into a file named 45-maple.rules and put that file
 * into /etc/udev/rules.d
ATTRS{idProduct}=="1001", ATTRS{idVendor}=="0110", MODE="664", GROUP="plugdev"
ATTRS{idProduct}=="1002", ATTRS{idVendor}=="0110", MODE="664", GROUP="plugdev"
ATTRS{idProduct}=="0003", ATTRS{idVendor}=="1eaf", MODE="664", GROUP="plugdev" SYMLINK+="maple"
ATTRS{idProduct}=="0004", ATTRS{idVendor}=="1eaf", MODE="664", GROUP="plugdev" SYMLINK+="maple"
 */

/* A successful download of my tiny blink demo looks like:
Sending 368 bytes
 - status 368
Ask for status
 - status response: 6
Ask for status
 - status response: 6
Sending zero size packet
 - status 0
Ask for status
 - status response: 6
state(8) = dfuMANIFEST-WAIT-RESET
status(0) = No error condition is present
Done!
All done !!
 */


#ifdef notdef
/* Now in maple.h */
struct maple_device {
	struct libusb_device *dev;
	struct libusb_device_descriptor desc;
	libusb_device_handle *devh;
	int xfer_size;
	int interface;
	int alt;
};
#endif

int list_maple ( libusb_context *, int );
int find_maple ( libusb_context *, struct maple_device * );
char *find_maple_serial ( void );
int get_file ( struct dfu_file * );
void perform_reset ( struct maple_device * );
int serial_trigger ( char * );
int wait_for_loader ( libusb_context * );
void milli_sleep ( int );

int dfuload_do_dnload ( struct maple_device *, struct dfu_file * );
int dfu_detach( libusb_device_handle *, const unsigned short, const unsigned short );

/* return codes from find_maple()
 * could be an enum, but I'm too lazy
 */
#define MAPLE_NONE	0
#define MAPLE_SERIAL	1
#define MAPLE_LOADER	2
#define MAPLE_UNKNOWN	3

void
error ( char *msg )
{
	fprintf ( stderr, "%s\n", msg );
	exit ( 1 );
}

/* The maple DFU loader has one interface.
 */
int
maple_open ( struct maple_device *mp )
{
	int s;

	mp->xfer_size = MAPLE_XFER_SIZE;
	mp->interface = 0;
	mp->alt = 1;

	mp->devh = NULL;
	s = libusb_open ( mp->dev, &mp->devh );
	if ( s || ! mp->devh ) {
	    printf ( "Maple open fails to open device\n" );
	    return 1;
	}

	s = libusb_claim_interface ( mp->devh, mp->interface);
	if ( s < 0 ) {
	    printf ( "Maple open cannot claim interface\n" );
	    return 1;
	}

	s = libusb_set_interface_alt_setting ( mp->devh, mp->interface, mp->alt );
	if ( s < 0 ) {
	    printf ( "Maple open cannot do alt setting %d\n", mp->alt );
	    return 1;
	}

	return 0;
}

void
maple_close ( struct maple_device *mp )
{
	libusb_release_interface ( mp->devh, mp->interface );

	if ( mp->devh )
	    libusb_close ( mp->devh );
	mp->devh = NULL;
}

struct dfu_file file;

int do_download = 1;

int verbose = 0;
int list_only = 0;


/* Options - 
 *
 * -vvvv - set verbosity
 * -l = list only
 */

int
main ( int argc, char **argv )
{
	struct maple_device maple_device;
	libusb_context *context;
	int s;
	char *ser;
	int m;
	int n;
	char *p;

	file.name = NULL;

	argc--;
	argv++;
	while ( argc-- ) {
	    p = *argv++;
	    if ( *p == '-' ) {
		p++;
		while ( *p && *p == 'v' ) {
		    p++;
		    verbose++;
		}
		if ( *p && *p == 'l' )
		    list_only = 1;
	    } else {
		file.name = p;
		printf ( "User filename: %s\n", file.name );
	    }
	}

	s = libusb_init(&context);
	if ( s )
	    error ( "Cannot init libusb" );

	n = list_maple ( context, verbose );
	if ( n > 1 ) {
	    printf ( "Warning !!!\n" );
	    printf ( " multiple (namely %d) maple devices discovered\n" );
	    printf ( " the first encountered will be used, which may not be right\n" );
	}

	m = find_maple ( context, NULL );

	switch ( m ) {
	    case MAPLE_SERIAL:
		printf ( "Maple device in serial (application) mode\n" );
		break;
	    case MAPLE_LOADER:
		printf ( "Maple device in DFU loader mode\n" );
		break;
	    case MAPLE_UNKNOWN:
		printf ( "Maple device in some unknown mode !!?\n" );
		return 0;
	    case MAPLE_NONE:
	    default:
		printf ( "No maple device found\n" );
		return 0;
	}

	if ( list_only ) {
	    // milli_sleep ( 100 );
	    exit ( 0 );
	}

	file.name = blink_file;

	if ( get_file ( &file ) ) {
	    printf ( "Cannot open file: %s\n", file.name );
	    error ( "Abandoning ship" );
	}
	if ( file.name && verbose )
	    printf ( "Read %d bytes from: %s\n", file.size, file.name );


	if ( m == MAPLE_SERIAL ) {
	    ser = find_maple_serial ();
	    if ( ! ser ) 
		printf ( "No maple device found\n" );
	    else
		printf ( "Found maple device: %s\n", ser );
	    if ( ! serial_trigger ( ser ) ) {
		printf ( "Failed to trigger USB loader\n" );
		exit ( 1 );
	    }
	    wait_for_loader ( context );
	}

	/* Get maple device and verify we are in
	 * DFU download mode.
	 */
	m = find_maple ( context, &maple_device );
	if ( m != MAPLE_LOADER ) {
	    printf ( "Not in DFU loader mode on final check\n" );
	    exit ( 1 );
	}

	if ( do_download ) {
	    // pickle ( &maple_device );
	    s = maple_open ( &maple_device );
	    if ( s == 0 ) {
		s = dfuload_do_dnload ( &maple_device, &file);
		if ( s != file.size )
		    printf ( "Download gave trouble\n" );
		printf ( "%d bytes sent\n", s );
		perform_reset ( &maple_device );
	    }
	    maple_close ( &maple_device );
	}

	libusb_exit(context);
	printf ( "All done !!\n" );
	return 0;
}

/* We usually see 1 0 0 2, i.e. we get the loader
 * after 0.4 seconds, even though we allow 1.0
 */

int
wait_for_loader ( libusb_context *context )
{
	int m;
	int i;

	for ( i=0; i<10; i++ ) {
	    milli_sleep ( 100 );
	    m = find_maple ( context, NULL );
	    // printf ( "Maple mode: %d\n", m );
	    if ( m == MAPLE_LOADER )
		return 1;
	}
	printf ( "Failed to enter loader mode\n" );
	return 0;
}

#include <sys/ioctl.h>

/* Monkey with modem control bits (see man 4 tty_ioctl)
 * we can BIC (clear), BIS (set), and GET (get)
 * This takes the Maple out of serial/application mode and
 * into loader mode.  I wrote a test to check every 0.1 seconds
 * and saw after this:
 *
 *  1 time - still in serial mode.
 *  1 time - gone altogether.
 *  28 times - in DFU loader mode (2.8 seconds).
 *  1 time - gone altogether.
 *  then back to serial/application mode.
 *
 * This just does the magic trick as per the reset.py script
 */

int
serial_trigger ( char *path )
{
	int fd;
	int rts_flag = TIOCM_RTS;
	int dtr_flag = TIOCM_DTR;
	int n;

	fd = open ( path, O_RDWR | O_NOCTTY );
	if ( fd < 0 ) {
	    printf ( "Open of %s fails\n", path );
	    return 0;
	}

	ioctl ( fd, TIOCMBIC, &rts_flag ); // RTS = 0
	milli_sleep ( 10 );	/* 0.01 second */

	ioctl ( fd, TIOCMBIC, &dtr_flag ); // DTR = 0
	milli_sleep ( 10 );	/* 0.01 second */
	ioctl ( fd, TIOCMBIS, &dtr_flag ); // DTR = 1
	milli_sleep ( 10 );	/* 0.01 second */
	ioctl ( fd, TIOCMBIC, &dtr_flag ); // DTR = 0

	ioctl ( fd, TIOCMBIS, &rts_flag ); // RTS = 1
	milli_sleep ( 10 );	/* 0.01 second */
	ioctl ( fd, TIOCMBIS, &dtr_flag ); // DTR = 1
	milli_sleep ( 10 );	/* 0.01 second */
	ioctl ( fd, TIOCMBIC, &dtr_flag ); // DTR = 0
	milli_sleep ( 10 );	/* 0.01 second */

	n = write ( fd, "1EAF", 4 );
	if ( n != 4 ) {
	    printf ( "Write to %s fails\n", path );
	    close(fd);
	    return 0;
	}
	// printf ( "Serial trigger, write: %d\n", n );
	/* Not buffered, no need to flush */

	milli_sleep ( 100 );	/* 0.1 second */
	close(fd);

	return 1;
}

#define DETACH_TIMEOUT	1000

void
perform_reset ( struct maple_device *mp )
{
	int s;

	printf ( "Performing device reset\n" );

	s = dfu_detach ( mp->devh, mp->interface, DETACH_TIMEOUT );
	if ( s < 0 )
	    printf ( "Detach failed\n" );

	s = libusb_reset_device ( mp->devh );
	if ( s < 0 )
	    printf ( "Reset failed: %d\n", s );
}

/* We don't read any fancy DFU format file,
 * just a binary image.
 */
int
get_file ( struct dfu_file *file )
{
	struct stat fstat;
	int fd;
	int n;

	if ( ! file->name )
	    return 1;

	if ( stat ( file->name, &fstat ) < 0 )
	    return 1;

	file->size = fstat.st_size;
	// printf ( "Stat gives: %d\n", fstat.st_size );
	if ( file->size > 128 * 1024 )
	    error ( "Input file too big" );

	/* We never free this, we just exit */
	file->buf = malloc ( file->size );
	if ( file->buf == NULL )
	    error ( "Cannot allocate file buffer" );

	fd = open ( file->name, O_RDONLY );
	if ( fd < 0 )
	    return 1;

	n = read ( fd, file->buf, file->size );
	// printf ( "read %d %d\n", n, file->size );
	close ( fd );
	if ( n != file->size )
	    error ( "IO error reading file" );

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
int
list_maple ( libusb_context *context, int verb )
{
	struct libusb_device_descriptor desc;
	struct libusb_device *dev;
	libusb_device **list;
	ssize_t ndev;
	int i;
	int s;
	int num = 0;

	ndev = libusb_get_device_list ( context, &list );
	// printf ( "%d USB devices in list\n", ndev );

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
	    if ( desc.idVendor != MAPLE_VENDOR ) {
		if ( verb ) 
		    printf("Vendor:Device = %04x:%04x\n", 
			desc.idVendor, desc.idProduct );
	    } else {
		num++;
		if ( desc.idProduct == MAPLE_PROD_SERIAL )
		    printf("Vendor:Device = %04x:%04x ---- Maple serial\n", 
			desc.idVendor, desc.idProduct );
		else if ( desc.idProduct == MAPLE_PROD_LOADER )
		    printf("Vendor:Device = %04x:%04x ---- Maple loader\n", 
			desc.idVendor, desc.idProduct );
		else
		    printf("Vendor:Device = %04x:%04x ---- Maple in unknown mode !?\n", 
			desc.idVendor, desc.idProduct );
	    }
	}
	libusb_free_device_list(list, 0);
	return num;
}

/* a modified version of the above, but instead of listing
 * everything, we just scan for the Maple vendor.
 *
 * XXX - we stop at the first match for the Maple Vendor.
 */
int
find_maple ( libusb_context *context, struct maple_device *mp )
{
	struct libusb_device_descriptor desc;
	struct libusb_device *dev;
	libusb_device **list;
	ssize_t ndev;
	int i;
	int s;
	int rv = MAPLE_NONE;

	ndev = libusb_get_device_list ( context, &list );
	// printf ( "%d USB devices in list\n", ndev );

	for ( i=0; i<ndev; i++ ) {
	    dev = list[i];
	    s = libusb_get_device_descriptor(dev, &desc);
	    if ( s ) {
		// printf ( "device %2d, no descriptor\n", i );
		continue;
	    }
	    if ( desc.idVendor != MAPLE_VENDOR )
		continue;
	    // printf("Vendor:Device = %04x:%04x\n", desc.idVendor, desc.idProduct );

	    if ( desc.idProduct == MAPLE_PROD_SERIAL )
		rv = MAPLE_SERIAL;
	    else if ( desc.idProduct == MAPLE_PROD_LOADER )
		rv = MAPLE_LOADER;
	    else
		rv = MAPLE_UNKNOWN;

	    /* We call with this NULL sometimes, just to get the state info.
	     */
	    if ( mp ) {
		/* Return first match */
		mp->dev = libusb_ref_device ( dev );
		memcpy ( &mp->desc, &desc, sizeof(desc) );
	    }

	    return rv;
	}

	libusb_free_device_list(list, 0);
	return MAPLE_NONE;
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
	    // printf ( "%s", p );
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
	    // printf ( "Serial: %s\n", dev );
	    sprintf ( dev2, "ttyACM%d", i );
	    if ( serial_is_maple ( dev2 ) )
		return dev;
	}

	return NULL;
}

#ifdef notdef
/* from usb_dfu.h */
struct usb_dfu_func_descriptor {
        uint8_t         bLength;
        uint8_t         bDescriptorType;
        uint8_t         bmAttributes;
#define USB_DFU_CAN_DOWNLOAD    (1 << 0)
#define USB_DFU_CAN_UPLOAD      (1 << 1)
#define USB_DFU_MANIFEST_TOL    (1 << 2)
#define USB_DFU_WILL_DETACH     (1 << 3)
        uint16_t                wDetachTimeOut;
        uint16_t                wTransferSize;
        uint16_t                bcdDFUVersion;
} __attribute__ ((packed));

/* We could actually have a list that we need to loop through
 * The Maple just gives us a single 9 byte descriptor.
 */
static void
extract_dfu ( const char *list, int len, int type, struct usb_dfu_func_descriptor *fp )
{
	int xfer_size;

	printf ( "len = %d, sizeof desc = %d\n", len, sizeof(*fp) );

	if ( len != sizeof(*fp) )
	    return;

	memcpy ( fp, list, len );
	printf ( "Desc length = %d\n", fp->bLength );
	printf ( "Desc type = 0x%x\n", fp->bDescriptorType );
	printf ( "Desc wSize = %d\n", fp->wTransferSize );
	xfer_size = libusb_le16_to_cpu ( fp->wTransferSize );
	printf ( "Desc wSize = %d\n", xfer_size );
}


#define USB_DT_DFU			0x21

/* An experiment.
 * This scans through the USB config information
 * to get a USB_DT_DFU interface descriptor.
 * This contains the transfer count for our device.
 * A generic utility would need to do this, but we can
 * just wire in the number since we only ever intend
 * to work with the maple boot loader.
 * The Maple loader has 1 configuration and 1 interface.
 * The interface has 2 alt settings.
 * alt setting 0 is "load to RAM" and as near as I
 *  can tell, was an unfinished idea that does not work.
 * alt setting 1 is "load to flash" and is what we use.
 * Here is the verbatim output from this code.
 *
Maple has 1 configurations
maple config extra length = 0
Maple has 1 interfaces
Maple has 2 alt settings
Maple alt 0, class =   fe, subclass =    1
Maple alt 0, extra length = 0
len = 0, sizeof desc = 9
Maple alt 1, class =   fe, subclass =    1
Maple alt 1, extra length = 9
len = 9, sizeof desc = 9
Desc length = 9
Desc type = 0x21
Desc wSize = 1024
Desc wSize = 1024
Cannot get maple config descriptor 2
 */
void
pickle ( struct maple_device *mp )
{
	int i, j, k;
	int nc, ni, na;
	struct libusb_config_descriptor *cp;
	const struct libusb_interface *ip;
	const struct libusb_interface_descriptor *idp;
	int s;
	struct usb_dfu_func_descriptor func_dfu;
	libusb_device_handle *devh;

	/* Doesn't work */
	nc = mp->desc.bNumConfigurations;
	printf ( "Maple has %d configurations\n", nc );
	for ( i=0; i<nc; i++ ) {
	    s = libusb_get_config_descriptor ( mp->dev, i, &cp );
	    if ( s ) {
		printf ( "Cannot get maple config descriptor 1\n" );
		break;
	    }
	    if ( ! cp ) {
		printf ( "Empty maple config descriptor\n" );
		break;
	    }
	    printf ( "maple config extra length = %d\n", cp->extra_length );

	    ni = cp->bNumInterfaces;
	    printf ( "Maple has %d interfaces\n", ni );
	    for ( j=0; j<ni; j++ ) {
		ip = &cp->interface[j];
		if ( ! ip )
		    break;
		na = ip->num_altsetting;
		printf ( "Maple has %d alt settings\n", na );
		for ( k=0; k<na; k++ ) {
		    idp = &ip->altsetting[k];
		    printf ( "Maple alt %d, class = %4x, subclass = %4x\n",
			k, idp->bInterfaceClass, idp->bInterfaceSubClass );
		    printf ( "Maple alt %d, extra length = %d\n", k, idp->extra_length );
		    extract_dfu ( idp->extra, idp->extra_length, USB_DT_DFU, &func_dfu );
		}
	    }
	}

	/* Doesn't work either */
	s = libusb_open ( mp->dev, &devh );
	if ( s ) {
	    printf ( "Cannot open maple device to get config\n" );
	    return;
	}
	s = libusb_get_descriptor ( devh, USB_DT_DFU, 0, (void *) &func_dfu, sizeof(func_dfu) );
	if ( s ) {
	    printf ( "Cannot get maple config descriptor 2\n" );
	    return;
	}
	libusb_close ( devh );

	printf ( "Xfer size = %d\n", func_dfu.wTransferSize );

}
#endif

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
