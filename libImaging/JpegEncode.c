/*
 * The Python Imaging Library.
 * $Id$
 *
 * coder for JPEG data
 *
 * history:
 * 1996-05-06 fl   created
 * 1996-07-16 fl   don't drop last block of encoded data
 * 1996-12-30 fl   added quality and progressive settings
 * 1997-01-08 fl   added streamtype settings
 * 1998-01-31 fl   adapted to libjpeg 6a
 * 1998-07-12 fl   added YCbCr support
 * 2001-04-16 fl   added DPI write support
 *
 * Copyright (c) 1997-2001 by Secret Labs AB
 * Copyright (c) 1995-1997 by Fredrik Lundh
 *
 * See the README file for details on usage and redistribution.
 */


#include "Imaging.h"

#ifdef	HAVE_LIBJPEG

#undef HAVE_PROTOTYPES
#undef HAVE_STDLIB_H
#undef HAVE_STDDEF_H
#undef UINT8
#undef UINT16
#undef UINT32
#undef INT16
#undef INT32

#include "Jpeg.h"

/* -------------------------------------------------------------------- */
/* Suspending output handler						*/
/* -------------------------------------------------------------------- */

METHODDEF(void)
stub(j_compress_ptr cinfo)
{
    /* empty */
}

METHODDEF(boolean)
empty_output_buffer (j_compress_ptr cinfo)
{
    /* Suspension */
    return FALSE;
}

GLOBAL(void)
jpeg_buffer_dest(j_compress_ptr cinfo, JPEGDESTINATION* destination)
{
    cinfo->dest = (void*) destination;

    destination->pub.init_destination = stub;
    destination->pub.empty_output_buffer = empty_output_buffer;
    destination->pub.term_destination = stub;
}


/* -------------------------------------------------------------------- */
/* Error handler							*/
/* -------------------------------------------------------------------- */

METHODDEF(void)
error(j_common_ptr cinfo)
{
  JPEGERROR* error;
  error = (JPEGERROR*) cinfo->err;
  (*cinfo->err->output_message) (cinfo);
  longjmp(error->setjmp_buffer, 1);
}


/* -------------------------------------------------------------------- */
/* Encoder								*/
/* -------------------------------------------------------------------- */

