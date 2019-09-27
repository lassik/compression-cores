// Lempel-Ziv decompression. Mostly based on Tom Pfau's assembly
// language code. The contents of this file are hereby released to the
// public domain. -- Rahul Dhesi 1986/11/14, 1987/02/08

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Use buffer sizes of at least 1024, larger if enough memory is
// available. Buffer sizes of over 8192 have not been confirmed to
// work.
#define IN_BUF_SIZE 1024
#define OUT_BUF_SIZE 1024

// Decompression stack. Except in pathological cases, 2000 bytes
// should be enough. Rare files may need a bigger stack to decompress.
// May be decreased to 1000 bytes if memory is tight.
#define STACKSIZE 2000  // adjust to conserve memory

#define INBUFSIZ (IN_BUF_SIZE - SPARE)
#define OUTBUFSIZ (OUT_BUF_SIZE - SPARE)
#define MEMERR 2
#define IOERR 1
#define MAXBITS 13
#define CLEAR 256       // clear code
#define Z_EOF 257       // end of file marker
#define FIRST_FREE 258  // first free code
#define MAXMAX 8192     // max code + 1

#define SPARE 5

struct tabentry {
    unsigned int next;
    char z_ch;
};

static struct tabentry *table;
static int gotmem = 0;

static void init_dtab(void);
static unsigned int rd_dcode(void);
static void wr_dchar(char ch);
static void ad_dcode(void);

static unsigned int lzd_sp = 0;
static unsigned int lzd_stack[STACKSIZE + SPARE];

static void die(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void push(int ch)
{
    lzd_stack[lzd_sp++] = ch;
    if (lzd_sp >= STACKSIZE) {
        die("Stack overflow");
    }
}

#define pop() (lzd_stack[--lzd_sp])

static char out_buf_adr[IN_BUF_SIZE + SPARE];
static char in_buf_adr[OUT_BUF_SIZE + SPARE];

static unsigned int cur_code;
static unsigned int old_code;
static unsigned int in_code;

static unsigned int free_code;
static int nbits;
static unsigned int max_code;

static char fin_char;
static char k;
static unsigned int masks[] = { 0, 0, 0,     0,     0,     0,     0,
                                0, 0, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff };
static unsigned int bit_offset;
static unsigned int output_offset;
static int in_han, out_han;

static int lzd(int input_handle, int output_handle)
{
    in_han = input_handle;    // make it avail to other fns
    out_han = output_handle;  // ditto
    nbits = 9;
    max_code = 512;
    free_code = FIRST_FREE;
    lzd_sp = 0;
    bit_offset = 0;
    output_offset = 0;

    if (gotmem == 0) {
        table =
        (struct tabentry *)malloc(MAXMAX * sizeof(struct tabentry) + SPARE);
        gotmem++;
    }
    if (table == (struct tabentry *)0) {
        die("Out of memory");
    }

    if (read(in_han, in_buf_adr, INBUFSIZ) == -1) {
        die("Read error");
    }

    init_dtab();  // initialize table

loop:
    cur_code = rd_dcode();
    if (cur_code == Z_EOF) {
        if (output_offset != 0) {
            if (out_han != -2) {
                if (write(out_han, out_buf_adr, output_offset) !=
                    output_offset) {
                    die("Write error");
                }
            }
            addbfcrc(out_buf_adr, output_offset);
        }
        return (0);
    }

    if (cur_code == CLEAR) {
        init_dtab();
        fin_char = k = old_code = cur_code = rd_dcode();
        wr_dchar(k);
        goto loop;
    }

    in_code = cur_code;
    if (cur_code >= free_code) {  // if code not in table (k<w>k<w>k)
        cur_code = old_code;      // previous code becomes current
        push(fin_char);
    }

    while (cur_code > 255) {              // if code, not character
        push(table[cur_code].z_ch);       // push suffix char
        cur_code = table[cur_code].next;  // <w> := <w>.code
    }

    k = fin_char = cur_code;
    push(k);
    while (lzd_sp != 0) {
        wr_dchar(pop());
    }
    ad_dcode();
    old_code = in_code;

    goto loop;
}  // lzd()

// rd_dcode() reads a code from the input (compressed) file and returns its
// value.
static unsigned int rd_dcode(void)
{
    register char *ptra, *ptrb;  // miscellaneous pointers
    unsigned int word;           // first 16 bits in buffer
    unsigned int byte_offset;
    char nextch;              // next 8 bits in buffer
    unsigned int ofs_inbyte;  // offset within byte

    ofs_inbyte = bit_offset % 8;
    byte_offset = bit_offset / 8;
    bit_offset = bit_offset + nbits;

    if (byte_offset >= INBUFSIZ - 5) {
        int space_left;

        bit_offset = ofs_inbyte + nbits;
        space_left = INBUFSIZ - byte_offset;
        ptrb = byte_offset + in_buf_adr;  // point to char
        ptra = in_buf_adr;
        // we now move the remaining characters down buffer beginning
        while (space_left > 0) {
            *ptra++ = *ptrb++;
            space_left--;
        }
        if (read(in_han, ptra, byte_offset) == -1) {
            die("Read error");
        }
        byte_offset = 0;
    }
    ptra = byte_offset + in_buf_adr;
    // NOTE:  "word = *((int *) ptra)" would not be independent of byte order.

    word = (unsigned char)*ptra;
    ptra++;
    word = word | ((unsigned char)*ptra) << 8;
    ptra++;

    nextch = *ptra;
    if (ofs_inbyte != 0) {
        // shift nextch right by ofs_inbyte bits
        // and shift those bits right into word;
        word =
        (word >> ofs_inbyte) | (((unsigned)nextch) << (16 - ofs_inbyte));
    }
    return (word & masks[nbits]);
}  // rd_dcode()

static void init_dtab(void)
{
    nbits = 9;
    max_code = 512;
    free_code = FIRST_FREE;
}

static void wr_dchar(char ch)
{
    if (output_offset >= OUTBUFSIZ) {  // if buffer full
        if (out_han != -2) {
            if (write(out_han, out_buf_adr, output_offset) != output_offset) {
                die("Write error");
            }
        }
        addbfcrc(out_buf_adr, output_offset);  // update CRC
        output_offset = 0;                     // restore empty buffer
    }
    out_buf_adr[output_offset++] = ch;  // store character
}  // wr_dchar()

// adds a code to table
static void ad_dcode(void)
{
    table[free_code].z_ch = k;         // save suffix char
    table[free_code].next = old_code;  // save prefix code
    free_code++;
    if (free_code >= max_code) {
        if (nbits < MAXBITS) {
            nbits++;
            max_code = max_code << 1;  // double max_code
        }
    }
}
