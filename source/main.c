/*
 * SC-TapeWave
 *
 * A tool to generate SC-3000 tape audio.
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Note that the data in 8-bit wave files is unsigned. */
#define WAVE_HIGH_X4    "\xff\xff\xff\xff"
#define WAVE_LOW_X4     "\x00\x00\x00\x00"
#define WAVE_ZERO       "\x80"

typedef enum tape_mode_e
{
    MODE_INVALID = 0,
    MODE_SC_MACHINE_CODE,
    MODE_SC_BASIC
} tape_mode_t;

static tape_mode_t mode = MODE_INVALID;
static FILE *output_file = NULL;
static int8_t checksum = 0;


/*
 * Write a specified length of silence to the output file.
 */
static void write_silent_ms (uint32_t length)
{
    /* 19.2 samples per ms. */
    int samples = length * 192 / 10;

    for (int i = 0; i < samples; i++)
    {
        fwrite (WAVE_ZERO, 1, 1, output_file);
    }
}


/*
 * Write a single bit to the wave file.
 */
static void write_bit (bool value)
{
    /* Note: We have 16 samples per bit */
    if (value)
    {
        /* '1' */
        fwrite (WAVE_HIGH_X4 WAVE_LOW_X4 WAVE_HIGH_X4 WAVE_LOW_X4, 1, 16, output_file);
    }
    else
    {
        /* '0' */
        fwrite (WAVE_HIGH_X4 WAVE_HIGH_X4 WAVE_LOW_X4 WAVE_LOW_X4, 1, 16, output_file);
    }
}


/*
 * Write a byte to the wave file.
 */
static void write_byte (uint8_t byte)
{
    /* Start bit */
    write_bit (0);

    /* Data bits */
    for (int i = 0; i < 8; i++)
    {
        write_bit ((byte >> i) & 1);
    }

    /* Stop bits */
    write_bit (1);
    write_bit (1);

    checksum += byte;
}


/*
 * Write the tape to the wave file.
 */
static void write_tape (const char *name, uint16_t program_length, uint8_t *program, uint16_t start_address)
{
    int name_length = strlen (name);

    /* Write a short silent section. */
    write_silent_ms (10);

    /* Write the first leader field */
    for (int i = 0; i < 3600; i++)
    {
        write_bit (1);
    }

    /* Write the header's key-code */
    write_byte ((mode == MODE_SC_MACHINE_CODE) ? 0x26 : 0x16);
    checksum = 0;

    /* Write the file-name */
    for (int i = 0; i < 16; i++)
    {
        write_byte ((i < name_length) ? name [i] : ' ');
    }

    /* Write the program length */
    write_byte (program_length >> 8);
    write_byte (program_length & 0xff);

    /* Write the program's start-address */
    if (mode == MODE_SC_MACHINE_CODE)
    {
        write_byte (start_address >> 8);
        write_byte (start_address & 0xff);
    }

    /* Write the parity byte */
    write_byte (-checksum);

    /* Write two bytes of dummy data */
    write_byte (0x00);
    write_byte (0x00);

    /* One second of silence */
    write_silent_ms (1000);

    /* Write the second leader field */
    for (int i = 0; i < 3600; i++)
    {
        write_bit (1);
    }

    /* Write the code's key-code */
    write_byte ((mode == MODE_SC_MACHINE_CODE) ? 0x27 : 0x17);
    checksum = 0;

    /* Write the program */
    for (int i = 0; i < program_length; i++)
    {
        write_byte (program [i]);
    }

    /* Write the parity byte */
    write_byte (-checksum);

    /* Write two bytes of dummy data */
    write_byte (0x00);
    write_byte (0x00);

    /* Write a short silent section. */
    write_silent_ms (10);
}


/*
 * Entry point.
 *
 * Note that we assume a little-endian host.
 */
