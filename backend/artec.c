/* sane - Scanner Access Now Easy.
   Copyright (C) 1997 David Mosberger-Tang
   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.

   As a special exception, the authors of SANE give permission for
   additional uses of the libraries contained in this release of SANE.

   The exception is that, if you link a SANE library with other files
   to produce an executable, this does not by itself cause the
   resulting executable to be covered by the GNU General Public
   License.  Your use of that executable is in no way restricted on
   account of linking the SANE library code into it.

   This exception does not, however, invalidate any other reasons why
   the executable file might be covered by the GNU General Public
   License.

   If you submit changes to SANE to the maintainers to be included in
   a subsequent release, you agree by submitting the changes that
   those changes may be distributed with this exception intact.

   If you write modifications of your own for SANE, it is your choice
   whether to permit this exception to apply to your modifications.
   If you do not wish that, delete this exception notice.

   This file implements a SANE backend for the Artec/Ultima scanners.

   Copyright (C) 1998,1999 Chris Pinkham
   Released under the terms of the GPL.
   *NO WARRANTY*

   Portions contributed by:
     David Leadbetter - A6000C (3-pass)
     Dick Bruijn - AT12

   *********************************************************************
   For feedback/information:

   cpinkham@infi.net
   http://www4.infi.net/~cpinkham/sane/sane-artec-doc.html
   *********************************************************************
 */

#include <sane/config.h>

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <sane/sane.h>
#include <sane/saneopts.h>
#include <sane/sanei_scsi.h>
#include <sane/sanei_backend.h>
#include <sane/sanei_config.h>

#include <artec.h>

#define BACKEND_NAME    artec

#define ARTEC_MAJOR     0
#define ARTEC_MINOR     5
#define ARTEC_LAST_MOD  "10/30/1999 01:11 EST"

#define MM_PER_INCH	25.4

#ifndef PATH_MAX
#define PATH_MAX	1024
#endif

#define ARTEC_CONFIG_FILE "artec.conf"
#define ARTEC_MAX_READ_SIZE 32768


static int num_devices;
static ARTEC_Device *first_dev;
static ARTEC_Scanner *first_handle;

static const SANE_String_Const mode_list[] =
{
  "Lineart", "Halftone", "Gray", "Color",
  0
};

static const SANE_String_Const filter_type_list[] =
{
  "Mono", "Red", "Green", "Blue",
  0
};

static const SANE_String_Const halftone_pattern_list[] =
{
  "User defined (unsupported)", "4x4 Spiral", "4x4 Bayer", "8x8 Spiral",
  "8x8 Bayer",
  0
};

static const SANE_Range u8_range =
{
  0,				/* minimum */
  255,				/* maximum */
  0				/* quantization */
};

#define INQ_LEN	0x60
static const u_int8_t inquiry[] =
{
  0x12, 0x00, 0x00, 0x00, INQ_LEN, 0x00
};

