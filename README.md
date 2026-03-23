# Teltonika_Speed_Test_Program

The program is supposed to be executed on Linux.

If curl isn't installed, run: sudo apt install libcurl4-openssl-dev

Then run: make

Then run program: ./program -n _ -x _

Usage:

The program expects at least one argument (1-4 after -n)

1 - Determine a location (country)

2 - Perform a download speed test (specify a server number)

3 - Perform an upload speed test (specify a server number)

4 - Perform a full test based on locaation

Examples:

./program -n 1

./program -n 2 -x 5420

./program -n 3 -x 9622

./program -n 4
