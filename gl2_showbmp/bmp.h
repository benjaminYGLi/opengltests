
// define BMP file header struct
typedef struct tagFILEHEADER {
    /* type : Magic identifier (0x42,0x4d) */
    unsigned short type;
    unsigned int size;/* File size in bytes */
    unsigned short reserved1, reserved2;
    unsigned int offset; /* Offset to image data, bytes */
} FILEHEADER;

// define BMP info header struct
typedef struct tagINFOHEADER {
    unsigned int size;/* Info Header size in bytes */
    int width,height;/* Width and height of image */
    unsigned short planes;/* Number of colour planes */
    unsigned short bits; /* Bits per pixel */
    unsigned int compression; /* Compression type */
    unsigned int imagesize; /* Image size in bytes */
    int xresolution,yresolution; /* Pixels per meter */
    unsigned int ncolours; /* Number of colours */
    unsigned int importantcolours; /* Important colours */
} INFOHEADER;