static const u_int8_t test_unit_ready[] =
{
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

struct
  {
    SANE_String model;		/* product model */
    SANE_String type;		/* type of scanner */
    double width;		/* width in inches */
    double height;		/* height in inches */
    SANE_Word contrast_min;	/* minimum contrast */
    SANE_Word contrast_max;	/* maximum contrast */
    SANE_Word threshold_min;	/* minimum threshold */
    SANE_Word threshold_max;	/* maximum threshold */
    SANE_Word brightness_min;	/* minimum brightness */
    SANE_Word brightness_max;	/* maximum brightness */
    SANE_Word adc_bits;		/* Analog-to-Digital Converter Bits */
    SANE_Word setwindow_cmd_size;	/* Set-Window command size */
    SANE_Word max_read_size;	/* Max Read size in bytes */
    long flags;			/* flags */
    SANE_String horz_resolution_str;	/* Horizontal resolution list */
    SANE_String vert_resolution_str;	/* Vertical resolution list */
  }
cap_data[] =
{
  {
    "AT3", "flatbed",
      8.3, 11, 0, 255, 0, 255, 0, 0, 8, 55, 32768,
      ARTEC_FLAG_CALIBRATE_RGB |
      ARTEC_FLAG_RGB_LINE_OFFSET |
      ARTEC_FLAG_RGB_CHAR_SHIFT |
      ARTEC_FLAG_GAMMA_SINGLE |
      ARTEC_FLAG_SEPARATE_RES |
      ARTEC_FLAG_SENSE_HANDLER |
      ARTEC_FLAG_ONE_PASS_SCANNER,
      "50,100,200,300", "50,100,200,300,600"
  }
  ,
  {
    "A6000C", "flatbed",
      8.3, 14, 0, 255, 0, 255, 0, 0, 8, 55, 8192,
/* some have reported that Calibration does not work the same as AT3 & A6000C+
   ARTEC_FLAG_CALIBRATE_RGB |
 */
      ARTEC_FLAG_SEPARATE_RES |
      ARTEC_FLAG_RGB_LINE_OFFSET |
      ARTEC_FLAG_SENSE_HANDLER |
      ARTEC_FLAG_MONO_ADJUST,
      "50,100,200,300", "50,100,200,300,600"
  }
  ,
  {
    "A6000C PLUS", "flatbed",
      8.3, 14, 0, 255, 0, 255, 0, 0, 8, 55, 8192,
      ARTEC_FLAG_CALIBRATE_RGB |
      ARTEC_FLAG_RGB_LINE_OFFSET |
      ARTEC_FLAG_RGB_CHAR_SHIFT |
      ARTEC_FLAG_GAMMA_SINGLE |
      ARTEC_FLAG_SEPARATE_RES |
      ARTEC_FLAG_SENSE_HANDLER |
      ARTEC_FLAG_ONE_PASS_SCANNER,
      "50,100,200,300", "50,100,200,300,600"
  }
  ,
  {
    "AT6", "flatbed",
      8.3, 11, 0, 255, 0, 255, 0, 0, 10, 55, 32768,
      ARTEC_FLAG_CALIBRATE_RGB |
      ARTEC_FLAG_RGB_LINE_OFFSET |
      ARTEC_FLAG_RGB_CHAR_SHIFT |
/* gamma not working totally correct yet.
   ARTEC_FLAG_GAMMA_SINGLE |
 */
      ARTEC_FLAG_SEPARATE_RES |
      ARTEC_FLAG_SENSE_HANDLER |
      ARTEC_FLAG_ONE_PASS_SCANNER,
      "50,100,200,300", "50,100,200,300,600"
  }
  ,
  {
    "AT12", "flatbed",
      8.5, 11, 0, 255, 0, 255, 0, 255, 12, 67, 32768,
/* no brightness working yet
   ARTEC_CALIBRATE_DARK_WHITE |
 */
/* calibration works slower so disabled
   ARTEC_FLAG_OPT_BRIGHTNESS |
 */
/* gamma not working totally correct yet.
   ARTEC_FLAG_GAMMA |
 */
      ARTEC_FLAG_SEPARATE_RES |
      ARTEC_FLAG_SENSE_HANDLER |
      ARTEC_FLAG_ONE_PASS_SCANNER,
      "25,50,100,200,300,400,500,600",
      "25,50,100,200,300,400,500,600,700,800,900,1000,1100,1200"
  }
  ,
  {
    "AM12S", "flatbed",
      8.26, 11.7, 0, 255, 0, 255, 0, 255, 12, 67, 32768,
/* no brightness working yet
   ARTEC_CALIBRATE_DARK_WHITE |
 */
/* calibration works slower so disabled
   ARTEC_FLAG_OPT_BRIGHTNESS |
 */
/* gamma not working totally correct yet.
   ARTEC_FLAG_GAMMA |
 */
      ARTEC_FLAG_SEPARATE_RES |
      ARTEC_FLAG_IMAGE_REV_LR |
      ARTEC_FLAG_SENSE_HANDLER |
      ARTEC_FLAG_ONE_PASS_SCANNER,
      "25,50,100,200,300,400,500,600",
      "25,50,100,200,300,400,500,600,700,800,900,1000,1100,1200"
  }
  ,
};

static char artec_vendor[9] = "";	/* store vendor if hardcoded in artec.conf */
static char artec_model[17] = "";	/* store model if hardcoded in artec.conf */


static SANE_Status
artec_str_list_to_word_list (SANE_Word ** word_list_ptr, SANE_String str)
{
  SANE_Word *word_list;
  char *start;
  char *end;
  char temp_str[1024];
  int comma_count = 1;

  if ((str == NULL) ||
      (strlen (str) == 0))
    {
      /* alloc space for word which stores length (0 in this case) */
      word_list = (SANE_Word *) malloc (sizeof (SANE_Word));
      if (word_list == NULL)
	return (SANE_STATUS_NO_MEM);

      word_list[0] = 0;
      *word_list_ptr = word_list;
      return (SANE_STATUS_GOOD);
    }

  /* make temp copy of input string (only hold 1024 for now) */
  strncpy (temp_str, str, 1023);
  temp_str[1023] = '\0';

  end = strchr (temp_str, ',');
  while (end != NULL)
    {
      comma_count++;
      start = end + 1;
      end = strchr (start, ',');
    }

  word_list = (SANE_Word *) calloc (comma_count + 1,
				    sizeof (SANE_Word));

  if (word_list == NULL)
    return (SANE_STATUS_NO_MEM);

  word_list[0] = comma_count;

  comma_count = 1;
  start = temp_str;
  end = strchr (temp_str, ',');
  while (end != NULL)
    {
      *end = '\0';
      word_list[comma_count] = atol (start);

      start = end + 1;
      comma_count++;
      end = strchr (start, ',');
    }

  word_list[comma_count] = atol (start);

  *word_list_ptr = word_list;
  return (SANE_STATUS_GOOD);
}

static size_t
artec_get_str_index (const SANE_String_Const strings[], char *str)
{
  size_t index;

  index = 0;
  while ((strings[index]) && strcmp (strings[index], str))
    {
      index++;
    }

  if (!strings[index])
    {
      index = 0;
    }

  return (index);
}

static size_t
max_string_size (const SANE_String_Const strings[])
{
  size_t size, max_size = 0;
  int i;

  for (i = 0; strings[i]; ++i)
    {
      size = strlen (strings[i]) + 1;
      if (size > max_size)
	max_size = size;
    }

  return (max_size);
}

/* DB added a sense handler */

static SANE_Status
sense_handler (int fd, u_char * sense, void *arg)
{
  int s;

  s = 0;
  if (sense[18] & 0x01)
    {
      DBG (2, "sense:  ADF PAPER JAM\n");
      s++;
    }
  if (sense[18] & 0x02)
    {
      DBG (2, "sense:  ADF NO DOCUMENT IN BIN\n");
      s++;
    }
  if (sense[18] & 0x04)
    {
      DBG (2, "sense:  ADF SWITCH COVER OPEN\n");
      s++;
    }
  /* DB : next is, i think no failure, so no incrementing s */
  if (sense[18] & 0x08)
    {
      DBG (2, "sense:  ADF SET CORRECTLY ON TARGET\n");
    }
  /* The following only for AT12, its reserved (zero?) on other models,  */
  if (sense[18] & 0x10)
    {
      DBG (2, "sense:  ADF LENGTH TOO SHORT\n");
      s++;
    }
  if (sense[18] & 0x20)
    {
      DBG (2, "sense:  LAMP FAIL : NOT WARM \n");
      s++;
    }
  if (sense[18] & 0x40)
    {
      DBG (2, "sense:  NOT READY STATE\n");
      s++;
    }
  /* --- */
  /* The following not for AT12, its reserved so should not give problems */
  if (sense[19] & 0x01)
    {
      DBG (2, "sense:  ROM ERROR\n");
      s++;
    }
  if (sense[19] & 0x02)
    {
      DBG (2, "sense:  CPU RAM ERROR\n");
      s++;
    }
  if (sense[19] & 0x04)
    {
      DBG (2, "sense:  SHAD.CORR.RAM W/R ERROR\n");
      s++;
    }
  if (sense[19] & 0x08)
    {
      DBG (2, "sense:  LINE RAM W/R ERROR\n");
      s++;
    }
  if (sense[19] & 0x10)
    {
      DBG (2, "sense:  CCD CONTR.CIRCUIT ERROR\n");
      s++;
    }
  if (sense[19] & 0x20)
    {
      DBG (2, "sense:  MOTOR END SWITCH ERROR\n");
      s++;
    }
  if (sense[19] & 0x40)
    {
      DBG (2, "sense:  LAMP ERROR\n");
      s++;
    }
  if (sense[19] & 0x80)
    {
      DBG (2, "sense:  OPTICAL SHADING ERROR\n");
      s++;
    }
  /* --- */
  /* The following only for AT12, its reserved so should not give problems */
  /* These are the self test results for tests 0-15 */
  if (sense[22] & 0x01)
    {
      DBG (2, "sense:  8031 INT MEM R/W ERROR\n");
      s++;
    }
  if (sense[22] & 0x02)
    {
      DBG (2, "sense:  EEPROM R/W ERROR\n");
      s++;
    }
  if (sense[22] & 0x04)
    {
      DBG (2, "sense:  ASIC TEST ERROR\n");
      s++;
    }
  if (sense[22] & 0x08)
    {
      DBG (2, "sense:  LINE RAM R/W ERROR\n");
      s++;
    }
  if (sense[22] & 0x10)
    {
      DBG (2, "sense:  PSRAM R/W TEST ERROR\n");
      s++;
    }
  if (sense[22] & 0x20)
    {
      DBG (2, "sense:  POSITIONING ERROR\n");
      s++;
    }
  if (sense[22] & 0x40)
    {
      DBG (2, "sense:  TEST 6 ERROR\n");
      s++;
    }
  if (sense[22] & 0x80)
    {
      DBG (2, "sense:  TEST 7 ERROR\n");
      s++;
    }
  if (sense[23] & 0x01)
    {
      DBG (2, "sense:  TEST 8 ERROR\n");
      s++;
    }
  if (sense[23] & 0x02)
    {
      DBG (2, "sense:  TEST 9 ERROR\n");
      s++;
    }
  if (sense[23] & 0x04)
    {
      DBG (2, "sense:  TEST 10 ERROR\n");
      s++;
    }
  if (sense[23] & 0x08)
    {
      DBG (2, "sense:  TEST 11 ERROR\n");
      s++;
    }
  if (sense[23] & 0x10)
    {
      DBG (2, "sense:  TEST 12 ERROR\n");
      s++;
    }
  if (sense[23] & 0x20)
    {
      DBG (2, "sense:  TEST 13 ERROR\n");
      s++;
    }
  if (sense[23] & 0x40)
    {
      DBG (2, "sense:  TEST 14 ERROR\n");
      s++;
    }
  if (sense[23] & 0x80)
    {
      DBG (2, "sense:  TEST 15 ERROR\n");
      s++;
    }
  /* --- */
  if (s)
    return SANE_STATUS_IO_ERROR;

  switch (sense[0])
    {
    case 0x70:			/* ALWAYS */
      switch (sense[2])
	{
	case 0x00:		/* SUCCES */
	  return SANE_STATUS_GOOD;
	case 0x02:		/* NOT READY */
	  DBG (2, "sense:  NOT READY\n");
	  return SANE_STATUS_IO_ERROR;
	case 0x03:		/* MEDIUM ERROR */
	  DBG (2, "sense:  MEDIUM ERROR\n");
	  return SANE_STATUS_IO_ERROR;
	case 0x04:		/* HARDWARE ERROR */
	  DBG (2, "sense:  HARDWARE ERROR\n");
	  return SANE_STATUS_IO_ERROR;
	case 0x05:		/* ILLEGAL REQUEST */
	  DBG (2, "sense:  ILLEGAL REQUEST\n");
	  return SANE_STATUS_IO_ERROR;
	case 0x06:		/* UNIT ATTENTION */
	  DBG (2, "sense:  UNIT ATTENTION\n");
	  return SANE_STATUS_GOOD;
	default:		/* SENSE KEY UNKNOWN */
	  DBG (2, "sense:  SENSE KEY UNKNOWN (%02x)\n", sense[2]);
	  return SANE_STATUS_IO_ERROR;
	}
    default:
      DBG (2, "sense: Unkown Error Code Qualifier (%02x)\n", sense[0]);
      return SANE_STATUS_IO_ERROR;
    }
  DBG (2, "sense: Should not come here!\n");
  return SANE_STATUS_IO_ERROR;
}


/* DB added a wait routine for the scanner to come ready */
static SANE_Status
wait_ready (int fd)
{
  SANE_Status status;
  int retry = 30;		/* make this tuneable? */

  DBG (7, "wait_ready\n");
  while (retry-- > 0)
    {
      status = sanei_scsi_cmd (fd, test_unit_ready, sizeof (test_unit_ready), 0, 0);
      if (status == SANE_STATUS_GOOD)
	return status;
      if (status == SANE_STATUS_DEVICE_BUSY)
	{
	  sleep (1);
	  continue;
	}
      /* status != GOOD && != BUSY */
      DBG (8, "wait_ready: '%s'\n", sane_strstatus (status));
      return status;
    }
  /* BUSY after n retries */
  DBG (8, "wait_ready: '%s'\n", sane_strstatus (status));
  return status;
}

/* DB added a abort routine, executed via mode select */
static SANE_Status
abort_scan (SANE_Handle handle)
{
  ARTEC_Scanner *s = handle;
  u_int8_t *data, comm[22];

  DBG (7, "mode_select_abort\n");
  memset (comm, 0, sizeof (comm));

  comm[0] = 0x15;
  comm[1] = 0x10;
  comm[2] = 0x00;
  comm[3] = 0x00;
  comm[4] = 0x10;
  comm[5] = 0x00;

  data = comm + 6;
  data[0] = 0x00;		/* mode data length */
  data[1] = 0x00;		/* medium type */
  data[2] = 0x00;		/* device specific parameter */
  data[3] = 0x00;		/* block descriptor length */

  data = comm + 10;
  data[0] = 0x00;		/* control page parameters */
  data[1] = 0x0a;		/* parameter length */
  data[2] = 0x03;		/* abort+inhibitADF flag */
  data[2] = 0x02;		/* abort flag */
  data[3] = 0x00;		/* reserved */
  data[4] = 0x00;		/* reserved */

  DBG (8, "abort: sending abort command\n");
  sanei_scsi_cmd (s->fd, comm, 6 + comm[4], 0, 0);

  DBG (8, "abort: wait for scanner to come ready...\n");
  wait_ready (s->fd);

  DBG (8, "abort: resetting abort status\n");
  data[6] = 0x01;		/* reset abort flags */
  sanei_scsi_cmd (s->fd, comm, 6 + comm[4], 0, 0);

  DBG (8, "abort: wait for scanner to come ready...\n");
  return wait_ready (s->fd);
}


static SANE_Status
read_data (int fd, int data_type_code, u_char * dest, size_t * len)
{
  static u_char read_6[10];

  DBG (7, "read_data\n");

  memset (read_6, 0, sizeof (read_6));
  read_6[0] = 0x28;
  read_6[2] = data_type_code;
  read_6[6] = *len >> 16;
  read_6[7] = *len >> 8;
  read_6[8] = *len;

  return (sanei_scsi_cmd (fd, read_6, sizeof (read_6), dest, len));
}

int
artec_get_status (int fd)
{
  u_char write_10[10];
  u_char read_12[12];
  size_t nread;

  DBG (7, "artec_get_status\n");

  nread = 12;

  memset (write_10, 0, 10);
  write_10[0] = 0x34;
  write_10[8] = 0x0c;

  sanei_scsi_cmd (fd, write_10, 10, read_12, &nread);

  nread = (read_12[9] << 16) + (read_12[10] << 8) + read_12[11];
  DBG (8, "artec_status: %lu\n", (u_long) nread);

  return (nread);
}

static SANE_Status
artec_reverse_line (SANE_Handle handle, SANE_Byte * data)
{
  ARTEC_Scanner *s = handle;
  SANE_Byte tmp_buf[32768];	/* Artec max dpi 1200 * 8.5 inches * 3 = 30600 */
  SANE_Byte *to, *from;
  int len;



  len = s->params.bytes_per_line;
  memcpy (tmp_buf, data, len);

  if (s->params.format == SANE_FRAME_RGB)	/* RGB format */
    {
      for (from = tmp_buf, to = data + len - 3;
	   to >= data;
	   to -= 3, from += 3)
	{
	  *(to + 0) = *(from + 0);	/* copy the R byte */
	  *(to + 1) = *(from + 1);	/* copy the G byte */
	  *(to + 2) = *(from + 2);	/* copy the B byte */
	}
    }
  else if (s->params.format == SANE_FRAME_GRAY)
    {
      if (s->params.depth == 8)	/* 256 color gray-scale */
	{
	  for (from = tmp_buf, to = data + len; to >= data; to--, from++)
	    {
	      *to = *from;
	    }
	}
      else if (s->params.depth == 1)	/* line art or halftone */
	{
	  for (from = tmp_buf, to = data + len; to >= data; to--, from++)
	    {
	      *to = (((*from & 0x01) << 7) &
		     ((*from & 0x02) << 5) &
		     ((*from & 0x04) << 3) &
		     ((*from & 0x08) << 1) &
		     ((*from & 0x10) >> 1) &
		     ((*from & 0x20) >> 3) &
		     ((*from & 0x40) >> 5) &
		     ((*from & 0x80) >> 7));
	    }
	}
    }

  return (SANE_STATUS_GOOD);
}

static SANE_Status
artec_line_rgb_to_byte_rgb (SANE_Byte * data, SANE_Int len)
{
  SANE_Byte tmp_buf[32768];	/* Artec max dpi 1200 * 8.5 inches * 3 = 30600 */
  int count, to;

  /* copy the rgb data to our temp buffer */
  memcpy (tmp_buf, data, len * 3);

  /* now copy back to *data in RGB format */
  for (count = 0, to = 0; count < len; count++, to += 3)
    {
      data[to] = tmp_buf[count];	/* R byte */
      data[to + 1] = tmp_buf[count + len];	/* G byte */
      data[to + 2] = tmp_buf[count + (len * 2)];	/* B byte */
    }

  return (SANE_STATUS_GOOD);
}

static SANE_Byte *tmp_line_buf = NULL;
static SANE_Byte **r_line_buf = NULL;
static SANE_Byte **g_line_buf = NULL;
static SANE_Int r_buf_lines;
static SANE_Int g_buf_lines;

static SANE_Status
artec_buffer_line_offset (SANE_Int line_offset, SANE_Byte * data, size_t * len)
{
  static SANE_Int width;
  static SANE_Int cur_line;
  SANE_Byte *tmp_r_buf_ptr;
  SANE_Byte *tmp_g_buf_ptr;
  int count;

  if (*len == 0)
    return (SANE_STATUS_GOOD);

  if (tmp_line_buf == NULL)
    {
      width = *len / 3;
      cur_line = 0;

      DBG (20, "buffer_line_offset: offset = %d, len = %lu, width = %d\n",
	   line_offset, (u_long) * len, width);

      tmp_line_buf = malloc (*len);
      if (tmp_line_buf == NULL)
	{
	  DBG (1, "couldn't allocate memory for temp line buffer\n");
	  return (SANE_STATUS_NO_MEM);
	}

      r_buf_lines = line_offset * 2;
      r_line_buf = malloc (r_buf_lines * sizeof (SANE_Byte *));
      if (r_line_buf == NULL)
	{
	  DBG (1, "couldn't allocate memory for red line buffer pointers\n");
	  return (SANE_STATUS_NO_MEM);
	}

      for (count = 0; count < r_buf_lines; count++)
	{
	  r_line_buf[count] = malloc (width * sizeof (SANE_Byte));
	  if (r_line_buf[count] == NULL)
	    {
	      DBG (1, "couldn't allocate memory for red line buffer %d\n",
		   count);
	      return (SANE_STATUS_NO_MEM);
	    }
	}

      g_buf_lines = line_offset;
      g_line_buf = malloc (g_buf_lines * sizeof (SANE_Byte *));
      if (g_line_buf == NULL)
	{
	  DBG (1,
	       "couldn't allocate memory for green line buffer pointers\n");
	  return (SANE_STATUS_NO_MEM);
	}

      for (count = 0; count < g_buf_lines; count++)
	{
	  g_line_buf[count] = malloc (width * sizeof (SANE_Byte));
	  if (g_line_buf[count] == NULL)
	    {
	      DBG (1, "couldn't allocate memory for green line buffer %d\n",
		   count);
	      return (SANE_STATUS_NO_MEM);
	    }
	}

      DBG (20, "buffer_line_offset: r lines = %d, g lines = %d\n",
	   r_buf_lines, g_buf_lines);
    }

  cur_line++;

  if (r_buf_lines > 0)
    {
      if (cur_line > r_buf_lines)
	{
	  /* get the red line info from r_buf_lines ago */
	  memcpy (tmp_line_buf, r_line_buf[0], width);

	  /* get the green line info from g_buf_lines ago */
	  memcpy (tmp_line_buf + width, g_line_buf[0], width);
	}

      /* move all the buffered red lines down (just move the ptrs for speed) */
      tmp_r_buf_ptr = r_line_buf[0];
      for (count = 0; count < (r_buf_lines - 1); count++)
	{
	  r_line_buf[count] = r_line_buf[count + 1];
	}
      r_line_buf[r_buf_lines - 1] = tmp_r_buf_ptr;

      /* insert the supplied red line data at the end of our FIFO */
      memcpy (r_line_buf[r_buf_lines - 1], data, width);

      /* move all the buffered green lines down (just move the ptrs for speed) */
      tmp_g_buf_ptr = g_line_buf[0];
      for (count = 0; count < (g_buf_lines - 1); count++)
	{
	  g_line_buf[count] = g_line_buf[count + 1];
	}
      g_line_buf[g_buf_lines - 1] = tmp_g_buf_ptr;

      /* insert the supplied green line data at the end of our FIFO */
      memcpy (g_line_buf[g_buf_lines - 1], data + width, width);

      if (cur_line > r_buf_lines)
	{
	  /* copy the red and green data in with the original blue */
	  memcpy (data, tmp_line_buf, width * 2);
	}
      else
	{
	  /* if we are in the first r_buf_lines, then we don't return anything */
	  *len = 0;
	}
    }

  return (SANE_STATUS_GOOD);
}

static SANE_Status
artec_buffer_line_offset_free (void)
{
  int count;

  free (tmp_line_buf);
  tmp_line_buf = NULL;

  for (count = 0; count < r_buf_lines; count++)
    {
      free (r_line_buf[count]);
    }
  free (r_line_buf);
  r_line_buf = NULL;

  for (count = 0; count < g_buf_lines; count++)
    {
      free (g_line_buf[count]);
    }
  free (g_line_buf);
  g_line_buf = NULL;

  return (SANE_STATUS_GOOD);
}

/* use the same  tmp_line_buf  as  artec_buffer_line_offset() */
static SANE_Byte **mono_line_buf = NULL;
static SANE_Int excess_lines;

static SANE_Status
artec_mono_bit_offset (SANE_Handle handle, SANE_Byte * data, size_t * len)
{
  ARTEC_Scanner *s = handle;
  static SANE_Int lines_in_buf;
  SANE_Byte *tmp_buf_ptr;
  int count;
  int byte;
  static int bits_read;
  int offset_byte, offset_bit;
  int line1_bits, line2_bits;
  int line1_bytes, line2_bytes;
  SANE_Byte offset_buf = 0;

  if (tmp_line_buf == NULL)
    {
      /* initialise global variables:  */

      lines_in_buf = 0;

      /* Calculate the number of lines (at the end of the scan) that contain
         unwanted data: the buffer must hold all these lines plus the previous
         line */
      excess_lines = ((s->params.bytes_per_line * 8 -
		       s->params.pixels_per_line) * s->params.lines +
		      s->params.pixels_per_line) /
	(s->params.bytes_per_line * 8);
      excess_lines++;

      mono_line_buf = malloc (excess_lines * sizeof (SANE_Byte *));
      if (mono_line_buf == NULL)
	{
	  DBG (1, "couldn't allocate memory for mono data buffer pointers\n");
	  return (SANE_STATUS_NO_MEM);
	}

      for (count = 0; count < excess_lines; count++)
	{
	  mono_line_buf[count] = malloc (s->params.bytes_per_line * sizeof (SANE_Byte));
	  if (mono_line_buf[count] == NULL)
	    {
	      DBG (1, "couldn't allocate memory for mono line buffer %d\n", count);
	      return (SANE_STATUS_NO_MEM);
	    }
	}

      tmp_line_buf = malloc (*len);
      if (tmp_line_buf == NULL)
	{
	  DBG (1, "couldn't allocate memory for temp line buffer\n");
	  return (SANE_STATUS_NO_MEM);
	}

      /* the initial data prior to the current line is 0 bytes and 0 bits */
      bits_read = 0;
    }

  if (*len == 0)
    return (SANE_STATUS_GOOD);

  DBG (7, "artec_mono_bit_offset: excess_lines = %d, lines_in_buf = %d\n",
       excess_lines, lines_in_buf);

  /* Now process the incoming mono data */

  /* insert the supplied data into the mono buffer */
  if (lines_in_buf < excess_lines)
    {
      memcpy (mono_line_buf[lines_in_buf], data, s->params.bytes_per_line);
      lines_in_buf++;
    }
  else
    {
      /* Assuming the excess_lines calculation is correct, this should
         never occur! */
      DBG (7, "artec_mono_bit_offset: mono buffer overflow!\n");
      return (SANE_STATUS_NO_MEM);	/* a more appropriate status should be returned */
    }

  offset_byte = bits_read / 8;	/* byte index (from 0) of next data */
  offset_bit = bits_read - offset_byte * 8;	/* bit index in that byte (from 0) */

  DBG (7, "artec_mono_bit_offset: bits_read = %d, offset_byte = %d, offset_bit = %d\n",
       bits_read, offset_byte, offset_bit);

  /* copy mono data from the buffer into the tmp line buffer */
  if ((offset_byte == 0) && (offset_bit == 0))
    {
      DBG (7, "artec_mono_bit_offset: Copying whole buffer line\n");
      memcpy (tmp_line_buf, mono_line_buf[0], s->params.bytes_per_line);
    }
  else
    {
      line1_bits = s->params.bytes_per_line * 8 - bits_read;
      if (line1_bits > s->params.pixels_per_line)
	line1_bits = s->params.pixels_per_line;
      line1_bytes = (line1_bits + 7) / 8;
      if (line1_bits < s->params.pixels_per_line)
	{
	  line2_bits = s->params.pixels_per_line - line1_bits;
	  line2_bytes = (line2_bits + 7) / 8;
	}
      else
	{
	  line2_bytes = 0;
	  line2_bits = 0;
	}
      DBG (7, "artec_mono_bit_offset: line1_bits = %d, line1bytes = %d\n",
	   line1_bits, line1_bytes);
      DBG (7, "artec_mono_bit_offset: line2_bits = %d, line2bytes = %d\n",
	   line2_bits, line2_bytes);

      count = 0;

      /* read the data from the first buffer line */
      for (byte = 0; byte < line1_bytes; byte++)
	{
	  if (offset_bit == 0)
	    {
	      *(tmp_line_buf + count) = *(mono_line_buf[0] + offset_byte + byte);
	      count++;
	    }
	  else
	    {
	      offset_buf = *(mono_line_buf[0] + offset_byte + byte);
	      offset_buf = offset_buf << offset_bit;
	      *(tmp_line_buf + count) = offset_buf;

	      if (byte < line1_bytes - 1)
		{
		  offset_buf = *(mono_line_buf[0] + offset_byte + byte + 1);
		  offset_buf = offset_buf >> (8 - offset_bit);
		  *(tmp_line_buf + count) = *(tmp_line_buf + count) | offset_buf;
		  count++;
		}
	    }
	}
      /* read the data from the second buffer line */
      for (byte = 0; byte < line2_bytes; byte++)
	{
	  if (offset_bit == 0)
	    {
	      *(tmp_line_buf + count) = *(mono_line_buf[1] + byte);
	      count++;
	    }
	  else
	    {
	      offset_buf = *(mono_line_buf[1] + byte);
	      offset_buf = offset_buf >> (8 - offset_bit);
	      *(tmp_line_buf + count) = *(tmp_line_buf + count) | offset_buf;
	      count++;
	      if (byte < line2_bytes - 1)
		{
		  offset_buf = *(mono_line_buf[1] + byte);
		  offset_buf = offset_buf << offset_bit;
		  *(tmp_line_buf + count) = offset_buf;
		}
	    }
	}
    }

  bits_read += s->params.pixels_per_line;
  if (bits_read >= s->params.bytes_per_line * 8)
    {
      bits_read -= s->params.bytes_per_line * 8;
      DBG (7, "artec_mono_bit_offset: rotating used line to end of buffer\n");
      /* the first line in the buffer is completely used so rotate the
         unused mono data up the buffer */
      tmp_buf_ptr = mono_line_buf[0];
      for (count = 0; count < s->params.bytes_per_line; count++)
	{
	  *(mono_line_buf[0] + count) = 0x0;
	}
      for (count = 1; count < excess_lines; count++)
	{
	  mono_line_buf[count - 1] = mono_line_buf[count];
	}
      mono_line_buf[excess_lines - 1] = tmp_buf_ptr;
      lines_in_buf--;
    }

  /* copy the offset corrected mono data back into the main buffer */
  memcpy (data, tmp_line_buf, s->params.bytes_per_line);

  return (SANE_STATUS_GOOD);
}

static SANE_Status
artec_mono_buffer_free (void)
{
  int count;

  DBG (7, "artec_mono_buffer_free\n");
  free (tmp_line_buf);
  tmp_line_buf = NULL;

  for (count = 0; count < excess_lines; count++)
    free (mono_line_buf[count]);
  free (mono_line_buf);
  mono_line_buf = NULL;

  return (SANE_STATUS_GOOD);
}

static SANE_Status
artec_read_gamma_table (SANE_Handle handle)
{
  ARTEC_Scanner *s = handle;
  char write_6[4096 + 20];	/* max gamma table is 4096 + 20 for command data */
  char *data;
  int i;

  DBG (7, "artec_read_gamma_table\n");

  memset (write_6, 0, sizeof (*write_6));

  write_6[0] = 0x28;		/* read data code */
  write_6[2] = 0x03;		/* data type code "gamma data" */
  write_6[6] = (s->gamma_length + 9) >> 16;
  write_6[7] = (s->gamma_length + 9) >> 8;
  write_6[8] = (s->gamma_length + 9);

  data = write_6 + 19;

  write_6[10] = 0x08;		/* bitmask, bit 3 means mono type */

  if (!s->val[OPT_CUSTOM_GAMMA].w)
    {
      write_6[11] = 1;		/* internal gamma table #1 (hope this is default) */
    }

  if (DBG_LEVEL > 10)
    {
      fprintf (stderr, "Gamma Table\n==================================\n");
    }

  for (i = 0; i < s->gamma_length; i++)
    {
      if (DBG_LEVEL > 10)
	{
	  if (!(i % 16))
	    fprintf (stderr, "\n%02x: ", i);
	  fprintf (stderr, "%02x ", (int) s->gamma_table[0][i]);
	}

      data[i] = s->gamma_table[0][i];
    }

  if (DBG_LEVEL > 10)
    {
      fprintf (stderr, "\n\n");
    }

  return (sanei_scsi_cmd (s->fd, write_6, 10 + 9 + s->gamma_length, 0, 0));
}

static SANE_Status
artec_send_gamma_table (SANE_Handle handle)
{
  ARTEC_Scanner *s = handle;
  char write_6[4096 + 20];	/* max gamma table is 4096 + 20 for command data */
  char *data;
  int i;

  DBG (7, "artec_send_gamma_table\n");

  memset (write_6, 0, sizeof (*write_6));

  write_6[0] = 0x2a;		/* send data code */
  write_6[2] = 0x03;		/* data type code "gamma data" */

  write_6[10] = 0x08;		/* bitmask, bit 3 means mono type */

  if (!s->val[OPT_CUSTOM_GAMMA].w)
    {
      write_6[6] = 9 >> 16;
      write_6[7] = 9 >> 8;
      write_6[8] = 9;
      write_6[11] = 1;		/* internal gamma table #1 (hope this is default) */

      return (sanei_scsi_cmd (s->fd, write_6, 10 + 9, 0, 0));
    }
  else
    {
      write_6[6] = (s->gamma_length + 9) >> 16;
      write_6[7] = (s->gamma_length + 9) >> 8;
      write_6[8] = (s->gamma_length + 9);

      if (DBG_LEVEL > 10)
	{
	  fprintf (stderr,
		   "Gamma Table\n==================================\n");
	}

      data = write_6 + 19;

      for (i = 0; i < s->gamma_length; i++)
	{
	  if (DBG_LEVEL > 10)
	    {
	      if (!(i % 16))
		fprintf (stderr, "\n%02x: ", i);
	      fprintf (stderr, "%02x ", (int) s->gamma_table[0][i]);
	    }

	  data[i] = s->gamma_table[0][i];
	}

      data[255] = 0;

      if (DBG_LEVEL > 10)
	{
	  fprintf (stderr, "\n\n");
	}

      return (sanei_scsi_cmd (s->fd, write_6, 10 + 9 + s->gamma_length, 0, 0));
    }
}

static SANE_Status
artec_set_scan_window (SANE_Handle handle)
{
  ARTEC_Scanner *s = handle;
  char write_6[4096];
  char *data;
  int counter;

  DBG (7, "artec_set_scan_window\n");

  /*
   * if we can, start before the desired window since we have to throw away
   * s->line_offset number of rows because of the RGB fixup.
   */
  if ((s->line_offset > 0) && (s->tl_x > 0))
    s->tl_x -= s->line_offset;

  data = write_6 + 10;

  DBG (4, "Scan window info:\n");
  DBG (4, "  X resolution: %5d (%d-%d)\n",
       s->x_resolution, ARTEC_MIN_X (s->hw), ARTEC_MAX_X (s->hw));
  DBG (4, "  Y resolution: %5d (%d-%d)\n",
       s->y_resolution, ARTEC_MIN_Y (s->hw), ARTEC_MAX_Y (s->hw));
  DBG (4, "  TL_X (pixel): %5d\n",
       s->tl_x);
  DBG (4, "  TL_Y (pixel): %5d\n",
       s->tl_y);
  DBG (4, "  Width       : %5d (%d-%d)\n",
       s->params.pixels_per_line,
       s->hw->x_range.min,
       (int) ((SANE_UNFIX (s->hw->x_range.max) / MM_PER_INCH) *
	      s->x_resolution));
  DBG (4, "  Height      : %5d (%d-%d)\n",
       s->params.lines,
       s->hw->y_range.min,
       (int) ((SANE_UNFIX (s->hw->y_range.max) / MM_PER_INCH) *
	      s->y_resolution));

  DBG (4, "  Image Comp. : %s\n", s->mode);
  DBG (4, "  Line Offset : %lu\n", (u_long) s->line_offset);

  memset (write_6, 0, 4096);
  write_6[0] = 0x24;
  write_6[8] = s->hw->setwindow_cmd_size;	/* total size of command */

  /* beginning of set window data header */
  data[7] = s->hw->setwindow_cmd_size - 8;	/* actual data byte count */
  data[10] = s->x_resolution >> 8;	/* x resolution */
  data[11] = s->x_resolution;
  data[12] = s->y_resolution >> 8;	/* y resolution */
  data[13] = s->y_resolution;
  data[14] = s->tl_x >> 24;	/* top left X value */
  data[15] = s->tl_x >> 16;
  data[16] = s->tl_x >> 8;
  data[17] = s->tl_x;
  data[18] = s->tl_y >> 24;	/* top left Y value */
  data[19] = s->tl_y >> 16;
  data[20] = s->tl_y >> 8;
  data[21] = s->tl_y;
  data[22] = s->params.pixels_per_line >> 24;	/* width */
  data[23] = s->params.pixels_per_line >> 16;
  data[24] = s->params.pixels_per_line >> 8;
  data[25] = s->params.pixels_per_line;
  data[26] = (s->params.lines + (s->line_offset * 2)) >> 24;	/* height */
  data[27] = (s->params.lines + (s->line_offset * 2)) >> 16;
  data[28] = (s->params.lines + (s->line_offset * 2)) >> 8;
  data[29] = (s->params.lines + (s->line_offset * 2));
  data[30] = s->val[OPT_BRIGHTNESS].w;	/* brightness (if supported) */
  data[31] = s->val[OPT_THRESHOLD].w;	/* threshold */
  data[32] = s->val[OPT_CONTRAST].w;	/* contrast */

  /* byte 33 is mode, byte 37 bit 7 is "negative" setting */
  if (strcmp (s->mode, "Lineart") == 0)
    {
      data[33] = ARTEC_COMP_LINEART;
      data[37] = (s->val[OPT_NEGATIVE].w == SANE_TRUE) ? 0x0 : 0x80;
    }
  else if (strcmp (s->mode, "Halftone") == 0)
    {
      data[33] = ARTEC_COMP_HALFTONE;
      data[37] = (s->val[OPT_NEGATIVE].w == SANE_TRUE) ? 0x0 : 0x80;
    }
  else if (strcmp (s->mode, "Gray") == 0)
    {
      data[33] = ARTEC_COMP_GRAY;
      data[37] = (s->val[OPT_NEGATIVE].w == SANE_TRUE) ? 0x80 : 0x0;
    }
  else if (strcmp (s->mode, "Color") == 0)
    {
      data[33] = ARTEC_COMP_COLOR;
      data[37] = (s->val[OPT_NEGATIVE].w == SANE_TRUE) ? 0x80 : 0x0;
    }

  data[34] = s->params.depth;	/* bits per pixel */

  data[35] = artec_get_str_index (halftone_pattern_list,
				  s->val[OPT_HALFTONE_PATTERN].s);	/* halftone pattern */
  /* user supplied halftone pattern not supported for now so override with */
  /* 8x8 Bayer */
  if (data[35] == 0)
    {
      data[35] = 4;
    }

  /* NOTE: AT12 doesn't support mono according to docs. */
  data[48] = artec_get_str_index (filter_type_list,
				  s->val[OPT_FILTER_TYPE].s);	/* filter mode */

  if (s->hw->setwindow_cmd_size > 55)
    {
      data[48] = 0x2;		/* DB filter type green for AT12,see above */
      /* this stuff is for models like the AT12 which support additional info */
      data[55] = 0x00;		/* buffer full line count */
      data[56] = 0x00;		/* buffer full line count */
      data[57] = 0x00;		/* buffer full line count */
      data[58] = 0x0a;		/* buffer full line count *//* guessing at this value */
      data[59] = 0x00;		/* access line count */
      data[60] = 0x00;		/* access line count */
      data[61] = 0x00;		/* access line count */
      data[62] = 0x0a;		/* access line count *//* guessing at this value */

      /* DB : following fields : high order bit (0x80) is enable */
      data[63] = 0x80;		/* scanner handles line offset fixup, 0 = driver handles */
      data[64] = 0;		/* disable pixel average function, 0x80 = enable */
      data[65] = 0;		/* disable lineart edge enhancement function, 0x80 = enable */
      data[66] = 0;		/* data is R-G-B format, 0x80 = G-B-R format (reversed) */
    }

  DBG (100, "Set Window data : \n");
  for (counter = 0; counter < s->hw->setwindow_cmd_size; counter++)
    {
      DBG (100, "  byte %2d = %02x \n", counter, data[counter] & 0xff);		/* DB */
    }
  DBG (100, "\n");

  /* set the scan window */
  return (sanei_scsi_cmd (s->fd, write_6, 10 +
			  s->hw->setwindow_cmd_size, 0, 0));
}

static SANE_Status
artec_start_scan (SANE_Handle handle)
{
  ARTEC_Scanner *s = handle;
  char write_7[7];

  DBG (7, "artec_start_scan\n");

  /* setup cmd to start scanning */
  memset (write_7, 0, 7);
  write_7[0] = 0x1b;
  write_7[4] = 0x01;

  /* start the scan */
  return (sanei_scsi_cmd (s->fd, write_7, 7, 0, 0));
}

static SANE_Status
artec_software_rgb_calibrate (SANE_Handle handle, SANE_Byte * buf, int lines)
{
  ARTEC_Scanner *s = handle;
  int line, i, loop, offset;

  for (line = 0; line < lines; line++)
    {
      i = 0;
      offset = 0;

      if (s->x_resolution == 200)
	{
	  /* skip ever 3rd byte, -= causes us to go down in count */
	  if ((s->tl_x % 3) == 0)
	    offset -= 1;
	}
      else
	{
	  /* round down to the previous pixel */
	  offset += ((s->tl_x / (300 / s->x_resolution)) *
		     (300 / s->x_resolution));
	}

      for (loop = 0; loop < s->params.pixels_per_line; loop++)
	{
	  if (( DBG_LEVEL == 100 ) &&
	      ( loop < 100 )) {
            fprintf( stderr, "  %2d-%4d R (%4d,%4d): %d * %5.2f = %d\n",
              line, loop, i, offset, buf[i],
              s->soft_calibrate_data[ARTEC_SOFT_CALIB_RED][offset],
              (int)(buf[i] *
                s->soft_calibrate_data[ARTEC_SOFT_CALIB_RED][offset]) );
	  }
	  buf[i] = buf[i] *
	    s->soft_calibrate_data[ARTEC_SOFT_CALIB_RED][offset];
	  i++;

	  if (( DBG_LEVEL == 100 ) &&
	      ( loop < 100 )) {
            fprintf( stderr, "          G (%4d,%4d): %d * %5.2f = %d\n",
              i, offset, buf[i],
              s->soft_calibrate_data[ARTEC_SOFT_CALIB_GREEN][offset],
              (int)(buf[i] *
                s->soft_calibrate_data[ARTEC_SOFT_CALIB_GREEN][offset]) );
	  }
	  buf[i] = buf[i] *
	    s->soft_calibrate_data[ARTEC_SOFT_CALIB_GREEN][offset];
	  i++;

	  if (( DBG_LEVEL == 100 ) &&
	      ( loop < 100 )) {
            fprintf( stderr, "          B (%4d,%4d): %d * %5.2f = %d\n",
              i, offset, buf[i],
              s->soft_calibrate_data[ARTEC_SOFT_CALIB_BLUE][offset],
              (int)(buf[i] *
                s->soft_calibrate_data[ARTEC_SOFT_CALIB_BLUE][offset]) );
	  }
	  buf[i] = buf[i] *
	    s->soft_calibrate_data[ARTEC_SOFT_CALIB_BLUE][offset];
	  i++;

	  if (s->x_resolution == 200)
	    {
	      offset += 1;

	      /* skip every 3rd byte */
	      if (((offset + 1) % 3) == 0)
		offset += 1;
	    }
	  else
	    {
	      offset += (300 / s->x_resolution);
	    }
	}
    }

  return (SANE_STATUS_GOOD);
}

static SANE_Status
artec_calibrate_shading (SANE_Handle handle)
{
  ARTEC_Scanner *s = handle;
  SANE_Status status;		/* DB added */
  u_char buf[76800];		/* should be big enough */
  size_t len;
  SANE_Word save_x_resolution;
  SANE_Word save_pixels_per_line;
  int i;

  DBG (7, "artec_calibrate_white_shading\n");

  if (s->hw->flags & ARTEC_FLAG_CALIBRATE_RGB)
    {
      /* this method scans in 4 lines each of Red, Green, and Blue */
      /* after reading line of shading data, generate data for software */
      /* calibration so we have it if user requests */
      len = 4 * 2592;		/* 4 lines of data, 2592 pixels wide */

      if ( DBG_LEVEL == 100 ) {
        fprintf( stderr, "RED Software Calibration data\n" );
      }

      read_data (s->fd, ARTEC_DATA_RED_SHADING, buf, &len);
      for (i = 0; i < 2592; i++) {
	s->soft_calibrate_data[ARTEC_SOFT_CALIB_RED][i] =
	  255.0 / ((buf[i] + buf[i + 2592] + buf[i + 5184] + buf[i + 7776]) / 4);
        if ( DBG_LEVEL == 100 ) {
          fprintf( stderr,
            "   %4d: 255.0 / (( %3d + %3d + %3d + %3d ) / 4 ) = %5.2f\n",
            i, buf[i], buf[i + 2592], buf[i + 5184], buf[i + 7776],
            s->soft_calibrate_data[ARTEC_SOFT_CALIB_RED][i] );
        }
      }

      if ( DBG_LEVEL == 100 ) {
        fprintf( stderr, "GREEN Software Calibration data\n" );
      }

      read_data (s->fd, ARTEC_DATA_GREEN_SHADING, buf, &len);
      for (i = 0; i < 2592; i++) {
	s->soft_calibrate_data[ARTEC_SOFT_CALIB_GREEN][i] =
	  255.0 / ((buf[i] + buf[i + 2592] + buf[i + 5184] + buf[i + 7776]) / 4);
        if ( DBG_LEVEL == 100 ) {
          fprintf( stderr,
            "   %4d: 255.0 / (( %3d + %3d + %3d + %3d ) / 4 ) = %5.2f\n",
            i, buf[i], buf[i + 2592], buf[i + 5184], buf[i + 7776],
	    s->soft_calibrate_data[ARTEC_SOFT_CALIB_GREEN][i] );
	}
      }

      if ( DBG_LEVEL == 100 ) {
        fprintf( stderr, "BLUE Software Calibration data\n" );
      }

      read_data (s->fd, ARTEC_DATA_BLUE_SHADING, buf, &len);
      for (i = 0; i < 2592; i++) {
	s->soft_calibrate_data[ARTEC_SOFT_CALIB_BLUE][i] =
	  255.0 / ((buf[i] + buf[i + 2592] + buf[i + 5184] + buf[i + 7776]) / 4);
        if ( DBG_LEVEL == 100 ) {
          fprintf( stderr,
            "   %4d: 255.0 / (( %3d + %3d + %3d + %3d ) / 4 ) = %5.2f\n",
            i, buf[i], buf[i + 2592], buf[i + 5184], buf[i + 7776],
	    s->soft_calibrate_data[ARTEC_SOFT_CALIB_BLUE][i] );
	}
      }
    }
  else if (s->hw->flags & ARTEC_FLAG_CALIBRATE_DARK_WHITE)
    {
      /* this method scans black, then white data */
      len = 3 * 5100;		/* 1 line of data, 5100 pixels wide, RGB data */
      read_data (s->fd, ARTEC_DATA_DARK_SHADING, buf, &len);
      save_x_resolution = s->x_resolution;
      s->x_resolution = 600;
      save_pixels_per_line = s->params.pixels_per_line;
      s->params.pixels_per_line = ARTEC_MAX_X (s->hw);
      s->params.pixels_per_line = 600 * 8.5;	/* ?this? or ?above line? */
      /* DB added wait_ready */
      status = wait_ready (s->fd);
      if (status != SANE_STATUS_GOOD)
	{
	  DBG (1, "wait for scanner ready failed: %s\n", sane_strstatus (status));
	  return status;
	}
      /* next line should use ARTEC_DATA_WHITE_SHADING_TRANS if using ADF */
      read_data (s->fd, ARTEC_DATA_WHITE_SHADING_OPT, buf, &len);
      s->x_resolution = save_x_resolution;
      s->params.pixels_per_line = save_pixels_per_line;
    }

  return (SANE_STATUS_GOOD);
}


static SANE_Status
end_scan (SANE_Handle handle)
{
  ARTEC_Scanner *s = handle;
  /* DB
     u_int8_t write_6[6] =
     {0x1B, 0, 0, 0, 0, 0};
   */

  DBG (7, "end_scan...\n");
  s->scanning = SANE_FALSE;

  if (s->this_pass == 3)
    s->this_pass = 0;

  /* DB
     return (sanei_scsi_cmd (s->fd, write_6, 6, 0, 0));
   */
  return abort_scan (s);
}


static SANE_Status
artec_get_cap_data (ARTEC_Device * dev, int fd)
{
  int cap_model, loop;
  SANE_Status status;
  u_char cap_buf[256];		/* buffer for cap data */

  /* DB always use the hard-coded capability info first 
   * if we get cap data from the scanner, we override */
  cap_model = -1;
  for (loop = 0; loop < NELEMS (cap_data); loop++)
    {
      if (strcmp (cap_data[loop].model, dev->sane.model) == 0)
	{
	  cap_model = loop;
	}
    }

  if (cap_model == -1)
    {
      DBG (1, "unable to identify Artec model '%s', check artec.c\n",
	   dev->sane.model);
      return (SANE_STATUS_UNSUPPORTED);
    }

  dev->x_range.min = 0;
  dev->x_range.quant = 1;
  dev->x_range.max = SANE_FIX (cap_data[cap_model].width) * MM_PER_INCH;

  dev->width = cap_data[cap_model].width;

  dev->y_range.min = 0;
  dev->y_range.quant = 1;
  dev->y_range.max = SANE_FIX (cap_data[cap_model].height) * MM_PER_INCH;

  dev->height = cap_data[cap_model].height;

  status = artec_str_list_to_word_list (&dev->horz_resolution_list,
				   cap_data[cap_model].horz_resolution_str);

  status = artec_str_list_to_word_list (&dev->vert_resolution_list,
				   cap_data[cap_model].vert_resolution_str);

  dev->contrast_range.min = cap_data[cap_model].contrast_min;
  dev->contrast_range.max = cap_data[cap_model].contrast_max;
  dev->contrast_range.quant = 1;

  dev->threshold_range.min = cap_data[cap_model].threshold_min;
  dev->threshold_range.max = cap_data[cap_model].threshold_max;
  dev->threshold_range.quant = 1;

  dev->sane.type = cap_data[cap_model].type;

  dev->brightness_range.min = cap_data[cap_model].brightness_min;
  dev->brightness_range.max = cap_data[cap_model].brightness_max;
  dev->brightness_range.quant = 1;

  dev->max_read_size = cap_data[cap_model].max_read_size;

  dev->flags = cap_data[cap_model].flags;

  switch (cap_data[cap_model].adc_bits)
    {
    case 8:
      dev->gamma_length = 256;
      break;

    case 10:
      dev->gamma_length = 1024;
      break;

    case 12:
      dev->gamma_length = 4096;
      break;
    }

  dev->setwindow_cmd_size = cap_data[cap_model].setwindow_cmd_size;

  if (dev->support_cap_data_retrieve)	/* DB */
    {
      /* DB added reading capability data from scanner */
      char info[80];		/* for printing debugging info */
      size_t len = sizeof (cap_buf);

      /* read the capability data from the scanner */
      DBG (20, "reading capability data from scanner...\n");

      wait_ready (fd);

      read_data (fd, ARTEC_DATA_CAPABILITY_DATA, cap_buf, &len);

      DBG (100, "scanner capability data : \n");
      /*
         for (i = 0; i < 59; i++)
         {
         DBG (100, "  byte %2d = %02x \n", i, cap_buf[i]&0xff); 
         }
         DBG (100, "\n");
       */

      strncpy (info, (const char *) &cap_buf[0], 8);
      info[8] = '\0';
      DBG (100, "  Vendor                    : %s\n", info);
      strncpy (info, (const char *) &cap_buf[8], 16);
      info[16] = '\0';
      DBG (100, "  Device Name               : %s\n", info);
      strncpy (info, (const char *) &cap_buf[24], 4);
      info[4] = '\0';
      DBG (100, "  Version Number            : %s\n", info);
      sprintf (info, "%d ", cap_buf[29]);
      DBG (100, "  CCD Type                  : %s\n", info);
      sprintf (info, "%d ", cap_buf[30]);
      DBG (100, "  AD Converter Type         : %s\n", info);
      sprintf (info, "%d ", (cap_buf[31] << 8) | cap_buf[32]);
      DBG (100, "  Buffer size               : %s\n", info);
      sprintf (info, "%d ", cap_buf[33]);
      DBG (100, "  Channels of RGB Gamma     : %s\n", info);
      sprintf (info, "%d ", (cap_buf[34] << 8) | cap_buf[35]);
      DBG (100, "  Opt. res. of R channel    : %s\n", info);
      sprintf (info, "%d ", (cap_buf[36] << 8) | cap_buf[37]);
      DBG (100, "  Opt. res. of G channel    : %s\n", info);
      sprintf (info, "%d ", (cap_buf[38] << 8) | cap_buf[39]);
      DBG (100, "  Opt. res. of B channel    : %s\n", info);
      sprintf (info, "%d ", (cap_buf[40] << 8) | cap_buf[41]);
      DBG (100, "  Min. Hor. Resolution      : %s\n", info);
      sprintf (info, "%d ", (cap_buf[42] << 8) | cap_buf[43]);
      DBG (100, "  Max. Vert. Resolution     : %s\n", info);
      sprintf (info, "%d ", (cap_buf[44] << 8) | cap_buf[45]);
      DBG (100, "  Min. Vert. Resolution     : %s\n", info);
      sprintf (info, "%s ", cap_buf[46] == 0x80 ? "yes" : "no");
      DBG (100, "  Chunky Data Format        : %s\n", info);
      sprintf (info, "%s ", cap_buf[47] == 0x80 ? "yes" : "no");
      DBG (100, "  RGB Data Format           : %s\n", info);
      sprintf (info, "%s ", cap_buf[48] == 0x80 ? "yes" : "no");
      DBG (100, "  BGR Data Format           : %s\n", info);
      sprintf (info, "%d ", cap_buf[49]);
      DBG (100, "  Line Offset               : %s\n", info);
      sprintf (info, "%s ", cap_buf[50] == 0x80 ? "yes" : "no");
      DBG (100, "  Channel Valid Sequence    : %s\n", info);
      sprintf (info, "%s ", cap_buf[51] == 0x80 ? "yes" : "no");
      DBG (100, "  True Gray                 : %s\n", info);
      sprintf (info, "%s ", cap_buf[52] == 0x80 ? "yes" : "no");
      DBG (100, "  Force Host Not Do Shading : %s\n", info);
      sprintf (info, "%s ", cap_buf[53] == 0x00 ? "AT006" : "AT010");
      DBG (100, "  ASIC                      : %s\n", info);
      sprintf (info, "%s ", cap_buf[54] == 0x82 ? "SCSI2" :
	       cap_buf[54] == 0x81 ? "SCSI1" : "Parallel");
      DBG (100, "  Interface                 : %s\n", info);
      sprintf (info, "%d ", (cap_buf[55] << 8) | cap_buf[56]);
      DBG (100, "  Phys. Area Width          : %s\n", info);
      sprintf (info, "%d ", (cap_buf[57] << 8) | cap_buf[58]);
      DBG (100, "  Phys. Area Length         : %s\n", info);

      /* fill in the information we've got from the scanner */

      dev->width = ((float) ((cap_buf[55] << 8) | cap_buf[56])) / 1000;
      dev->height = ((float) ((cap_buf[57] << 8) | cap_buf[58])) / 1000;

      /* DB ----- */
    }

  DBG (8, "Scanner capability info.\n");
  DBG (8, "  Vendor      : %s\n", dev->sane.vendor);
  DBG (8, "  Model       : %s\n", dev->sane.model);
  DBG (8, "  Type        : %s\n", dev->sane.type);
  DBG (5, "  Width       : %.2f inches\n", dev->width);
  DBG (8, "  Height      : %.2f inches\n", dev->height);
  DBG (8, "  X Range(mm) : %d-%d\n",
       dev->x_range.min,
       (int) (SANE_UNFIX (dev->x_range.max)));
  DBG (8, "  Y Range(mm) : %d-%d\n",
       dev->y_range.min,
       (int) (SANE_UNFIX (dev->y_range.max)));

  DBG (8, "  Horz. DPI   : %d-%d\n", ARTEC_MIN_X (dev), ARTEC_MAX_X (dev));
  DBG (8, "  Vert. DPI   : %d-%d\n", ARTEC_MIN_Y (dev), ARTEC_MAX_Y (dev));
  DBG (8, "  Contrast    : %d-%d\n",
       dev->contrast_range.min, dev->contrast_range.max);
  DBG (8, "  REQ Sh. Cal.: %d\n",
       dev->flags & ARTEC_FLAG_CALIBRATE ? 1 : 0);
  DBG (8, "  REQ Ln. Offs: %d\n",
       dev->flags & ARTEC_FLAG_RGB_LINE_OFFSET ? 1 : 0);
  DBG (8, "  REQ Ch. Shft: %d\n",
       dev->flags & ARTEC_FLAG_RGB_CHAR_SHIFT ? 1 : 0);
  DBG (8, "  SetWind Size: %d\n",
       dev->setwindow_cmd_size);
  DBG (8, "  Calib Method: %s\n",
       dev->flags & ARTEC_FLAG_CALIBRATE_RGB ? "RGB" :
       dev->flags & ARTEC_FLAG_CALIBRATE_DARK_WHITE ? "white/black" : "N/A");

  return (SANE_STATUS_GOOD);
}

static SANE_Status
dump_inquiry (unsigned char *result)
{
  int i;

  DBG (4, "dump_inquiry.\n");
  fprintf (stderr, " === SANE/Artec backend v%d.%d ===\n",
	   ARTEC_MAJOR, ARTEC_MINOR);
  fprintf (stderr, " ===== Scanner Inquiry Block =====\n");
  for (i = 0; i < 96; i++)
    {
      if (!(i % 16))
	fprintf (stderr, "\n%02x: ", i);
      fprintf (stderr, "%02x ", (int) result[i]);
    }
  fprintf (stderr, "\n\n");

  return (SANE_STATUS_GOOD);
}

static SANE_Status
attach (const char *devname, ARTEC_Device ** devp)
{
  char result[INQ_LEN];
  char product_revision[5];
  char temp_result[33];
  char *str, *t;
  int fd;
  SANE_Status status;
  ARTEC_Device *dev;
  size_t size;

  for (dev = first_dev; dev; dev = dev->next)
    {
      if (strcmp (dev->sane.name, devname) == 0)
	{
	  if (devp)
	    *devp = dev;
	  return (SANE_STATUS_GOOD);
	}
    }

  DBG (6, "attach: opening %s\n", devname);

  status = sanei_scsi_open (devname, &fd, sense_handler, 0);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "attach: open failed (%s)\n", sane_strstatus (status));
      return (SANE_STATUS_INVAL);
    }

  DBG (6, "attach: sending INQUIRY\n");
  size = sizeof (result);
  status = sanei_scsi_cmd (fd, inquiry, sizeof (inquiry), result, &size);
  if (status != SANE_STATUS_GOOD || size < 16)
    {
      DBG (1, "attach: inquiry failed (%s)\n", sane_strstatus (status));
      sanei_scsi_close (fd);
      return (status);
    }

  /*
   * Check to see if this device is a scanner.
   */
  if (result[0] != 0x6)
    {
      DBG (1, "attach: device doesn't look like a scanner at all.\n");
      sanei_scsi_close (fd);
      return (SANE_STATUS_INVAL);
    }

  /*
   * The BlackWidow BW4800SP is actually a rebadged AT3, with the vendor
   * string set to 8 spaces and the product to "Flatbed Scanner ".  So,
   * if we have one of these, we'll make it look like an AT3.
   *
   * For now, to be on the safe side, we'll also check the version number
   * since BlackWidow seems to have left that intact as "1.90".
   *
   * Check that result[36] == 0x00 so we don't mistake a microtek scanner.
   */
  if ((result[36] == 0x00) &&
      (strncmp (result + 32, "1.90", 4) == 0) &&
      (strncmp (result + 8, "        ", 8) == 0) &&
      (strncmp (result + 16, "Flatbed Scanner ", 16) == 0))
    {
      DBG (4, "Found BlackWidow BW4800SP scanner, setting up like AT3\n");

      /* setup the vendor and product to mimic the Artec/Ultima AT3 */
      strncpy (result + 8, "ULTIMA", 6);
      strncpy (result + 16, "AT3             ", 16);
    }

  /*
   * Check to see if they have forced a vendor and/or model string and
   * if so, fudge the inquiry results with that info.  We do this right
   * before we check the inquiry results, otherwise we might not be forcing
   * anything.
   */
  if (artec_vendor[0] != 0x0)
    {
      /*
       * 1) copy the vendor string to our temp variable
       * 2) append 8 spaces to make sure we have at least 8 characters
       * 3) copy our fudged vendor string into the inquiry result.
       */
      strcpy (temp_result, artec_vendor);
      strcat (temp_result, "        ");
      strncpy (result + 8, temp_result, 8);
    }

  if (artec_model[0] != 0x0)
    {
      /*
       * 1) copy the model string to our temp variable
       * 2) append 16 spaces to make sure we have at least 16 characters
       * 3) copy our fudged model string into the inquiry result.
       */
      strcpy (temp_result, artec_model);
      strcat (temp_result, "                ");
      strncpy (result + 16, temp_result, 16);
    }

  /* are we really dealing with a scanner by ULTIMA/ARTEC? */
  if ((strncmp (result + 8, "ULTIMA", 6) != 0) &&
      (strncmp (result + 8, "ARTEC", 5) != 0))
    {
      DBG (1, "attach: device doesn't look like a Artec/ULTIMA scanner\n");

      strncpy (temp_result, result + 8, 8);
      temp_result[8] = 0x0;
      DBG (1, "attach: FOUND vendor = '%s'\n", temp_result);
      strncpy (temp_result, result + 16, 16);
      temp_result[16] = 0x0;
      DBG (1, "attach: FOUND model  = '%s'\n", temp_result);

      sanei_scsi_close (fd);
      return (SANE_STATUS_INVAL);
    }

  DBG (6, "attach: wait for scanner to come ready\n");
  status = wait_ready (fd);

  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "attach: test unit ready failed (%s)\n",
	   sane_strstatus (status));
      sanei_scsi_close (fd);
      return (status);
    }

  dev = malloc (sizeof (*dev));
  if (!dev)
    return (SANE_STATUS_NO_MEM);

  memset (dev, 0, sizeof (*dev));

  if (DBG_LEVEL > 0)
    dump_inquiry ((unsigned char *) result);

  dev->sane.name = strdup (devname);

  /* get the model info */
  str = malloc (17);
  memcpy (str, result + 16, 16);
  str[16] = ' ';
  t = str + 16;
  while ((*t == ' ') && (t > str))
    {
      *t = '\0';
      t--;
    }
  dev->sane.model = str;

  /* for some reason, the firmware revision is in the model info string on */
  /* the A6000C PLUS scanners instead of in it's proper place */
  if (strstr (str, "A6000C PLUS") == str)
    {
      str[11] = '\0';
      strncpy (product_revision, str + 12, 4);
    }
  else
    {
      /* get the product revision from it's normal place */
      strncpy (product_revision, result + 32, 4);
    }
  product_revision[4] = ' ';
  t = strchr (product_revision, ' ');
  *t = '\0';

  /* get the vendor info */
  str = malloc (9);
  memcpy (str, result + 8, 8);
  str[8] = ' ';
  t = strchr (str, ' ');
  *t = '\0';
  dev->sane.vendor = str;

  /* Artec docs say if bytes 36-43 = "ULTIMA  ", then supports read cap. data */
  if (strncmp (result + 36, "ULTIMA  ", 8) == 0)
    {
      DBG (5, "scanner supports read capability data function\n");
      dev->support_cap_data_retrieve = SANE_TRUE;
    }
  else
    {
      DBG (5, "scanner does NOT support read capability data function\n");
      dev->support_cap_data_retrieve = SANE_FALSE;
    }

  DBG (6, "attach: getting scanner capability data\n");
  status = artec_get_cap_data (dev, fd);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "attach: artec_get_cap_data failed (%s)\n",
	   sane_strstatus (status));
      sanei_scsi_close (fd);
      return (status);
    }

  sanei_scsi_close (fd);

  ++num_devices;
  dev->next = first_dev;
  first_dev = dev;

  if (devp)
    *devp = dev;

  return (SANE_STATUS_GOOD);
}

