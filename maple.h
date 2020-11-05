struct dfu_file {
    /* File name */
    char *name;
    /* Pointer to file loaded into memory */
    char *buf;
    int size;
};

struct maple_device {
	struct libusb_device *dev;
	struct libusb_device_descriptor desc;
	libusb_device_handle *devh;
	int xfer_size;
	int interface;
	int alt;
};

