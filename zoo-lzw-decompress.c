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
#define BUFFER_SIZE 1024

// Decompression stack. Except in pathological cases, 2000 bytes
// should be enough. Rare files may need a bigger stack to decompress.
// May be decreased to 1000 bytes if memory is tight.
#define MAX_STACK 2000  // adjust to conserve memory

#define CODE_BITS 13
#define CODE_LIMIT 8192

#define CLEAR_CODE 256
#define EOF_CODE 257
#define FIRST_FREE_CODE 258

struct table_entry {
    unsigned int next;
    char suffix_char;
};

static struct table_entry table[CODE_LIMIT];

static unsigned int nstack;
static unsigned int stack[MAX_STACK];

static char ibuf[BUFFER_SIZE];
static char obuf[BUFFER_SIZE];

static unsigned int cur_code;
static unsigned int old_code;
static unsigned int in_code;

static unsigned int next_free_code;
static int nbits;
static unsigned int max_code;

static char fin_char;
static char k;
static unsigned int masks[CODE_BITS + 1] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff
};
static unsigned int bit_offset;
static unsigned int output_offset;

static void die(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void write_decompressed_byte(char ch)
{
    if (output_offset >= BUFFER_SIZE) {
        if (write(STDOUT_FILENO, obuf, output_offset) != output_offset) {
            die("Write error");
        }
        // addbfcrc(obuf, output_offset);  // update CRC
        output_offset = 0;
    }
    obuf[output_offset++] = ch;
}

static void table_clear(void)
{
    nbits = 9;
    max_code = 512;
    next_free_code = FIRST_FREE_CODE;
}

static void table_add_code(void)
{
    table[next_free_code].suffix_char = k;
    table[next_free_code].next = old_code;  // save prefix code
    next_free_code++;
    if (next_free_code >= max_code) {
        if (nbits < CODE_BITS) {
            nbits++;
            max_code *= 2;
        }
    }
}

static void stack_push(int ch)
{
    stack[nstack++] = ch;
    if (nstack >= MAX_STACK) {
        die("Stack overflow");
    }
}

static unsigned int stack_pop(void) { return stack[--nstack]; }

static unsigned int decode_uint16le(void *bytes_void)
{
    unsigned char *bytes = bytes_void;

    return bytes[0] | (bytes[1] << 8);
}

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

    if (byte_offset >= BUFFER_SIZE) {
        int space_left;

        bit_offset = ofs_inbyte + nbits;
        space_left = BUFFER_SIZE - byte_offset;
        ptrb = byte_offset + ibuf;  // point to char
        ptra = ibuf;
        // we now move the remaining characters down buffer beginning
        while (space_left > 0) {
            *ptra++ = *ptrb++;
            space_left--;
        }
        if (read(STDIN_FILENO, ptra, byte_offset) == -1) {
            die("Read error");
        }
        byte_offset = 0;
    }
    ptra = byte_offset + ibuf;
    word = decode_uint16le(ptra);
    ptra += 2;
    nextch = *ptra;
    if (ofs_inbyte) {
        // shift nextch right by ofs_inbyte bits
        // and shift those bits right into word;
        word =
        (word >> ofs_inbyte) | (((unsigned)nextch) << (16 - ofs_inbyte));
    }
    return (word & masks[nbits]);
}

static void decompress(void)
{
    table_clear();

    if (read(STDIN_FILENO, ibuf, BUFFER_SIZE) == -1) {
        die("Read error");
    }

loop:
    cur_code = rd_dcode();
    if (cur_code == EOF_CODE) {
        if (output_offset) {
            if (write(STDOUT_FILENO, obuf, output_offset) != output_offset) {
                die("Write error");
            }
            // addbfcrc(obuf, output_offset);
        }
        return;
    }

    if (cur_code == CLEAR_CODE) {
        table_clear();
        fin_char = k = old_code = cur_code = rd_dcode();
        write_decompressed_byte(k);
        goto loop;
    }

    in_code = cur_code;
    if (cur_code >= next_free_code) {  // if code not in table (k<w>k<w>k)
        cur_code = old_code;           // previous code becomes current
        stack_push(fin_char);
    }

    while (cur_code > 255) {  // if code, not character
        stack_push(table[cur_code].suffix_char);
        cur_code = table[cur_code].next;  // <w> := <w>.code
    }

    k = fin_char = cur_code;
    stack_push(k);
    while (nstack) {
        write_decompressed_byte(stack_pop());
    }
    table_add_code();
    old_code = in_code;

    goto loop;
}

int main(void)
{
    decompress();
    return 0;
}
