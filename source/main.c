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

/* Note that samples in 8-bit wave files are unsigned. */
#define WAVE_ZERO       "\xff\xff\xff\xff\x00\x00\x00\x00"
#define WAVE_ONE        "\xff\xff\x00\x00\xff\xff\x00\x00"
#define WAVE_SILENT     "\x80"

static FILE *output_file = NULL;
static int8_t checksum = 0;


/*
 * Write a specified length of silence to the output file.
 */
static void write_silent_ms (uint32_t length)
{
    /* 9.6 samples per ms. */
    int samples = length * 96 / 10;

    for (int i = 0; i < samples; i++)
    {
        fwrite (WAVE_SILENT, 1, 1, output_file);
    }
}


/*
 * Write a single bit to the wave file.
 */
static void write_bit (bool bit)
{
    /* Note: We have 8 samples per bit */
    fwrite (bit ? WAVE_ONE : WAVE_ZERO, 1, 8, output_file);
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
static void write_tape (const char *name, uint16_t program_length, uint8_t *program)
{
    int name_length = strlen (name);

    /* Write a short silent section. */
    write_silent_ms (10);

    /* Write the first leader field */
    for (int i = 0; i < 3600; i++)
    {
        write_bit (1);
    }

    /* Write the header key-code */
    write_byte (0x16);
    checksum = 0;

    /* Write the file-name */
    for (int i = 0; i < 16; i++)
    {
        write_byte ((i < name_length) ? name [i] : ' ');
    }

    /* Write the program length */
    /* TODO: Confirm byte order - In the scanned document, pencil and ink disagree. */
    write_byte (program_length >> 8);
    write_byte (program_length & 0xff);

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

    /* Write the program key-code */
    write_byte (0x17);
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
    const uint32_t format_sample_rate       = 9600;    /* 9.6 kHz, giving 8 samples per tape-bit */
    const uint32_t format_byte_rate         = 9600;    /* One byte per frame */
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

    /* Check parameters */
    if (argc != 4)
    {
        fprintf (stderr, "Usage: %s <name-on-tape> <input-file> <output-file.wav>\n", argv_0);
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
    write_tape (tape_name, program_length, program_buffer);

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