int
ImagingJpegEncode(Imaging im, ImagingCodecState state, UINT8* buf, int bytes)
{
    JPEGENCODERSTATE* context = (JPEGENCODERSTATE*) state->context;
    int ok;

    if (setjmp(context->error.setjmp_buffer)) {
	/* JPEG error handler */
	jpeg_destroy_compress(&context->cinfo);
	state->errcode = IMAGING_CODEC_BROKEN;
	return -1;
    }

    if (!state->state) {

	/* Setup compression context (very similar to the decoder) */
	context->cinfo.err = jpeg_std_error(&context->error.pub);
	context->error.pub.error_exit = error;
	jpeg_create_compress(&context->cinfo);
	jpeg_buffer_dest(&context->cinfo, &context->destination);

        context->extra_offset = 0;

	/* Ready to encode */
	state->state = 1;

    }

    /* Load the destination buffer */
    context->destination.pub.next_output_byte = buf;
    context->destination.pub.free_in_buffer = bytes;

    switch (state->state) {

    case 1:

	context->cinfo.image_width = state->xsize;
	context->cinfo.image_height = state->ysize;

	switch (state->bits) {
        case 8:
            context->cinfo.input_components = 1;
            context->cinfo.in_color_space = JCS_GRAYSCALE;
            break;
        case 24:
            context->cinfo.input_components = 3;
            if (strcmp(im->mode, "YCbCr") == 0)
                context->cinfo.in_color_space = JCS_YCbCr;
            else
                context->cinfo.in_color_space = JCS_RGB;
            break;
        case 32:
            context->cinfo.input_components = 4;
            context->cinfo.in_color_space = JCS_CMYK;
            break;
        default:
            state->errcode = IMAGING_CODEC_CONFIG;
            return -1;
	}

	/* Compressor configuration */
	jpeg_set_defaults(&context->cinfo);
	if (context->quality > 0)
	    jpeg_set_quality(&context->cinfo, context->quality, 1);

	/* Set subsampling options */
	switch (context->subsampling)
	{
		case 0:  /* 1x1 1x1 1x1 (4:4:4) : None */
		{
			context->cinfo.comp_info[0].h_samp_factor = 1;
			context->cinfo.comp_info[0].v_samp_factor = 1;
			context->cinfo.comp_info[1].h_samp_factor = 1;
			context->cinfo.comp_info[1].v_samp_factor = 1;
			context->cinfo.comp_info[2].h_samp_factor = 1;
			context->cinfo.comp_info[2].v_samp_factor = 1;
			break;
		}
		case 1:  /* 2x1, 1x1, 1x1 (4:2:2) : Medium */
		{
			context->cinfo.comp_info[0].h_samp_factor = 2;
			context->cinfo.comp_info[0].v_samp_factor = 1;
			context->cinfo.comp_info[1].h_samp_factor = 1;
			context->cinfo.comp_info[1].v_samp_factor = 1;
			context->cinfo.comp_info[2].h_samp_factor = 1;
			context->cinfo.comp_info[2].v_samp_factor = 1;
			break;
		}
		case 2:  /* 2x2, 1x1, 1x1 (4:1:1) : High */
		{
			context->cinfo.comp_info[0].h_samp_factor = 2;
			context->cinfo.comp_info[0].v_samp_factor = 2;
			context->cinfo.comp_info[1].h_samp_factor = 1;
			context->cinfo.comp_info[1].v_samp_factor = 1;
			context->cinfo.comp_info[2].h_samp_factor = 1;
			context->cinfo.comp_info[2].v_samp_factor = 1;
			break;
		}
		default:
		{
			/* Use the lib's default */
			break;
		}
	}
	if (context->progressive)
	    jpeg_simple_progression(&context->cinfo);
	context->cinfo.smoothing_factor = context->smooth;
	context->cinfo.optimize_coding = (boolean) context->optimize;
        if (context->xdpi > 0 && context->ydpi > 0) {
            context->cinfo.density_unit = 1; /* dots per inch */
            context->cinfo.X_density = context->xdpi;
            context->cinfo.Y_density = context->ydpi;
        }
	switch (context->streamtype) {
	case 1:
	    /* tables only -- not yet implemented */
	    state->errcode = IMAGING_CODEC_CONFIG;
	    return -1;
	case 2:
	    /* image only */
	    jpeg_suppress_tables(&context->cinfo, TRUE);
	    jpeg_start_compress(&context->cinfo, FALSE);
            /* suppress extra section */
            context->extra_offset = context->extra_size;
        //add exif header
        if (context->rawExifLen > 0)
            jpeg_write_marker(&context->cinfo, JPEG_APP0+1, context->rawExif, context->rawExifLen);

	    break;
	default:
	    /* interchange stream */
	    jpeg_start_compress(&context->cinfo, TRUE);
        //add exif header
        if (context->rawExifLen > 0)
            jpeg_write_marker(&context->cinfo, JPEG_APP0+1, context->rawExif, context->rawExifLen);

        break;
	}
	state->state++;
	/* fall through */

    case 2:

        if (context->extra) {
            /* copy extra buffer to output buffer */
            unsigned int n = context->extra_size - context->extra_offset;
            if (n > context->destination.pub.free_in_buffer)
                n = context->destination.pub.free_in_buffer;
            memcpy(context->destination.pub.next_output_byte,
                   context->extra + context->extra_offset, n);
            context->destination.pub.next_output_byte += n;
            context->destination.pub.free_in_buffer -= n;
            context->extra_offset += n;
            if (context->extra_offset >= context->extra_size)
                state->state++;
            else
                break;
        } else
              state->state++;

    case 3:

	ok = 1;
	while (state->y < state->ysize) {
	    state->shuffle(state->buffer,
			   (UINT8*) im->image[state->y + state->yoff] +
			   state->xoff * im->pixelsize, state->xsize);
	    ok = jpeg_write_scanlines(&context->cinfo, &state->buffer, 1);
	    if (ok != 1)
		break;
	    state->y++;
	}

	if (ok != 1)
	    break;
	state->state++;
	/* fall through */

    case 4:

	/* Finish compression */
	if (context->destination.pub.free_in_buffer < 100)
	    break;
	jpeg_finish_compress(&context->cinfo);

	/* Clean up */
        if (context->extra)
            free(context->extra);
	jpeg_destroy_compress(&context->cinfo);
	/* if (jerr.pub.num_warnings) return BROKEN; */
	state->errcode = IMAGING_CODEC_END;
	break;

    }

    /* Return number of bytes in output buffer */
    return context->destination.pub.next_output_byte - buf;

}

const char*
ImagingJpegVersion(void)
{
    static char version[20];
    sprintf(version, "%d.%d", JPEG_LIB_VERSION / 10, JPEG_LIB_VERSION % 10);
    return version;
}

#endif
