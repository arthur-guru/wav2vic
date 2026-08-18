/*
BSD License for wav2vic
Copyright (c) 2006-2026, arthurguru
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
- Neither the name of arthurguru nor the names of its contributors   
  may be used to endorse or promote products derived from this software
  without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


  Vic-20 C2N tape format
  1. Sync signal
  2. Header block
  3. Small sync signal
  4. Header block repeated again
  5. Medium sync signal 
  6. Data block
  7. Small sync signal
  8. Data block repeated again
  data = 1 start bit, 8 data bits (0-7), 1 stop/parity bit

  Waveform details
  ================
  equal (or near equal) waves = sync signal
  large wave followed by a small wave = bit value 1
  small wave followed by a large wave = bit value 0
  very small waves = noise
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PROGNAME "WAV2VIC"
#define VERSION  "1.0"
#define AUTHOR   "arthurguru"
#define YEAR     "2006"

#define DEBUG 0       // 0 = off, 1 = low, 2 = high

#define WAVEZERO 127  // for 8 bit resolution half way is 127
#define WAVEFILE_HEADER_LENGTH 44  // Keeping it simple
#define THRESHOLD 15  // difference b/w similar of different size waves.

#define VIC20_HEADER_LENGTH 202 
#define MAX_BLOCK_SIZE 16384 

// if errors found in in_block1, try salvage from in_block2
struct tapeblock
{
  unsigned char c[MAX_BLOCK_SIZE];
  int           length;
  unsigned int  data_length;
  int           error_parity;
  int           error_header;
  unsigned char error_checksum;
  int           block_ok;
} in_block[4];
int in_block_cnt = 0;
int in_block_seq = 0;

char *inputfile;
int debug, wavezero, wavefile_header_length, threshold;

int get_options(int argc, char **argv)
{
  int i, n, err;

  // Set defaults
  err = 0;
  debug = DEBUG;
  wavefile_header_length = WAVEFILE_HEADER_LENGTH;
  threshold = THRESHOLD;

  for (i = 1 ; i < argc ; i++)
  {
    // DEBUG switch
    if (strcmp(argv[i], "-d") == 0 && i < argc - 1)
    {
      if (n = atoi(argv[i + 1]))
        debug = n;
      else
        err++;
    }
        
    // WAVEFILE_HEADER_LENGTH switch
    if (strcmp(argv[i], "-h") == 0 && i < argc - 1)
    {
      if (n = atoi(argv[i + 1]))
        wavefile_header_length = n;
      else
        err++;
    }
    
    // THRESHOLD switch
    if (strcmp(argv[i], "-t") == 0 && i < argc - 1)
    {
      if (n = atoi(argv[i + 1]))
        threshold = n;
      else
        err++;
    }
  }       

  if (argc > 1)
    inputfile = argv[argc - 1];
  else
    err++;

  if (debug)
    printf("err=%d, argc=%d, debug=%d, wavefile_header_length=%d, threshold=%d, inputfile=%s\n", 
            err, argc , debug, wavefile_header_length, threshold, inputfile);

  return (err);
}

usage()
{
  printf("Usage: %s [-d x] [-h y] [-t z] file.wav\n", PROGNAME);
  printf("Where\n");
  printf("  -d turns on debug mode, values 1 (low) or 2 (high), default %d (none)\n", DEBUG);
  printf("  -h skips wavefile header by y bytes, default is %d\n", WAVEFILE_HEADER_LENGTH);
  printf("  -t adjusts threshold limit between big and small wave pairs, default is %d\n", THRESHOLD);
  printf("  file.wav is an 8 bit, 22Khz, Mono wave file containing a recording of\n");
  printf("           your old Commodore Vic-20 C2N tapes.\n");
  printf("Please read the documentation for more information.\n\n");
}

void init_in_blocks()
{
  int i, j;

  for (i = 0 ; i < 4 ; i++)
  {
    for (j = 0 ; j < MAX_BLOCK_SIZE ; j++)
      in_block[i].c[j] = 0;
    in_block[i].length = 0;
    in_block[i].data_length = 0;
    in_block[i].error_parity = 0;
    in_block[i].error_header = 0;
    in_block[i].error_checksum = 0;
    in_block[i].block_ok = 0;
  }
}

void print_vic20_file()
{
  unsigned char hdr1[9], hdr2[9], checksum;
  char hdr_type[5][30];
  int in_block_checksum = 0;
  int i, j, k, l;
  FILE *fpo;
  char fileprefix[30];
  char outputfile[30];

  if (in_block_cnt == 0) return;

  in_block_seq++;

  strcpy(hdr_type[0], "Relocatable basic program");
  strcpy(hdr_type[1], "Data block of data file");
  strcpy(hdr_type[2], "Non-relocatable basic program");
  strcpy(hdr_type[3], "Data file header");
  strcpy(hdr_type[4], "End of tape");

  for (i = 0; i < 9; i++)
  {
    hdr1[i] = 137 - i; // First 9 bytes of header 1
    hdr2[i] =   9 - i; // First 9 bytes of header 2
  }

  if (in_block_cnt != 4) // good set to make a file from: 2xheader + 2xdata
  {
    printf("WARNING: Number of remaining blocks in file %s is: %d\n", inputfile, in_block_cnt);
    printf("This is not quite right, expecting 4.\n");
  }

  if (strlen(inputfile) == 0);
  {
    strcpy(fileprefix, "FILE1\0");
    i = strlen(fileprefix);
  }
  for (i = 0 ; i < (int) strlen(inputfile) && i < 20 && inputfile[i] != '.' ; i++ )
  {
    fileprefix[i] = inputfile[i];
    fileprefix[i + 1] = '\0';
  }

  for (i = 0 ; i < in_block_cnt ; i++)
  {
    checksum = 0;
    // Check for header errors
    for (j = 0 ; j < in_block[i].length - 1 ; j++)
    {
      if (j < 9)
      {
        if (i == 0 || i == 2)
        {
          if (in_block[i].c[j] != hdr1[j])
            in_block[i].error_header++;
        }
        else
        {
          if (in_block[i].c[j] != hdr2[j])
            in_block[i].error_header++;
        }
      }
      else
      {
        // Calculate block checksum
        checksum ^= in_block[i].c[j];
      }
    }
    if (checksum != in_block[i].c[j])
      in_block[i].error_checksum = 1;

    in_block[i].data_length =  in_block[i].c[12] | in_block[i].c[13] << 8;
    in_block[i].data_length -= in_block[i].c[10] | in_block[i].c[11] << 8;
    in_block[i].data_length &= 0xffff;

    if (in_block[i].error_parity + in_block[i].error_header + in_block[i].error_checksum == 0)
      if (i < 2)
      {
        if (in_block[i].length == VIC20_HEADER_LENGTH && in_block[i].c[9] < 5)
          in_block[i].block_ok = 1;
      }
      else
        in_block[i].block_ok = 1;

    printf("\nFile %s, Sequence %d, Block %d\n", inputfile, in_block_seq, i + 1);
    printf("  Total block bytes: %d (Data %d)\n", in_block[i].length, in_block[i].length - 10);
    if (in_block[i].block_ok == 0)
    {
      printf("  Parity errors: %d\n", in_block[i].error_parity);
      printf("  Block header error: %d\n", in_block[i].error_header);
      printf("  Block checksum error: %d\n", in_block[i].error_checksum);
    }

    if (i < 2)
    {
      if (in_block[i].block_ok)
      {
        printf("  Header type: %d (%s)\n", in_block[i].c[9], hdr_type[in_block[i].c[9]]);
        printf("  Data length value in header block: %d\n", in_block[i].data_length);
        printf("  Data file name in header block: \"");
        for (j = 14 ; j < in_block[i].length - 1 ; j++) 
        {
          if(in_block[i].c[j] >= ' ' && in_block[i].c[j] <= 'z')
          {
            // truncate multiple spaces
            if (in_block[i].c[j] != ' ' && in_block[i].c[j - 1] != ' ')
              printf("%c", in_block[i].c[j]);
          }
        }
        printf("\"\n");
      }
      else
        printf("  Potential corruption in header block.\n");
    }
    
    // Create output file
    sprintf(outputfile,"%s_F%dB%d.RAW", fileprefix, in_block_seq, i + 1);
    if ((fpo = fopen(outputfile,"w")) != NULL)
    {
      for (j = 9 ; j < in_block[i].length - 1; j++)
      {
        fprintf(fpo, "%c", in_block[i].c[j]);
      }
      fclose(fpo);
      printf("  Block %d written to file: %s\n", i, outputfile);
    }
    else
      printf("ERROR: Cannot create output file: %s\n", outputfile);
  }
  
  // Construct a Vic-20 .PRG file as best as possible.
  if ((in_block[0].block_ok || in_block[1].block_ok) && (in_block[2].block_ok || in_block[3].block_ok))
  {
    sprintf(outputfile,"%s_F%d.PRG", fileprefix, in_block_seq);
    if ((fpo = fopen(outputfile,"w")) != NULL)
    {
      if (in_block[0].block_ok)
        k = 0;
      else 
        k = 1;

      if (in_block[2].block_ok)
        l = 2;
      else 
        l = 3;

      // Print loading address from header for .PRG format
      fprintf(fpo, "%c", in_block[k].c[10]);
      fprintf(fpo, "%c", in_block[k].c[11]);
      for (j = 9 ; j < in_block[l].length - 1; j++)
      {
        fprintf(fpo, "%c", in_block[l].c[j]);
      }
      fclose(fpo);
      printf("\n------------------------------------------------\n");
      printf("Vic-20 Program written to file: %s\n", outputfile);
      printf("------------------------------------------------\n");
    }
    else
      printf("ERROR: Cannot create output file: %s\n", outputfile);

  }
  else
  {
    printf("\nERROR: Failed to make a Vic-20 program out of sequence %d blocks.\n", in_block_seq);
    printf("Please examine blocklettes <file>_F%dB1,2,3,4.RAW for salvaging.\n", in_block_seq);
  }

  // get ready for a new set.
  init_in_blocks();
}

int main(int argc, char *argv[])
{
  FILE *fpi;
  int c, c_prev;
  unsigned char mychar;
  int peak_max, peak_min;
  int i; 
  int bit, bit_stream, startbit_check, parity_check;
  int sync_signal; 
  int wave1, wave2;
 
  mychar = 0;
  bit = bit_stream = startbit_check = parity_check = 0;
  sync_signal = 0;
  wave1 = wave2 = 0;
  c_prev = peak_max = peak_min = WAVEZERO;

  // Print version string
  printf("%s %s - %s %s\n", PROGNAME, VERSION, AUTHOR, YEAR);

  // Validate options
  if (get_options(argc, argv))
  {
    usage();
    return(2);
  }

  init_in_blocks();

  if ((fpi = fopen(inputfile,"r")) != NULL)
  {
    if (debug) printf("\nLegend: -=noise, s=sync, 1=bit on, 0=bit off\n\n");
    
    // Ignore the initial bytes of .wav header
    // Potentially there is useful info here which I'm ignoring
    for (i = 0 ; i < wavefile_header_length && (c = fgetc(fpi)) != EOF ; i++)
      if (debug > 1) printf("Byte %3.3d: %3.3d = <wavefile header>\n", i + 1, (unsigned char) c);

    // Now for the real data...
    //while ((c = fgetc(fpi)) != EOF)
    while (fread(&c, 1, 1, fpi) > 0)
    {
      if (c > c_prev) // only measure the rising part of the wave
      {
        if (c > peak_max) peak_max = c;
        if (c < peak_min) peak_min = c_prev;
      }
      else
      {
        if (!wave1) 
        {
          wave1 = peak_max - peak_min;
          peak_max = peak_min = WAVEZERO;
        }
        else if (!wave2) 
        {
          wave2 = peak_max - peak_min;
        }
      }
      c_prev = c;

      if (wave1 && wave2)
      {
        if (debug > 1) printf("\n<%3.3d:%3.3d:%+3.3d> = ", wave1, wave2, wave1 - wave2);  

        if (wave1 < 5) // ignore noise
        {
          if (debug) printf("-"); // - for noise/preamble
          // shift wave
          wave1 = wave2; 
          wave2 = 0;
          peak_max = peak_min = WAVEZERO;
        }
        else if (sync_signal && wave2 - wave1 > threshold) // sync may pair with data
        {
          // shift wave
          wave1 = wave2; 
          wave2 = 0;
          peak_max = peak_min = WAVEZERO;
        }
      }

      if (wave1 && wave2)
      {
        if (abs(wave1 - wave2) < threshold)
        {
          sync_signal++;
          bit_stream = mychar = 0;
          if (debug) printf("s"); // s for sync
        }
        else if (wave2 > wave1) 
        {
          bit=0;
          bit_stream++;    
          sync_signal = 0;
          if (debug) printf("0");
        }
        else if (wave2 < wave1)
        {
          bit=1;
          bit_stream++;    
          sync_signal = 0;
          if (debug) printf("1");
        }
        
        if (bit_stream == 1) // 1st bit = start bit should always be 1 unless sync signal
        {
          startbit_check = bit;
        }
        else if (bit_stream == 2) // 2nd bit = d0
        {
          if (bit) 
          {
             mychar += 1; 
             parity_check++;
          }
        }
        else if (bit_stream == 3) // 3rd bit = d1
        {
          if (bit)
          {
             mychar += 2; 
             parity_check++;
          }
        }
        else if (bit_stream == 4) // 4th bit = d2
        {
          if (bit)
          {
             mychar += 4; 
             parity_check++;
          }
        }
        else if (bit_stream == 5) // 5th bit = d3
        {
          if (bit)
          {
             mychar += 8; 
             parity_check++;
          }
        }
        else if (bit_stream == 6) // 6th bit = d4
        {
          if (bit)
          {
             mychar += 16; 
             parity_check++;
          }
        }
        else if (bit_stream == 7) // 7th bit = d5
        {
          if (bit)
          {
             mychar += 32; 
             parity_check++;
          }
        }
        else if (bit_stream == 8) // 8th bit = d6
        {
          if (bit)
          {
             mychar += 64; 
             parity_check++;
          }
        }
        else if (bit_stream == 9) // 9th bit = d7
        {
          if (bit)
          {
             mychar += 128; 
             parity_check++;
          }
        }
        else if (bit_stream == 10) // 10th bit = stop bit plus parity
        {
          // Only print properly formed bytes
          if (startbit_check)
          {
            //Even number of data bits - parity bit = 1
            //Odd  number of data bits - parity bit = 0
            if ((parity_check % 2) == 0 && bit || \
                (parity_check % 2) != 0 && !bit) 
            {
              if (debug == 1)
              {
                printf(" [%3.3d] ", (unsigned char) mychar); 
                if (mychar >= ' ' && mychar <= 'z') 
                  printf("'%c'", mychar);
                else
                  printf("'?'");
                printf(" <Ok>\n"); 
              }
              else
              {
                //printf("%c", mychar); 
                in_block[in_block_cnt].c[in_block[in_block_cnt].length++] = mychar;
              }
            }
            else
            {
              if (debug == 1) printf("<Paritybit error!%d>\n", parity_check % 2);
              in_block[in_block_cnt].error_parity++;
            }
          }
          else
            if (debug == 1) printf("<Startbit error!>\n");

          mychar = 0; 
          bit_stream = 0;
          startbit_check = 0;
          parity_check = 0;
        }
        wave1 = wave2 = 0;
        peak_max = peak_min = WAVEZERO;
        if (sync_signal && in_block[in_block_cnt].length > 0)
        {
          in_block_cnt++;
        }
        if (in_block_cnt > 3) 
        {
          print_vic20_file();
          in_block_cnt = 0;
        }
      }
    }  
    if (debug) printf("\nEnd-Of-File\n");
    fclose(fpi);
  }  
  else
  {
    printf("ERROR: Cannot open input file %d\n", inputfile);
    usage();
    return(2);
  }

  // Print any remaining blocks
  print_vic20_file();

  return 0;
}
