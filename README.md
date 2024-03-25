
# SC-TapeWave
SC-TapeWave is a tool for generating tape audio for the SC-3000 micro-computer.

Usage: `./tapewave "Program Name" <input_file.bin> <output_file.wav>`

## Loading

Use the `LOAD` command from BASIC.

BASIC IIIa and BASIC IIIb both load the program to address `0x9800`.
To run the program, you can use a CALL command such as `CALL &H9800`