static SANE_Status
init_options (ARTEC_Scanner * s)
{
  int i;

  DBG (7, "init_options\n");

  memset (s->opt, 0, sizeof (s->opt));
  memset (s->val, 0, sizeof (s->val));

  for (i = 0; i < NUM_OPTIONS; ++i)
    {
      s->opt[i].size = sizeof (SANE_Word);
      s->opt[i].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    }

  s->opt[OPT_NUM_OPTS].title = SANE_TITLE_NUM_OPTIONS;
  s->opt[OPT_NUM_OPTS].desc = SANE_DESC_NUM_OPTIONS;
  s->opt[OPT_NUM_OPTS].cap = SANE_CAP_SOFT_DETECT;
  s->val[OPT_NUM_OPTS].w = NUM_OPTIONS;

  /* "Mode" group: */
  s->opt[OPT_MODE_GROUP].title = "Scan Mode";
  s->opt[OPT_MODE_GROUP].desc = "";
  s->opt[OPT_MODE_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_MODE_GROUP].cap = 0;
  s->opt[OPT_MODE_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  /* scan mode */
  s->opt[OPT_MODE].name = SANE_NAME_SCAN_MODE;
  s->opt[OPT_MODE].title = SANE_TITLE_SCAN_MODE;
  s->opt[OPT_MODE].desc = SANE_DESC_SCAN_MODE;
  s->opt[OPT_MODE].type = SANE_TYPE_STRING;
  s->opt[OPT_MODE].size = max_string_size (mode_list);
  s->opt[OPT_MODE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
  s->opt[OPT_MODE].constraint.string_list = mode_list;
  s->val[OPT_MODE].s = strdup (mode_list[3]);

  /* horizontal resolution */
  s->opt[OPT_X_RESOLUTION].name = SANE_NAME_SCAN_RESOLUTION;
  s->opt[OPT_X_RESOLUTION].title = SANE_TITLE_SCAN_RESOLUTION;
  s->opt[OPT_X_RESOLUTION].desc = SANE_DESC_SCAN_RESOLUTION;
  s->opt[OPT_X_RESOLUTION].type = SANE_TYPE_INT;
  s->opt[OPT_X_RESOLUTION].unit = SANE_UNIT_DPI;
  s->opt[OPT_X_RESOLUTION].constraint_type = SANE_CONSTRAINT_WORD_LIST;
  s->opt[OPT_X_RESOLUTION].constraint.word_list = s->hw->horz_resolution_list;
  s->val[OPT_X_RESOLUTION].w = 100;

  /* vertical resolution */
  s->opt[OPT_Y_RESOLUTION].name = SANE_NAME_SCAN_Y_RESOLUTION;
  s->opt[OPT_Y_RESOLUTION].title = SANE_TITLE_SCAN_Y_RESOLUTION;
  s->opt[OPT_Y_RESOLUTION].desc = SANE_DESC_SCAN_Y_RESOLUTION;
  s->opt[OPT_Y_RESOLUTION].type = SANE_TYPE_INT;
  s->opt[OPT_Y_RESOLUTION].unit = SANE_UNIT_DPI;
  s->opt[OPT_Y_RESOLUTION].constraint_type = SANE_CONSTRAINT_WORD_LIST;
  s->opt[OPT_Y_RESOLUTION].constraint.word_list = s->hw->vert_resolution_list;
  s->opt[OPT_Y_RESOLUTION].cap |= SANE_CAP_INACTIVE;
  s->val[OPT_Y_RESOLUTION].w = 100;

  /* bind resolution */
  s->opt[OPT_RESOLUTION_BIND].name = SANE_NAME_RESOLUTION_BIND;
  s->opt[OPT_RESOLUTION_BIND].title = SANE_TITLE_RESOLUTION_BIND;
  s->opt[OPT_RESOLUTION_BIND].desc = SANE_DESC_RESOLUTION_BIND;
  s->opt[OPT_RESOLUTION_BIND].type = SANE_TYPE_BOOL;
  s->val[OPT_RESOLUTION_BIND].w = SANE_TRUE;

  if (!(s->hw->flags & ARTEC_FLAG_SEPARATE_RES))
    s->opt[OPT_RESOLUTION_BIND].cap |= SANE_CAP_INACTIVE;

  /* Preview Mode */
  s->opt[OPT_PREVIEW].name = SANE_NAME_PREVIEW;
  s->opt[OPT_PREVIEW].title = SANE_TITLE_PREVIEW;
  s->opt[OPT_PREVIEW].desc = SANE_DESC_PREVIEW;
  s->opt[OPT_PREVIEW].type = SANE_TYPE_BOOL;
  s->opt[OPT_PREVIEW].unit = SANE_UNIT_NONE;
  s->opt[OPT_PREVIEW].size = sizeof (SANE_Word);
  s->val[OPT_PREVIEW].w = SANE_FALSE;

  /* Grayscale Preview Mode */
  s->opt[OPT_GRAY_PREVIEW].name = SANE_NAME_GRAY_PREVIEW;
  s->opt[OPT_GRAY_PREVIEW].title = SANE_TITLE_GRAY_PREVIEW;
  s->opt[OPT_GRAY_PREVIEW].desc = SANE_DESC_GRAY_PREVIEW;
  s->opt[OPT_GRAY_PREVIEW].type = SANE_TYPE_BOOL;
  s->opt[OPT_GRAY_PREVIEW].unit = SANE_UNIT_NONE;
  s->opt[OPT_GRAY_PREVIEW].size = sizeof (SANE_Word);
  s->val[OPT_GRAY_PREVIEW].w = SANE_FALSE;

  /* negative */
  s->opt[OPT_NEGATIVE].name = SANE_NAME_NEGATIVE;
  s->opt[OPT_NEGATIVE].title = SANE_TITLE_NEGATIVE;
  s->opt[OPT_NEGATIVE].desc = "Negative Image";
  s->opt[OPT_NEGATIVE].type = SANE_TYPE_BOOL;
  s->val[OPT_NEGATIVE].w = SANE_FALSE;

  /* "Geometry" group: */
  s->opt[OPT_GEOMETRY_GROUP].title = "Geometry";
  s->opt[OPT_GEOMETRY_GROUP].desc = "";
  s->opt[OPT_GEOMETRY_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_GEOMETRY_GROUP].cap = SANE_CAP_ADVANCED;
  s->opt[OPT_GEOMETRY_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  /* top-left x */
  s->opt[OPT_TL_X].name = SANE_NAME_SCAN_TL_X;
  s->opt[OPT_TL_X].title = SANE_TITLE_SCAN_TL_X;
  s->opt[OPT_TL_X].desc = SANE_DESC_SCAN_TL_X;
  s->opt[OPT_TL_X].type = SANE_TYPE_FIXED;
  s->opt[OPT_TL_X].unit = SANE_UNIT_MM;

  s->opt[OPT_TL_X].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_TL_X].constraint.range = &s->hw->x_range;
  s->val[OPT_TL_X].w = s->hw->x_range.min;

  /* top-left y */
  s->opt[OPT_TL_Y].name = SANE_NAME_SCAN_TL_Y;
  s->opt[OPT_TL_Y].title = SANE_TITLE_SCAN_TL_Y;
  s->opt[OPT_TL_Y].desc = SANE_DESC_SCAN_TL_Y;
  s->opt[OPT_TL_Y].type = SANE_TYPE_FIXED;
  s->opt[OPT_TL_Y].unit = SANE_UNIT_MM;

  s->opt[OPT_TL_Y].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_TL_Y].constraint.range = &s->hw->y_range;
  s->val[OPT_TL_Y].w = s->hw->y_range.min;

  /* bottom-right x */
  s->opt[OPT_BR_X].name = SANE_NAME_SCAN_BR_X;
  s->opt[OPT_BR_X].title = SANE_TITLE_SCAN_BR_X;
  s->opt[OPT_BR_X].desc = SANE_DESC_SCAN_BR_X;
  s->opt[OPT_BR_X].type = SANE_TYPE_FIXED;
  s->opt[OPT_BR_X].unit = SANE_UNIT_MM;

  s->opt[OPT_BR_X].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_BR_X].constraint.range = &s->hw->x_range;
  s->val[OPT_BR_X].w = s->hw->x_range.max;

  /* bottom-right y */
  s->opt[OPT_BR_Y].name = SANE_NAME_SCAN_BR_Y;
  s->opt[OPT_BR_Y].title = SANE_TITLE_SCAN_BR_Y;
  s->opt[OPT_BR_Y].desc = SANE_DESC_SCAN_BR_Y;
  s->opt[OPT_BR_Y].type = SANE_TYPE_FIXED;
  s->opt[OPT_BR_Y].unit = SANE_UNIT_MM;

  s->opt[OPT_BR_Y].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_BR_Y].constraint.range = &s->hw->y_range;
  s->val[OPT_BR_Y].w = s->hw->y_range.max;

  /* Enhancement group: */
  s->opt[OPT_ENHANCEMENT_GROUP].title = "Enhancement";
  s->opt[OPT_ENHANCEMENT_GROUP].desc = "";
  s->opt[OPT_ENHANCEMENT_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_ENHANCEMENT_GROUP].cap = SANE_CAP_ADVANCED;
  s->opt[OPT_ENHANCEMENT_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  /* Calibrate Every Scan? */
  s->opt[OPT_QUALITY_CAL].name = SANE_NAME_QUALITY_CAL;
  s->opt[OPT_QUALITY_CAL].title = "H/W Calibrate Every Scan";
  s->opt[OPT_QUALITY_CAL].desc = "Perform hardware calibration on every scan";
  s->opt[OPT_QUALITY_CAL].type = SANE_TYPE_BOOL;
  s->val[OPT_QUALITY_CAL].w = SANE_FALSE;

  if (!(s->hw->flags & ARTEC_FLAG_CALIBRATE))
    {
      s->opt[OPT_QUALITY_CAL].cap |= SANE_CAP_INACTIVE;
    }

  /* Perform Software Quality Calibration */
  s->opt[OPT_SOFTWARE_CAL].name = "software-cal";
  s->opt[OPT_SOFTWARE_CAL].title = "Software Calibration";
  s->opt[OPT_SOFTWARE_CAL].desc = "Perform software quality calibration in "
    "addition to hardware calibration";
  s->opt[OPT_SOFTWARE_CAL].type = SANE_TYPE_BOOL;
  s->val[OPT_SOFTWARE_CAL].w = SANE_FALSE;

  /* check for RGB calibration now because we have only implemented software */
  /* calibration in conjunction with hardware RGB calibration */
  if ((!(s->hw->flags & ARTEC_FLAG_CALIBRATE)) ||
      (!(s->hw->flags & ARTEC_FLAG_CALIBRATE_RGB)))
    {
      s->opt[OPT_SOFTWARE_CAL].cap |= SANE_CAP_INACTIVE;
    }

  /* filter mode */
  s->opt[OPT_FILTER_TYPE].name = "filter-type";
  s->opt[OPT_FILTER_TYPE].title = "Filter Type";
  s->opt[OPT_FILTER_TYPE].desc = "Filter Type for mono scans";
  s->opt[OPT_FILTER_TYPE].type = SANE_TYPE_STRING;
  s->opt[OPT_FILTER_TYPE].size = max_string_size (filter_type_list);
  s->opt[OPT_FILTER_TYPE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
  s->opt[OPT_FILTER_TYPE].constraint.string_list = filter_type_list;
  s->val[OPT_FILTER_TYPE].s = strdup (filter_type_list[0]);

  /* contrast */
  s->opt[OPT_CONTRAST].name = SANE_NAME_CONTRAST;
  s->opt[OPT_CONTRAST].title = SANE_TITLE_CONTRAST;
  s->opt[OPT_CONTRAST].desc = SANE_DESC_CONTRAST;
  s->opt[OPT_CONTRAST].type = SANE_TYPE_INT;
  s->opt[OPT_CONTRAST].unit = SANE_UNIT_NONE;
  s->opt[OPT_CONTRAST].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_CONTRAST].constraint.range = &s->hw->contrast_range;
  s->val[OPT_CONTRAST].w = 0x80;

  /* threshold */
  s->opt[OPT_THRESHOLD].name = SANE_NAME_THRESHOLD;
  s->opt[OPT_THRESHOLD].title = SANE_TITLE_THRESHOLD;
  s->opt[OPT_THRESHOLD].desc = SANE_DESC_THRESHOLD;
  s->opt[OPT_THRESHOLD].type = SANE_TYPE_INT;
  s->opt[OPT_THRESHOLD].unit = SANE_UNIT_NONE;
  s->opt[OPT_THRESHOLD].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_THRESHOLD].constraint.range = &s->hw->threshold_range;
  s->val[OPT_THRESHOLD].w = 0x80;

  /* brightness */
  s->opt[OPT_BRIGHTNESS].name = SANE_NAME_BRIGHTNESS;
  s->opt[OPT_BRIGHTNESS].title = SANE_TITLE_BRIGHTNESS;
  s->opt[OPT_BRIGHTNESS].desc = SANE_DESC_BRIGHTNESS;
  s->opt[OPT_BRIGHTNESS].type = SANE_TYPE_INT;
  s->opt[OPT_BRIGHTNESS].unit = SANE_UNIT_NONE;
  s->opt[OPT_BRIGHTNESS].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_BRIGHTNESS].constraint.range = &s->hw->brightness_range;
  s->val[OPT_BRIGHTNESS].w = 0x80;

  if (!(s->hw->flags & ARTEC_FLAG_OPT_BRIGHTNESS))
    {
      s->opt[OPT_BRIGHTNESS].cap |= SANE_CAP_INACTIVE;
    }

  /* halftone pattern */
  s->opt[OPT_HALFTONE_PATTERN].name = SANE_NAME_HALFTONE_PATTERN;
  s->opt[OPT_HALFTONE_PATTERN].title = SANE_TITLE_HALFTONE_PATTERN;
  s->opt[OPT_HALFTONE_PATTERN].desc = SANE_DESC_HALFTONE_PATTERN;
  s->opt[OPT_HALFTONE_PATTERN].type = SANE_TYPE_STRING;
  s->opt[OPT_HALFTONE_PATTERN].size = max_string_size (halftone_pattern_list);
  s->opt[OPT_HALFTONE_PATTERN].constraint_type = SANE_CONSTRAINT_STRING_LIST;
  s->opt[OPT_HALFTONE_PATTERN].constraint.string_list = halftone_pattern_list;
  s->val[OPT_HALFTONE_PATTERN].s = strdup (halftone_pattern_list[1]);

  /* custom-gamma table */
  s->opt[OPT_CUSTOM_GAMMA].name = SANE_NAME_CUSTOM_GAMMA;
  s->opt[OPT_CUSTOM_GAMMA].title = SANE_TITLE_CUSTOM_GAMMA;
  s->opt[OPT_CUSTOM_GAMMA].desc = SANE_DESC_CUSTOM_GAMMA;
  s->opt[OPT_CUSTOM_GAMMA].type = SANE_TYPE_BOOL;
  s->val[OPT_CUSTOM_GAMMA].w = SANE_FALSE;

  /* grayscale gamma vector */
  s->opt[OPT_GAMMA_VECTOR].name = SANE_NAME_GAMMA_VECTOR;
  s->opt[OPT_GAMMA_VECTOR].title = SANE_TITLE_GAMMA_VECTOR;
  s->opt[OPT_GAMMA_VECTOR].desc = SANE_DESC_GAMMA_VECTOR;
  s->opt[OPT_GAMMA_VECTOR].type = SANE_TYPE_INT;
  s->opt[OPT_GAMMA_VECTOR].unit = SANE_UNIT_NONE;
  s->opt[OPT_GAMMA_VECTOR].constraint_type = SANE_CONSTRAINT_RANGE;
  s->val[OPT_GAMMA_VECTOR].wa = &(s->gamma_table[0][0]);
  s->opt[OPT_GAMMA_VECTOR].constraint.range = &u8_range;
  s->opt[OPT_GAMMA_VECTOR].size = s->gamma_length * sizeof (SANE_Word);

  /* red gamma vector */
  s->opt[OPT_GAMMA_VECTOR_R].name = SANE_NAME_GAMMA_VECTOR_R;
  s->opt[OPT_GAMMA_VECTOR_R].title = SANE_TITLE_GAMMA_VECTOR_R;
  s->opt[OPT_GAMMA_VECTOR_R].desc = SANE_DESC_GAMMA_VECTOR_R;
  s->opt[OPT_GAMMA_VECTOR_R].type = SANE_TYPE_INT;
  s->opt[OPT_GAMMA_VECTOR_R].unit = SANE_UNIT_NONE;
  s->opt[OPT_GAMMA_VECTOR_R].constraint_type = SANE_CONSTRAINT_RANGE;
  s->val[OPT_GAMMA_VECTOR_R].wa = &(s->gamma_table[1][0]);
  s->opt[OPT_GAMMA_VECTOR_R].constraint.range = &(s->gamma_range);
  s->opt[OPT_GAMMA_VECTOR_R].size = s->gamma_length * sizeof (SANE_Word);

  /* green gamma vector */
  s->opt[OPT_GAMMA_VECTOR_G].name = SANE_NAME_GAMMA_VECTOR_G;
  s->opt[OPT_GAMMA_VECTOR_G].title = SANE_TITLE_GAMMA_VECTOR_G;
  s->opt[OPT_GAMMA_VECTOR_G].desc = SANE_DESC_GAMMA_VECTOR_G;
  s->opt[OPT_GAMMA_VECTOR_G].type = SANE_TYPE_INT;
  s->opt[OPT_GAMMA_VECTOR_G].unit = SANE_UNIT_NONE;
  s->opt[OPT_GAMMA_VECTOR_G].constraint_type = SANE_CONSTRAINT_RANGE;
  s->val[OPT_GAMMA_VECTOR_G].wa = &(s->gamma_table[2][0]);
  s->opt[OPT_GAMMA_VECTOR_G].constraint.range = &(s->gamma_range);
  s->opt[OPT_GAMMA_VECTOR_G].size = s->gamma_length * sizeof (SANE_Word);

  /* blue gamma vector */
  s->opt[OPT_GAMMA_VECTOR_B].name = SANE_NAME_GAMMA_VECTOR_B;
  s->opt[OPT_GAMMA_VECTOR_B].title = SANE_TITLE_GAMMA_VECTOR_B;
  s->opt[OPT_GAMMA_VECTOR_B].desc = SANE_DESC_GAMMA_VECTOR_B;
  s->opt[OPT_GAMMA_VECTOR_B].type = SANE_TYPE_INT;
  s->opt[OPT_GAMMA_VECTOR_B].unit = SANE_UNIT_NONE;
  s->opt[OPT_GAMMA_VECTOR_B].constraint_type = SANE_CONSTRAINT_RANGE;
  s->val[OPT_GAMMA_VECTOR_B].wa = &(s->gamma_table[3][0]);
  s->opt[OPT_GAMMA_VECTOR_B].constraint.range = &(s->gamma_range);
  s->opt[OPT_GAMMA_VECTOR_B].size = s->gamma_length * sizeof (SANE_Word);

  if (s->hw->flags & ARTEC_FLAG_GAMMA_SINGLE)
    {
      s->opt[OPT_GAMMA_VECTOR_R].cap |= SANE_CAP_INACTIVE;
      s->opt[OPT_GAMMA_VECTOR_G].cap |= SANE_CAP_INACTIVE;
      s->opt[OPT_GAMMA_VECTOR_B].cap |= SANE_CAP_INACTIVE;
    }

  if (!(s->hw->flags & ARTEC_FLAG_GAMMA))
    {
      s->opt[OPT_GAMMA_VECTOR_R].cap |= SANE_CAP_INACTIVE;
      s->opt[OPT_GAMMA_VECTOR_G].cap |= SANE_CAP_INACTIVE;
      s->opt[OPT_GAMMA_VECTOR_B].cap |= SANE_CAP_INACTIVE;
      s->opt[OPT_CUSTOM_GAMMA].cap |= SANE_CAP_INACTIVE;
      s->opt[OPT_GAMMA_VECTOR].cap |= SANE_CAP_INACTIVE;
    }

  return (SANE_STATUS_GOOD);
}

