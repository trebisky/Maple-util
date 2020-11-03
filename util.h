struct dfu_file {
    /* File name */
    char *name;
    /* Pointer to file loaded into memory */
    char *buf;
    int size;
};