int main (int argc, char **argv)
{
    const uint32_t format_length            = 16;       /* Length of the format section in bytes */
    const uint16_t format_type              = 1;        /* PCM */
    const uint16_t format_channels          = 1;        /* Mono */
    const uint32_t format_sample_rate       = 19200;    /* 19.2 kHz, giving 16 samples per tape-bit */
    const uint32_t format_byte_rate         = 19200;    /* One byte per frame */
    const uint16_t format_block_align       = 1;        /* Frames are one-byte aligned */
    const uint16_t format_bits_per_sample   = 8;        /* 8-bit */

    /* 'riff_size' and 'data_size' store the number of bytes still to come,
     * counting from the first byte that comes after the size field itself. */
    uint32_t output_file_size = 0;
    uint32_t riff_size = 0;
    uint32_t data_size = 0;

    fpos_t riff_size_pos;
    fpos_t data_size_pos;

    const char *argv_0 = argv [0];
    uint32_t start_address = 0;

    /* Parameter parsing */
    if (argc == 5 && strcmp (argv [1], "--basic") == 0)
    {
        mode = MODE_SC_BASIC;
        argv++;
        argc--;

        fprintf (stderr, "Error: BASIC support not yet implemented.");
        return EXIT_FAILURE;

        /* BASIC programs aren't just plain-text, the following format is used for each line:
         *
         * byte [0] = line length
         * byte [1] = line number (LSB)
         * byte [2] = line number (MSB)
         * byte [3] = 0x00
         * byte [4] = 0x00
         * byte [5...n] = line contents
         * byte [n + 1] = '\r'
         *
         * BASIC keywords are also stored using a 1-2 byte code to save space.
         * When counting the line length, keywords only count as one byte. (what about 2-byte keywords?)
         */
    }
    else if (argc == 6 && strcmp (argv [1], "--machine-code") == 0)
    {
        mode = MODE_SC_MACHINE_CODE;
        start_address = strtol (argv [2], NULL, 16);
        argv += 2;
        argc -= 2;
    }

    /* If we have a start_address, check that it will fit in the tape's 16-bit start-address field */
    if (start_address > 0xffff)
    {
        fprintf (stderr, "Error: Start address '0x%x' is too high.\n", start_address);
        return EXIT_FAILURE;
    }

    if (mode == MODE_INVALID || argc != 4)
    {
        fprintf (stderr, "Usage: %s <--basic|--machine-code <start-address>> <name-on-tape> <input-file> <output-file.wav>\n", argv_0);
        return EXIT_FAILURE;
    }

    const char *tape_name =       argv [1];
    const char *input_filename =  argv [2];
    const char *output_filename = argv [3];

    /* Check for the .wav extension in the output filename */
    const char *output_extension = strrchr (output_filename, '.');
    if (output_extension == NULL || strlen(output_extension) != 4 ||
        tolower (output_extension [1]) != 'w' ||
        tolower (output_extension [2]) != 'a' ||
        tolower (output_extension [3]) != 'v')
    {
        fprintf (stderr, "Output file must have '.wav' extension.\n");
        return EXIT_FAILURE;
    }

    /* Open the input file and get its length */
    FILE *input_file = fopen (input_filename, "r");
    if (input_file == NULL)
    {
        fprintf (stderr, "Failed to open input file '%s'.\n", input_filename);
        return EXIT_FAILURE;
    }
    fseek (input_file, 0, SEEK_END);
    uint32_t program_length = ftell (input_file);
    fseek (input_file, 0, SEEK_SET);

    /* Check that it will fit in the tape's 16-bit length field */
    if (program_length > 65535)
    {
        fprintf (stderr, "Error: Program '%s' is too large.\n", input_filename);
        return EXIT_FAILURE;
    }

    /* Copy the input file into a buffer */
    uint8_t *program_buffer = calloc (program_length, 1);
    if (program_buffer == NULL)
    {
        fprintf (stderr, "Failed to allocate memory for input file '%s', size .\n", input_filename);
        return EXIT_FAILURE;
    }
    uint32_t bytes_read = 0;
    while (bytes_read < program_length)
    {
        bytes_read += fread (program_buffer + bytes_read, 1, program_length - bytes_read, input_file);
    }

    /* Open the output file */
    output_file = fopen (output_filename, "w");
    if (output_file == NULL)
    {
        fprintf (stderr, "Failed to open output file '%s'.\n", output_filename);
        return EXIT_FAILURE;
    }

    /* Write RIFF header */
    fwrite ("RIFF", 1, 4, output_file);
    fgetpos (output_file, &riff_size_pos);
    fwrite (&riff_size, 1, 4, output_file);
    fwrite ("WAVE", 1, 4, output_file);

    /* Write WAVE format */
    fwrite ("fmt ", 1, 4, output_file);
    fwrite (&format_length, 1, 4, output_file);
    fwrite (&format_type, 1, 2, output_file);
    fwrite (&format_channels, 1, 2, output_file);
    fwrite (&format_sample_rate, 1, 4, output_file);
    fwrite (&format_byte_rate, 1, 4, output_file);
    fwrite (&format_block_align, 1, 2, output_file);
    fwrite (&format_bits_per_sample, 1, 2, output_file);

    /* Write WAVE data */
    fwrite ("data", 1, 4, output_file);
    fgetpos (output_file, &data_size_pos);
    fwrite (&data_size, 1, 4, output_file);
    write_tape (tape_name, program_length, program_buffer, start_address);

    /* Get size */
    output_file_size = ftell (output_file);
    riff_size = output_file_size - 8;
    data_size = output_file_size - 44;

    /* Populate size fields in wave file */
    fsetpos (output_file, &riff_size_pos);
    fwrite (&riff_size, 1, 4, output_file);
    fsetpos (output_file, &data_size_pos);
    fwrite (&data_size, 1, 4, output_file);

    fclose (output_file);

    return EXIT_SUCCESS;
}
