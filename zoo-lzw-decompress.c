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

static unsigned int lzd_sp;
static unsigned int lzd_stack[MAX_STACK];

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

static void wr_dchar(char ch)
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

static void clear_table(void)
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

static void push(int ch)
{
    lzd_stack[lzd_sp++] = ch;
    if (lzd_sp >= MAX_STACK) {
        die("Stack overflow");
    }
}

static unsigned int pop(void) { return lzd_stack[--lzd_sp]; }

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
    // NOTE:  "word = *((int *) ptra)" would not be independent of byte order.

    word = (unsigned char)*ptra;
    ptra++;
    word = word | ((unsigned char)*ptra) << 8;
    ptra++;

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
    clear_table();

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
        clear_table();
        fin_char = k = old_code = cur_code = rd_dcode();
        wr_dchar(k);
        goto loop;
    }

    in_code = cur_code;
    if (cur_code >= next_free_code) {  // if code not in table (k<w>k<w>k)
        cur_code = old_code;           // previous code becomes current
        push(fin_char);
    }

    while (cur_code > 255) {  // if code, not character
        push(table[cur_code].suffix_char);
        cur_code = table[cur_code].next;  // <w> := <w>.code
    }

    k = fin_char = cur_code;
    push(k);
    while (lzd_sp) {
        wr_dchar(pop());
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