static SANE_Status
do_cancel (ARTEC_Scanner * s)
{
  DBG (7, "do_cancel\n");

  s->scanning = SANE_FALSE;

  /* DAL: Terminate a three pass scan properly */
  if (s->this_pass == 3)
    s->this_pass = 0;


  if (s->fd >= 0)
    {
      sanei_scsi_close (s->fd);
      s->fd = -1;
    }

  return (SANE_STATUS_CANCELLED);
}


SANE_Status
attach_one (const char *dev)
{
  DBG (7, "attach_one\n");

  attach (dev, 0);
  return (SANE_STATUS_GOOD);
}


SANE_Status
sane_init (SANE_Int * version_code, SANE_Auth_Callback authorize)
{
  char dev_name[PATH_MAX], *cp;
  size_t len;
  FILE *fp;

  DBG_INIT ();

  DBG (1, "Artec/Ultima backend version %d.%d, last mod: %s\n",
       ARTEC_MAJOR, ARTEC_MINOR, ARTEC_LAST_MOD);
  DBG (1, "http://www4.infi.net/~cpinkham/sane-artec-doc.html\n");

  /* make sure these 2 are empty */
  strcpy (artec_vendor, "");
  strcpy (artec_model, "");

  if (version_code)
    *version_code = SANE_VERSION_CODE (V_MAJOR, V_MINOR, 0);

  fp = sanei_config_open (ARTEC_CONFIG_FILE);
  if (!fp)
    {
      /* default to /dev/scanner instead of insisting on config file */
      attach ("/dev/scanner", 0);
      return (SANE_STATUS_GOOD);
    }

  while (fgets (dev_name, sizeof (dev_name), fp))
    {
      cp = (char *) sanei_config_skip_whitespace (dev_name);

      if ((!*cp) || (*cp == '#'))	/* ignore line comments and blank lines */
	continue;

      len = strlen (cp);
      if (cp[len - 1] == '\n')
	cp[--len] = '\0';

      if (!len)
	continue;		/* ignore empty lines */

      DBG (100, "%s line: '%s', len = %d\n", ARTEC_CONFIG_FILE, cp, len);

      /* check to see if they forced a vendor string in artec.conf */
      if ((strncmp (cp, "vendor", 6) == 0) && isspace (cp[6]))
	{
	  cp += 7;
	  cp = (char *) sanei_config_skip_whitespace (cp);

	  strcpy (artec_vendor, cp);
	  DBG (5, "sane_init: Forced vendor string '%s' in %s.\n",
	       cp, ARTEC_CONFIG_FILE);
	}
      /* OK, maybe they forced the model string in artec.conf */
      else if ((strncmp (cp, "model", 5) == 0) && isspace (cp[5]))
	{
	  cp += 6;
	  cp = (char *) sanei_config_skip_whitespace (cp);

	  strcpy (artec_model, cp);
	  DBG (5, "sane_init: Forced model string '%s' in %s.\n",
	       cp, ARTEC_CONFIG_FILE);
	}
      /* well, nothing else to do but attempt the attach */
      else
	{
	  sanei_config_attach_matching_devices (dev_name, attach_one);
	  strcpy (artec_vendor, "");
	  strcpy (artec_model, "");
	}
    }
  fclose (fp);

  return (SANE_STATUS_GOOD);
}

void
sane_exit (void)
{
  ARTEC_Device *dev, *next;

  DBG (7, "sane_exit\n");

  for (dev = first_dev; dev; dev = next)
    {
      next = dev->next;
      free ((void *) dev->sane.name);
      free ((void *) dev->sane.model);
      free (dev);
    }
}

SANE_Status
sane_get_devices (const SANE_Device *** device_list, SANE_Bool local_only)
{
  static const SANE_Device **devlist = 0;
  ARTEC_Device *dev;
  int i;

  DBG (7, "sane_get_devices\n");

  if (devlist)
    free (devlist);

  devlist = malloc ((num_devices + 1) * sizeof (devlist[0]));
  if (!devlist)
    return SANE_STATUS_NO_MEM;

  i = 0;
  for (dev = first_dev; i < num_devices; dev = dev->next)
    devlist[i++] = &dev->sane;
  devlist[i++] = 0;

  *device_list = devlist;

  return (SANE_STATUS_GOOD);
}

SANE_Status
sane_open (SANE_String_Const devicename, SANE_Handle * handle)
{
  SANE_Status status;
  ARTEC_Device *dev;
  ARTEC_Scanner *s;
  int i, j;

  DBG (7, "sane_open\n");

  if (devicename[0])
    {
      for (dev = first_dev; dev; dev = dev->next)
	if (strcmp (dev->sane.name, devicename) == 0)
	  break;

      if (!dev)
	{
	  status = attach (devicename, &dev);
	  if (status != SANE_STATUS_GOOD)
	    return (status);
	}
    }
  else
    {
      /* empty devicname -> use first device */
      dev = first_dev;
    }

  if (!dev)
    return SANE_STATUS_INVAL;

  s = malloc (sizeof (*s));
  if (!s)
    return SANE_STATUS_NO_MEM;
  memset (s, 0, sizeof (*s));
  s->fd = -1;
  s->hw = dev;
  s->this_pass = 0;

  s->gamma_length = s->hw->gamma_length;
  s->gamma_range.min = 0;
  s->gamma_range.max = s->gamma_length - 1;
  s->gamma_range.quant = 0;

  /* not sure if I need this or not, it was in the umax backend though. :-) */
  for (j = 0; j < s->gamma_length; ++j)
    {
      s->gamma_table[0][j] = j * (s->gamma_length - 1) / s->gamma_length;
    }

  for (i = 1; i < 4; i++)
    {
      for (j = 0; j < s->gamma_length; ++j)
	{
	  s->gamma_table[i][j] = j;
	}
    }

  init_options (s);

  /* insert newly opened handle into list of open handles: */
  s->next = first_handle;
  first_handle = s;

  *handle = s;

  if (s->hw->flags & ARTEC_FLAG_CALIBRATE)
    {
      status = sanei_scsi_open (s->hw->sane.name, &s->fd, 0, 0);

      if (status != SANE_STATUS_GOOD)
	{
	  DBG (1, "error opening scanner for initial calibration: %s\n",
	       sane_strstatus (status));
	  s->fd = -1;
	  return status;
	}

      status = artec_calibrate_shading (s);

      if (status != SANE_STATUS_GOOD)
	{
	  DBG (1, "initial shading calibration failed: %s\n",
	       sane_strstatus (status));
	  sanei_scsi_close (s->fd);
	  s->fd = -1;
	  return status;
	}

      sanei_scsi_close (s->fd);
    }

  return (SANE_STATUS_GOOD);
}

void
sane_close (SANE_Handle handle)
{
  ARTEC_Scanner *prev, *s;

  DBG (7, "sane_close\n");

  /* remove handle from list of open handles: */
  prev = 0;
  for (s = first_handle; s; s = s->next)
    {
      if (s == handle)
	break;
      prev = s;
    }
  if (!s)
    {
      DBG (1, "close: invalid handle %p\n", handle);
      return;			/* oops, not a handle we know about */
    }

  if (s->scanning)
    do_cancel (handle);


  if (prev)
    prev->next = s->next;
  else
    first_handle = s;

  free (handle);
}

const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle handle, SANE_Int option)
{
  ARTEC_Scanner *s = handle;

  if ((unsigned) option >= NUM_OPTIONS)
    return (0);

  return (s->opt + option);
}

SANE_Status
sane_control_option (SANE_Handle handle, SANE_Int option,
		     SANE_Action action, void *val, SANE_Int * info)
{
  ARTEC_Scanner *s = handle;
  SANE_Status status;
  SANE_Word w, cap;

  if (info)
    *info = 0;

  if (s->scanning)
    return SANE_STATUS_DEVICE_BUSY;

  if (s->this_pass)
    return SANE_STATUS_DEVICE_BUSY;

  if (option >= NUM_OPTIONS)
    return SANE_STATUS_INVAL;

  cap = s->opt[option].cap;

  if (!SANE_OPTION_IS_ACTIVE (cap))
    return SANE_STATUS_INVAL;

  if (action == SANE_ACTION_GET_VALUE)
    {
      DBG (13, "sane_control_option %d, get value\n", option);

      switch (option)
	{
	  /* word options: */
	case OPT_X_RESOLUTION:
	case OPT_Y_RESOLUTION:
	case OPT_PREVIEW:
	case OPT_GRAY_PREVIEW:
	case OPT_RESOLUTION_BIND:
	case OPT_NEGATIVE:
	case OPT_TL_X:
	case OPT_TL_Y:
	case OPT_BR_X:
	case OPT_BR_Y:
	case OPT_NUM_OPTS:
	case OPT_QUALITY_CAL:
	case OPT_SOFTWARE_CAL:
	case OPT_CONTRAST:
	case OPT_THRESHOLD:
	case OPT_CUSTOM_GAMMA:
	  *(SANE_Word *) val = s->val[option].w;
	  return (SANE_STATUS_GOOD);

	  /* string options: */
	case OPT_MODE:
	case OPT_FILTER_TYPE:
	case OPT_HALFTONE_PATTERN:
	  strcpy (val, s->val[option].s);
	  return (SANE_STATUS_GOOD);

	  /* word array options: */
	case OPT_GAMMA_VECTOR:
	case OPT_GAMMA_VECTOR_R:
	case OPT_GAMMA_VECTOR_G:
	case OPT_GAMMA_VECTOR_B:
	  memcpy (val, s->val[option].wa, s->opt[option].size);
	  return (SANE_STATUS_GOOD);
	}
    }
  else if (action == SANE_ACTION_SET_VALUE)
    {
      DBG (13, "sane_control_option %d, set value\n", option);

      if (!SANE_OPTION_IS_SETTABLE (cap))
	return (SANE_STATUS_INVAL);

      status = sanei_constrain_value (s->opt + option, val, info);
      if (status != SANE_STATUS_GOOD)
	return (status);

      switch (option)
	{
	  /* (mostly) side-effect-free word options: */
	case OPT_X_RESOLUTION:
	case OPT_Y_RESOLUTION:
	case OPT_BR_X:
	case OPT_BR_Y:
	case OPT_TL_X:
	case OPT_TL_Y:
	  if (info && s->val[option].w != *(SANE_Word *) val)
	    *info |= SANE_INFO_RELOAD_PARAMS;

	  /* fall through */
	case OPT_PREVIEW:
	case OPT_GRAY_PREVIEW:
	case OPT_QUALITY_CAL:
	case OPT_SOFTWARE_CAL:
	case OPT_NUM_OPTS:
	case OPT_NEGATIVE:
	case OPT_CONTRAST:
	case OPT_THRESHOLD:
	  s->val[option].w = *(SANE_Word *) val;
	  return (SANE_STATUS_GOOD);

	case OPT_MODE:
	  {
	    int halftoning;

	    if (s->val[option].s)
	      free (s->val[option].s);

	    s->val[option].s = (SANE_Char *) strdup (val);

	    if (info)
	      *info |= SANE_INFO_RELOAD_OPTIONS | SANE_INFO_RELOAD_PARAMS;

	    s->val[OPT_CUSTOM_GAMMA].w = SANE_FALSE;

	    /* s->opt[OPT_NEGATIVE].cap           |= SANE_CAP_INACTIVE; */
	    s->opt[OPT_GAMMA_VECTOR].cap |= SANE_CAP_INACTIVE;
	    s->opt[OPT_GAMMA_VECTOR_R].cap |= SANE_CAP_INACTIVE;
	    s->opt[OPT_GAMMA_VECTOR_G].cap |= SANE_CAP_INACTIVE;
	    s->opt[OPT_GAMMA_VECTOR_B].cap |= SANE_CAP_INACTIVE;
	    s->opt[OPT_CONTRAST].cap |= SANE_CAP_INACTIVE;
	    s->opt[OPT_THRESHOLD].cap |= SANE_CAP_INACTIVE;
	    s->opt[OPT_FILTER_TYPE].cap |= SANE_CAP_INACTIVE;
	    s->opt[OPT_BRIGHTNESS].cap |= SANE_CAP_INACTIVE;
	    s->opt[OPT_HALFTONE_PATTERN].cap |= SANE_CAP_INACTIVE;


	    halftoning = (strcmp (val, "Halftone") == 0);

	    if (halftoning || (strcmp (val, "Lineart") == 0))
	      {
		/* s->opt[OPT_NEGATIVE].cap &= ~SANE_CAP_INACTIVE; */
		s->opt[OPT_FILTER_TYPE].cap &= ~SANE_CAP_INACTIVE;

		if (halftoning)
		  {
		    /* halftone mode */
		    s->opt[OPT_CONTRAST].cap &= ~SANE_CAP_INACTIVE;
		    s->opt[OPT_HALFTONE_PATTERN].cap &= ~SANE_CAP_INACTIVE;

		    if (s->hw->flags & ARTEC_FLAG_OPT_BRIGHTNESS)
		      s->opt[OPT_BRIGHTNESS].cap &= ~SANE_CAP_INACTIVE;
		  }
		else
		  {
		    /* lineart mode */
		    s->opt[OPT_THRESHOLD].cap &= ~SANE_CAP_INACTIVE;
		  }
	      }
	  }
	  return (SANE_STATUS_GOOD);

	case OPT_FILTER_TYPE:
	case OPT_HALFTONE_PATTERN:
	  if (s->val[option].s)
	    free (s->val[option].s);
	  s->val[option].s = strdup (val);
	  return (SANE_STATUS_GOOD);

	case OPT_RESOLUTION_BIND:
	  if (s->val[option].w != *(SANE_Word *) val)
	    {
	      s->val[option].w = *(SANE_Word *) val;

	      if (info)
		{
		  *info |= SANE_INFO_RELOAD_OPTIONS;
		}

	      if (s->val[option].w == SANE_FALSE)
		{		/* don't bind */
		  s->opt[OPT_Y_RESOLUTION].cap &= ~SANE_CAP_INACTIVE;
		  s->opt[OPT_X_RESOLUTION].title =
		    SANE_TITLE_SCAN_X_RESOLUTION;
		  s->opt[OPT_X_RESOLUTION].name =
		    SANE_NAME_SCAN_X_RESOLUTION;
		  s->opt[OPT_X_RESOLUTION].desc =
		    SANE_DESC_SCAN_X_RESOLUTION;
		}
	      else
		{		/* bind */
		  s->opt[OPT_Y_RESOLUTION].cap |= SANE_CAP_INACTIVE;
		  s->opt[OPT_X_RESOLUTION].title =
		    SANE_TITLE_SCAN_RESOLUTION;
		  s->opt[OPT_X_RESOLUTION].name =
		    SANE_NAME_SCAN_RESOLUTION;
		  s->opt[OPT_X_RESOLUTION].desc =
		    SANE_DESC_SCAN_RESOLUTION;
		}
	    }
	  return (SANE_STATUS_GOOD);

	  /* side-effect-free word-array options: */
	case OPT_GAMMA_VECTOR:
	case OPT_GAMMA_VECTOR_R:
	case OPT_GAMMA_VECTOR_G:
	case OPT_GAMMA_VECTOR_B:
	  memcpy (s->val[option].wa, val, s->opt[option].size);
	  return (SANE_STATUS_GOOD);

	  /* options with side effects: */
	case OPT_CUSTOM_GAMMA:
	  w = *(SANE_Word *) val;
	  if (w == s->val[OPT_CUSTOM_GAMMA].w)
	    return (SANE_STATUS_GOOD);

	  s->val[OPT_CUSTOM_GAMMA].w = w;
	  if (w)		/* use custom_gamma_table */
	    {
	      const char *mode = s->val[OPT_MODE].s;

	      if ((strcmp (mode, "Lineart") == 0) ||
		  (strcmp (mode, "Halftone") == 0) ||
		  (strcmp (mode, "Gray") == 0))
		{
		  s->opt[OPT_GAMMA_VECTOR].cap &= ~SANE_CAP_INACTIVE;
		}
	      else if (strcmp (mode, "Color") == 0)
		{
		  s->opt[OPT_GAMMA_VECTOR].cap &= ~SANE_CAP_INACTIVE;

		  if (!(s->hw->flags & ARTEC_FLAG_GAMMA_SINGLE))
		    {
		      s->opt[OPT_GAMMA_VECTOR_R].cap &= ~SANE_CAP_INACTIVE;
		      s->opt[OPT_GAMMA_VECTOR_G].cap &= ~SANE_CAP_INACTIVE;
		      s->opt[OPT_GAMMA_VECTOR_B].cap &= ~SANE_CAP_INACTIVE;
		    }
		}
	    }
	  else
	    /* don't use custom_gamma_table */
	    {
	      s->opt[OPT_GAMMA_VECTOR].cap |= SANE_CAP_INACTIVE;
	      s->opt[OPT_GAMMA_VECTOR_R].cap |= SANE_CAP_INACTIVE;
	      s->opt[OPT_GAMMA_VECTOR_G].cap |= SANE_CAP_INACTIVE;
	      s->opt[OPT_GAMMA_VECTOR_B].cap |= SANE_CAP_INACTIVE;
	    }

	  if (info)
	    *info |= SANE_INFO_RELOAD_OPTIONS;

	  return (SANE_STATUS_GOOD);
	}
    }

  return (SANE_STATUS_INVAL);
}

void 
set_pass_parameters (SANE_Handle handle)
{
  ARTEC_Scanner *s = handle;

  if (s->threepasscolor)
    {
      s->this_pass += 1;
      DBG (20, "set_pass_parameters:  three-pass, on %d\n", s->this_pass);
      switch (s->this_pass)
	{
	case 1:
	  s->params.format = SANE_FRAME_RED;
	  s->params.last_frame = SANE_FALSE;
	  break;
	case 2:
	  s->params.format = SANE_FRAME_GREEN;
	  s->params.last_frame = SANE_FALSE;
	  break;
	case 3:
	  s->params.format = SANE_FRAME_BLUE;
	  s->params.last_frame = SANE_TRUE;
	  break;
	default:
	  DBG (20, "set_pass_parameters:  What?!? pass %d = filter?\n",
	       s->this_pass);
	  break;
	}
    }
  else
    s->this_pass = 0;
}

SANE_Status
sane_get_parameters (SANE_Handle handle, SANE_Parameters * params)
{
  ARTEC_Scanner *s = handle;

  if (!s->scanning)
    {
      double width, height;

      memset (&s->params, 0, sizeof (s->params));

      s->x_resolution = s->val[OPT_X_RESOLUTION].w;
      s->y_resolution = s->val[OPT_Y_RESOLUTION].w;

      if ((s->val[OPT_RESOLUTION_BIND].w == SANE_TRUE) ||
	  (s->val[OPT_PREVIEW].w == SANE_TRUE))
	{
	  s->y_resolution = s->x_resolution;
	}

      s->tl_x = SANE_UNFIX (s->val[OPT_TL_X].w) / MM_PER_INCH
	* s->x_resolution;
      s->tl_y = SANE_UNFIX (s->val[OPT_TL_Y].w) / MM_PER_INCH
	* s->y_resolution;
      width = SANE_UNFIX (s->val[OPT_BR_X].w - s->val[OPT_TL_X].w);
      height = SANE_UNFIX (s->val[OPT_BR_Y].w - s->val[OPT_TL_Y].w);

      if ((s->x_resolution > 0.0) &&
	  (s->y_resolution > 0.0) &&
	  (width > 0.0) &&
	  (height > 0.0))
	{
	  s->params.pixels_per_line = width * s->x_resolution / MM_PER_INCH + 1;
	  s->params.lines = height * s->y_resolution / MM_PER_INCH + 1;
	}

      s->onepasscolor = SANE_FALSE;
      s->threepasscolor = SANE_FALSE;
      s->params.last_frame = SANE_TRUE;

      if ((s->val[OPT_PREVIEW].w == SANE_TRUE) &&
	  (s->val[OPT_GRAY_PREVIEW].w == SANE_TRUE))
	{
	  s->mode = "Gray";
	}
      else
	{
	  s->mode = s->val[OPT_MODE].s;
	}

      if ((strcmp (s->mode, "Lineart") == 0) ||
	  (strcmp (s->mode, "Halftone") == 0))
	{
	  s->params.format = SANE_FRAME_GRAY;
	  s->params.bytes_per_line = (s->params.pixels_per_line + 7) / 8;
	  s->params.depth = 1;
	  s->line_offset = 0;
	}
      else if (strcmp (s->mode, "Gray") == 0)
	{
	  s->params.format = SANE_FRAME_GRAY;
	  s->params.bytes_per_line = s->params.pixels_per_line;
	  s->params.depth = 8;
	  s->line_offset = 0;
	}
      else
	{
	  s->params.bytes_per_line = s->params.pixels_per_line;
	  s->params.depth = 8;

	  if (s->hw->flags & ARTEC_FLAG_ONE_PASS_SCANNER)
	    {
	      s->onepasscolor = SANE_TRUE;
	      s->params.format = SANE_FRAME_RGB;
	      s->params.bytes_per_line *= 3;

	      switch (s->y_resolution)
		{
		case 600:
		  s->line_offset = 16;
		  break;
		case 300:
		  s->line_offset = 8;
		  break;
		case 200:
		  s->line_offset = 5;
		  break;
		case 100:
		  s->line_offset = 2;
		  break;
		case 50:
		  s->line_offset = 1;
		  break;
		default:
		  s->line_offset = 8 *
		    (int) (s->y_resolution / 300.0);
		  break;
		}
	    }
	  else
	    {
	      s->params.last_frame = SANE_FALSE;
	      s->threepasscolor = SANE_TRUE;
	      s->line_offset = 0;
	    }
	}
    }

  if (params)
    *params = s->params;

  return (SANE_STATUS_GOOD);
}

SANE_Status
sane_start (SANE_Handle handle)
{
  ARTEC_Scanner *s = handle;
  SANE_Status status;

  DBG (7, "sane_start\n");

  /* First make sure we have a current parameter set.  Some of the
   * parameters will be overwritten below, but that's OK.  */
  status = sane_get_parameters (s, 0);
  if (status != SANE_STATUS_GOOD)
    return status;

  /* DAL: This is part of the RGB 3 pass fix */
  if ((strcmp (s->mode, "Color") == 0) && s->threepasscolor)
    set_pass_parameters (s);

  /* DAL: This is part of the RGB 3 pass fix */
  if ((strcmp (s->mode, "Color") != 0) || !s->threepasscolor ||
      (s->threepasscolor && s->this_pass == 1))
    {

      if (s->hw->flags & ARTEC_FLAG_SENSE_HANDLER)
	{
	  status = sanei_scsi_open (s->hw->sane.name, &s->fd, sense_handler, 0);
	}
      else
	{
	  status = sanei_scsi_open (s->hw->sane.name, &s->fd, 0, 0);
	}

      if (status != SANE_STATUS_GOOD)
	{
	  DBG (1, "open of %s failed: %s\n",
	       s->hw->sane.name, sane_strstatus (status));
	  return status;
	}

      /* DB added wait_ready */
      status = wait_ready (s->fd);
      if (status != SANE_STATUS_GOOD)
	{
	  DBG (1, "wait for scanner ready failed: %s\n", sane_strstatus (status));
	  return status;
	}
    }

  s->bytes_to_read = s->params.bytes_per_line * s->params.lines;

  DBG (20, "%d pixels per line, %d bytes, %d lines high, xdpi = %d, "
       "ydpi = %d, btr = %lu\n",
       s->params.pixels_per_line, s->params.bytes_per_line, s->params.lines,
       s->x_resolution, s->y_resolution, (u_long) s->bytes_to_read);

  /* DAL: This is part of the RGB 3 pass fix */
  if ((strcmp (s->mode, "Color") != 0) || !s->threepasscolor ||
      (s->threepasscolor && s->this_pass == 1))
    {

      /* do a calibrate if scanner requires/recommends it */
      if ((s->hw->flags & ARTEC_FLAG_CALIBRATE) &&
	  (s->val[OPT_QUALITY_CAL].w == SANE_TRUE))
	{
	  status = artec_calibrate_shading (s);

	  if (status != SANE_STATUS_GOOD)
	    {
	      DBG (1, "shading calibration failed: %s\n",
		   sane_strstatus (status));
	      return status;
	    }
	}

      /* DB added wait_ready */
      status = wait_ready (s->fd);
      if (status != SANE_STATUS_GOOD)
	{
	  DBG (1, "wait for scanner ready failed: %s\n", sane_strstatus (status));
	  return status;
	}

      /* send the custom gamma table if we have one */
      if (s->hw->flags & ARTEC_FLAG_GAMMA)
	artec_send_gamma_table (s);

      /* now set our scan window */
      status = artec_set_scan_window (s);

      if (status != SANE_STATUS_GOOD)
	{
	  DBG (1, "set scan window failed: %s\n",
	       sane_strstatus (status));
	  return status;
	}

      /* DB added wait_ready */
      status = wait_ready (s->fd);
      if (status != SANE_STATUS_GOOD)
	{
	  DBG (1, "wait for scanner ready failed: %s\n", sane_strstatus (status));
	  return status;
	}
    }

  /* now we can start the actual scan */
  /* DAL: This is part of the RGB 3 scan fix */
  if ((strcmp (s->mode, "Color") != 0) || !s->threepasscolor || s->this_pass == 1)
    {
      status = artec_start_scan (s);

      if (status != SANE_STATUS_GOOD)
	{
	  DBG (1, "start scan: %s\n",
	       sane_strstatus (status));
	  return status;
	}
    }

  s->scanning = SANE_TRUE;

  return (SANE_STATUS_GOOD);
}

void 
binout (SANE_Byte byte)
{
  SANE_Byte b = byte;
  int bit;

  for (bit = 0; bit < 8; bit++)
    {
      DBG (7, "%d", b & 128 ? 1 : 0);
      b = b << 1;
    }
}

SANE_Status
artec_sane_read (SANE_Handle handle, SANE_Byte * buf, SANE_Int max_len, SANE_Int * len)
{
  ARTEC_Scanner *s = handle;
  SANE_Status status;
  size_t nread;
  size_t lread;
  int bytes_read;
  int rows_read;
  int max_read_rows;
  int max_ret_rows;
  /* DAL: Changed usage of above variables and added one more */
  int remaining_rows;

  int rows_available;
  int line;
  SANE_Byte temp_buf[ARTEC_MAX_READ_SIZE];
  SANE_Byte line_buf[ARTEC_MAX_READ_SIZE];

  DBG (7, "artec_sane_read( %p, %p, %d, %d )\n", handle, buf, max_len, *len);

  *len = 0;

  if (s->bytes_to_read == 0)
    {
      if ((strcmp (s->mode, "Color") != 0) || !s->threepasscolor ||
	  (s->threepasscolor && s->this_pass == 3))
	{
	  do_cancel (s);
	  s->scanning = SANE_FALSE;	/* without this a 4th pass is attempted, yet do_cancel does this */
	}
      return (SANE_STATUS_EOF);
    }

  if (!s->scanning)
    return do_cancel (s);

  remaining_rows = (s->bytes_to_read + s->params.bytes_per_line - 1) / s->params.bytes_per_line;
  max_read_rows = s->hw->max_read_size / s->params.bytes_per_line;
  max_ret_rows = max_len / s->params.bytes_per_line;

  while (artec_get_status (s->fd) == 0)
    {
      DBG (100, "hokey loop till data available\n");
      usleep (50000);		/* sleep for .05 second */
    }

  rows_read = 0;
  bytes_read = 0;
  while ((rows_read < max_ret_rows) && (rows_read < remaining_rows))
    {
      DBG (100, "top of while loop, rr = %d, mrr = %d, rem = %d\n",
	   rows_read, max_ret_rows, remaining_rows);

      if (s->bytes_to_read - bytes_read <= s->params.bytes_per_line * max_read_rows)
	{
	  nread = s->bytes_to_read - bytes_read;
	}
      else
	{
	  nread = s->params.bytes_per_line * max_read_rows;
	}
      lread = nread / s->params.bytes_per_line;

      if ((max_read_rows - rows_read) < lread)
	{
	  lread = max_read_rows - rows_read;
	  nread = lread * s->params.bytes_per_line;
	}

      if ((max_ret_rows - rows_read) < lread)
	{
	  lread = max_ret_rows - rows_read;
	  nread = lread * s->params.bytes_per_line;
	}

      while ((rows_available = artec_get_status (s->fd)) == 0)
	{
	  DBG (100, "hokey loop till data available\n");
	  usleep (50000);	/* sleep for .05 second */
	}

/* **** This is a test for 1BPP modes: there is a discrepancy
   **** between "lines of bpl" and "lines of ppl":
   **** "rows_available" refer to "lines of ppl"
   **** so reading "lines of bpl" could result in image corruption! */
      if (((strcmp (s->mode, "Lineart") == 0) ||
	   (strcmp (s->mode, "Halftone") == 0)) &&
	  (s->hw->flags & ARTEC_FLAG_MONO_ADJUST))
	{
	  while ((rows_available = artec_get_status (s->fd)) < lread)
	    {
	      DBG (100, "hokey loop till data available\n");
	      usleep (50000);	/* sleep for .05 second */
	    }
	}

      if (rows_available < lread)
	{
	  lread = rows_available;
	  nread = lread * s->params.bytes_per_line;
	}

      /* This should never happen, but just in case... */
      if (nread > s->bytes_to_read - bytes_read)
	{
	  nread = s->bytes_to_read - bytes_read;
	  lread = 1;
	}

      /* DB check if done reading (lineart and halftone only?)
       * the number of pre-computed bytes_to_read is not accurate
       * so follow the number of available rows returned from the scanner
       * the last time it is set (AT12) to the maximum again
       * Reason is when asking for a read with a number of bytes less than a line
       * you get a timeout on the read command with the AT12
       */
      DBG (20, "rows_available=%d,params.lines=%d,btr=%lu,bpl=%d\n",
	   rows_available, s->params.lines,
	   (u_long) s->bytes_to_read, s->params.bytes_per_line);
      /* kludge for the ??AT12 model?? */
      /* this breaks the AT3 & A6000C+ if turned on for them */
      if ((!strcmp (s->hw->sane.model, "AT12")) &&
	  ((strcmp (s->mode, "Lineart") == 0) ||
	   (strcmp (s->mode, "Halftone") == 0)))
	{
	  /* DB byte alignment for the AT12 */
	  nread = (8192 / s->params.bytes_per_line) * s->params.bytes_per_line;
	  if ((rows_available == s->params.lines) &&
	   s->bytes_to_read != (s->params.lines * s->params.bytes_per_line))
	    {
	      s->bytes_to_read = 0;
	      end_scan (s);
	      do_cancel (s);
	      return (SANE_STATUS_EOF);
	    }
	}

      DBG (20, "bytes_to_read = %lu, max_len = %d, max_rows = %d, "
	   "rows_avail = %d\n",
	   (u_long) s->bytes_to_read, max_len, max_ret_rows, rows_available);
      DBG (20, "nread = %lu, lread = %lu, bytes_read = %d, rows_read = %d\n",
	   (u_long) nread, (u_long) lread, bytes_read, rows_read);

      status = read_data (s->fd, ARTEC_DATA_IMAGE, temp_buf, &nread);

      if (status != SANE_STATUS_GOOD)
	{
	  end_scan (s);
	  do_cancel (s);
	  return (SANE_STATUS_IO_ERROR);
	}

      if ((strcmp (s->mode, "Color") == 0) &&
	  (s->hw->flags & ARTEC_FLAG_RGB_LINE_OFFSET))
	{
	  for (line = 0; line < lread; line++)
	    {
	      memcpy (line_buf,
		      temp_buf + (line * s->params.bytes_per_line),
		      s->params.bytes_per_line);

	      nread = s->params.bytes_per_line;

	      artec_buffer_line_offset (s->line_offset, line_buf, &nread);

	      if (nread > 0)
		{
		  if (s->hw->flags & ARTEC_FLAG_RGB_CHAR_SHIFT)
		    {
		      artec_line_rgb_to_byte_rgb (line_buf,
						  s->params.pixels_per_line);
		    }
		  if (s->hw->flags & ARTEC_FLAG_IMAGE_REV_LR)
		    {
		      artec_reverse_line (s, line_buf);
		    }

		  /* do software calibration if necessary */
		  if (s->val[OPT_SOFTWARE_CAL].w)
		    {
		      artec_software_rgb_calibrate (s, line_buf, 1);
		    }

		  memcpy (buf + bytes_read, line_buf,
			  s->params.bytes_per_line);
		  bytes_read += nread;
		  rows_read++;
		}
	    }
	}
      else if (((strcmp (s->mode, "Lineart") == 0) ||
		(strcmp (s->mode, "Halftone") == 0)) &&
	       (s->hw->flags & ARTEC_FLAG_MONO_ADJUST))
	{
	  nread = s->params.bytes_per_line;
	  for (line = 0; line < lread; line++)
	    {
	      memcpy (line_buf, temp_buf + (line * s->params.bytes_per_line),
		      s->params.bytes_per_line);

	      artec_mono_bit_offset (s, line_buf, &nread);

	      memcpy (buf + bytes_read, line_buf, s->params.bytes_per_line);
	      bytes_read += nread;
	      rows_read++;
	    }
	}
      else
	{
	  if ((s->hw->flags & ARTEC_FLAG_IMAGE_REV_LR) ||
	      ((strcmp (s->mode, "Color") == 0) &&
	       (s->hw->flags & ARTEC_FLAG_RGB_CHAR_SHIFT)))
	    {
	      for (line = 0; line < lread; line++)
		{
		  if ((strcmp (s->mode, "Color") == 0) &&
		      (s->hw->flags & ARTEC_FLAG_RGB_CHAR_SHIFT))
		    {
		      artec_line_rgb_to_byte_rgb (temp_buf +
					  (line * s->params.bytes_per_line),
						  s->params.pixels_per_line);
		    }
		  if (s->hw->flags & ARTEC_FLAG_IMAGE_REV_LR)
		    {
		      artec_reverse_line (s, temp_buf +
					  (line * s->params.bytes_per_line));
		    }
		}
	    }

	  /* do software calibration if necessary */
	  if (s->val[OPT_SOFTWARE_CAL].w)
	    {
	      artec_software_rgb_calibrate (s, temp_buf, lread);
	    }

	  memcpy (buf + bytes_read, temp_buf, nread);
	  bytes_read += nread;
	  rows_read += lread;
	}
    }

  *len = bytes_read;
  s->bytes_to_read -= bytes_read;

  DBG (20, "artec_sane_read() returning, we read %lu bytes, %lu left\n",
       (u_long) * len, (u_long) s->bytes_to_read);

  if ((s->bytes_to_read == 0) &&
      (s->hw->flags & ARTEC_FLAG_RGB_LINE_OFFSET) &&
      (tmp_line_buf != NULL))
    {
      artec_buffer_line_offset_free ();
    }

  if ((s->bytes_to_read == 0) && (s->hw->flags & ARTEC_FLAG_MONO_ADJUST) &&
      (tmp_line_buf != NULL))
    {
      artec_mono_buffer_free ();
    }

  return (SANE_STATUS_GOOD);
}

SANE_Status
sane_read (SANE_Handle handle, SANE_Byte * buf, SANE_Int max_len, SANE_Int * len)
{
  ARTEC_Scanner *s = handle;
  SANE_Status status;
  int bytes_to_copy;
  int loop;

  static SANE_Byte temp_buf[ARTEC_MAX_READ_SIZE];
  static int bytes_in_buf = 0;

  DBG (7, "sane_read( %p, %p, %d, %d )\n", handle, buf, max_len, *len);
  DBG (7, "sane_read: bib = %d, ml = %d\n", bytes_in_buf, max_len);

  if (bytes_in_buf != 0)
    {
      bytes_to_copy = max_len < bytes_in_buf ? max_len : bytes_in_buf;
    }
  else
    {
      status = artec_sane_read (s, temp_buf, s->hw->max_read_size, len);

      if (status != SANE_STATUS_GOOD)
	{
	  return (status);
	}

      bytes_in_buf = *len;

      if (*len == 0)
	{
	  return (SANE_STATUS_GOOD);
	}

      bytes_to_copy = max_len < s->hw->max_read_size ?
	max_len : s->hw->max_read_size;
      bytes_to_copy = *len < bytes_to_copy ? *len : bytes_to_copy;
    }

  memcpy (buf, temp_buf, bytes_to_copy);
  bytes_in_buf -= bytes_to_copy;
  *len = bytes_to_copy;

  DBG (7, "sane_read: btc = %d, bib now = %d\n",
       bytes_to_copy, bytes_in_buf);

  for (loop = 0; loop < bytes_in_buf; loop++)
    {
      temp_buf[loop] = temp_buf[loop + bytes_to_copy];
    }

  return (SANE_STATUS_GOOD);
}

void
sane_cancel (SANE_Handle handle)
{
  ARTEC_Scanner *s = handle;

  DBG (7, "sane_cancel\n");

  if (s->scanning)
    {
      s->scanning = SANE_FALSE;

      abort_scan (s);

      do_cancel (s);
    }
}

SANE_Status
sane_set_io_mode (SANE_Handle handle, SANE_Bool non_blocking)
{
  DBG (7, "sane_set_io_mode( %p, %d )\n", handle, non_blocking);

  return (SANE_STATUS_UNSUPPORTED);
}

SANE_Status
sane_get_select_fd (SANE_Handle handle, SANE_Int * fd)
{
  DBG (7, "sane_get_select_fd\n");

  return (SANE_STATUS_UNSUPPORTED);
}
