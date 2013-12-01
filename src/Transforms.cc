// Image Transform Functions

/*  IIP fcgi server module - image processing routines

    Copyright (C) 2004-2013 Ruven Pillay.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*/


#include <cmath>
#include "Transforms.h"

/* D65 temp 6504.
 */
#define D65_X0 95.0470
#define D65_Y0 100.0
#define D65_Z0 108.8827


static const float _sRGB[3][3] = { {  3.240479, -1.537150, -0.498535 },
				   { -0.969256, 1.875992, 0.041556 },
				   { 0.055648, -0.204043, 1.057311 } };


// Normalization function
void filter_normalize( RawTile& in, std::vector<float>& max, std::vector<float>& min ) {

  float *normdata;
  float v;
  unsigned int np = in.dataLength * 8 / in.bpc;
  unsigned int nc = in.channels;

  float* fptr;
  unsigned int* uiptr;
  unsigned short* usptr;
  unsigned char* ucptr; 

  if( in.bpc == 32 && in.sampleType == FLOATINGPOINT ) {
    normdata = (float*)in.data;
  } else {
    normdata = new float[np];
  }

  for( unsigned int c = 0 ; c<nc ; c++) {
 
    float minc = min[c];
    float diffc = max[c] - minc;
    float invdiffc = fabs(diffc) > 1e-30? 1./diffc : 1e30;

   // Normalize our data
   if( in.bpc == 32 && in.sampleType == FLOATINGPOINT ) {
      fptr = (float*)in.data;
      // Loop through our pixels for floating values 
#pragma ivdep
      for( unsigned int n=c; n<np; n+=nc ){
        normdata[n] = std::isfinite(fptr[n])? (fptr[n] - minc) * invdiffc : 0.0;
      }
    } else if( in.bpc == 32 && in.sampleType == FIXEDPOINT ) {
      uiptr = (unsigned int*)in.data;
      // Loop through our pixels for uint values 
#pragma ivdep
      for( unsigned int n=c; n<np; n+=nc ){
        normdata[n] = (uiptr[n] - minc) * invdiffc;
      }
    } else if( in.bpc == 16 ) {
      usptr = (unsigned short*)in.data;
      // Loop through our unsigned short pixels
#pragma ivdep
      for( unsigned int n=c; n<np; n+=nc ){
        normdata[n] = (usptr[n] - minc) * invdiffc;
      }
    } else {
      ucptr = (unsigned char*)in.data;
      // Loop through our unsigned char pixels
#pragma ivdep
      for( unsigned int n=c; n<np; n+=nc ){
        normdata[n] = (ucptr[n] - minc) * invdiffc;
      }
    }
  }

  if(! (in.bpc == 32 && in.sampleType == FLOATINGPOINT) ) {
    delete[] (float*) in.data;
    in.data = normdata;
    in.bpc = 32;
    in.dataLength = np * in.bpc / 8;
  }

  return;
}


// Hillshading function
void filter_shade( RawTile& in, int h_angle, int v_angle ){

  float o_x, o_y, o_z;
  // Incident light angle
  float a = (h_angle * 2 * 3.14159) / 360.0;
  // We assume a hypotenous of 1.0
  float s_y = cos(a);
  float s_x = sqrt( 1.0 - s_y*s_y );
  if( h_angle > 180 ){
    s_x = -s_x;
  }

  a = (v_angle * 2 * 3.14159) / 360.0;
  float s_z = - sin(a);

  float s_norm = sqrt( s_x*s_x + s_y*s_y + s_z*s_z );
  s_x = s_x / s_norm;
  s_y = s_y / s_norm;
  s_z = s_z / s_norm;

  float *ptr, *buffer, *infptr;

  unsigned int k = 0;
  unsigned int ndata = in.dataLength * 8 / in.bpc;

  infptr= (float*)in.data;
  // Create new (float) data buffer
  buffer = new float[ndata];

  for( int n=0; n<ndata; n+=3 ){
    if( infptr[n] == 0. && infptr[n+1] == 0. && infptr[n+2] == 0. ) {
      o_x = o_y = o_z = 0.;
    }
    else {
      o_x = (float) - ((float)infptr[n]-0.5) * 2.;
      o_y = (float) - ((float)infptr[n+1]-0.5) * 2.;
      o_z = (float) - ((float)infptr[n+2]-0.5) * 2.;
    }


    float dot_product;
    dot_product = (s_x*o_x) + (s_y*o_y) + (s_z*o_z);

    dot_product = 0.5 * dot_product;
    if( dot_product < 0. ) dot_product = 0.;
    if( dot_product > 1. ) dot_product = 1.;

    buffer[k++] = dot_product;
  }


  // Delete old data buffer
  delete[] (float*) in.data;

  in.data = buffer;
  in.channels = 1;
  in.dataLength = in.width * in.height * in.bpc / 8;
}


// Convert a single pixel from CIELAB to sRGB
static void LAB2sRGB( unsigned char *in, unsigned char *out ){

  /* First convert to XYZ
   */
  int l;
  float L, a, b;
  float X, Y, Z;
  double cby, tmp;
  double R, G, B;

  /* Extract our LAB - packed in TIFF as unsigned char for L
     and signed char for a/b. We also need to rescale
     correctly to 0-100 for L and -127 -> +127 for a/b.
  */
  l = in[0];
  L = (float) ( in[0] / 2.55 );
  l = ( (signed char*)in )[1];
  a = (float) l;
  l = ( (signed char*)in )[2];
  b = (float) l;


  if( L < 8.0 ) {
    Y = (L * D65_Y0) / 903.3;
    cby = 7.787 * (Y / D65_Y0) + 16.0 / 116.0;
  }
  else {
    cby = (L + 16.0) / 116.0;
    Y = D65_Y0 * cby * cby * cby;
  }

  tmp = a / 500.0 + cby;
  if( tmp < 0.2069 ) X = D65_X0 * (tmp - 0.13793) / 7.787;
  else X = D65_X0 * tmp * tmp * tmp;

  tmp = cby - b / 200.0;
  if( tmp < 0.2069 ) Z = D65_Z0 * (tmp - 0.13793) / 7.787;
  else Z = D65_Z0 * tmp * tmp * tmp;

  X /= 100.0;
  Y /= 100.0;
  Z /= 100.0;


  /* Then convert to sRGB
   */
  R = (X * _sRGB[0][0]) + (Y * _sRGB[0][1]) + (Z * _sRGB[0][2]);
  G = (X * _sRGB[1][0]) + (Y * _sRGB[1][1]) + (Z * _sRGB[1][2]);
  B = (X * _sRGB[2][0]) + (Y * _sRGB[2][1]) + (Z * _sRGB[2][2]);

  /* Clip any -ve values
   */
  if( R < 0.0 ) R = 0.0;
  if( G < 0.0 ) G = 0.0;
  if( B < 0.0 ) B = 0.0;


  /* We now need to convert these to non-linear display values
   */
  if( R <= 0.0031308 ) R *= 12.92;
  else R = 1.055 * pow( R, 1.0/2.4 ) - 0.055;

  if( G <= 0.0031308 ) G *= 12.92;
  else G = 1.055 * pow( G, 1.0/2.4 ) - 0.055;

  if( B <= 0.0031308 ) B *= 12.92;
  else B = 1.055 * pow( B, 1.0/2.4 ) - 0.055;

  /* Scale to 8bit
   */
  R *= 255.0;
  G *= 255.0;
  B *= 255.0;

  /* Clip to our 8 bit limit
   */
  if( R > 255.0 ) R = 255.0;
  if( G > 255.0 ) G = 255.0;
  if( B > 255.0 ) B = 255.0;


  /* Return our sRGB values
   */
  out[0] = (unsigned char) R;
  out[1] = (unsigned char) G;
  out[2] = (unsigned char) B;

}


// Convert whole tile from CIELAB to sRGB
void filter_LAB2sRGB( RawTile& in ){

  unsigned long np = in.width * in.height * in.channels;

  // Parallelize code using OpenMP
  unsigned int nstep = in.channels;
#pragma omp parallel for
  for( unsigned long n=0; n<np; n+=nstep ){
    unsigned char* ptr = (unsigned char*) in.data;
    unsigned char q[3];
    LAB2sRGB( &ptr[n], &q[0] );
    ((unsigned char*)in.data)[n] = q[0];
    ((unsigned char*)in.data)[n+1] = q[1];
    ((unsigned char*)in.data)[n+2] = q[2];
  }
}

// Colormap function
void filter_cmap( RawTile& in, enum cmap_type cmap ){

  int i;
  float value;
  unsigned out_chan = 3;
  unsigned int ndata = in.dataLength * 8 / in.bpc / in.channels;
  unsigned int k = 0;

  const float max3=1./3.;
  const float max8=1./8.;

  float *fptr = (float*)in.data;
  float *outptr = new float[ndata*out_chan];
  float *outv = outptr;

  switch(cmap){
    case HOT:
#pragma ivdep
      for( int n=0; n<ndata; n++, outv+=3 ){
        value = fptr[n];
        if(value>1.)
          { outv[0]=outv[1]=outv[2]=1.; }
        else if(value<=0.)
          { outv[0]=outv[1]=outv[2]=0.; }
        else if(value<max3)
          { outv[0]=3.*value; outv[1]=outv[2]=0.; }
        else if(value<2*max3)
          { outv[0]=1.; outv[1]=3.*value-1.; outv[2]=0.; }
        else if(value<1.)
          { outv[0]=outv[1]=1.; outv[2]=3.*value-2.; }
        else { outv[0]=outv[1]=outv[2]=1.; }
      }
      break;
    case COLD:
#pragma ivdep
      for( int n=0; n<ndata; n++, outv+=3 ){
        value = fptr[n];
        if(value>1.)
          { outv[0]=outv[1]=outv[2]=1.; }
        else if(value<=0.)
          { outv[0]=outv[1]=outv[2]=0.; }
        else if(value<max3)
          { outv[0]=outv[1]=0.; outv[2]=3.*value; }
        else if(value<2.*max3)
          { outv[0]=0.; outv[1]=3.*value-1.; outv[2]=1.; }
        else if(value<1.)
          { outv[0]=3.*value-2.; outv[1]=outv[2]=1.; }
        else {outv[0]=outv[1]=outv[2]=1.;}
      }
      break;
    case JET:
#pragma ivdep
      for( int n=0; n<ndata; n++, outv+=3 ){
        value = fptr[n];
        if(value<0.)
          { outv[0]=outv[1]=outv[2]=0.; }
        else if(value<max8)
          { outv[0]=outv[1]=0.; outv[2]= 4.*value + 0.5; }
        else if(value<3.*max8)
          { outv[0]=0.; outv[1]= 4.*value - 0.5; outv[2]=1.; }
        else if(value<5.*max8)
          { outv[0]= 4*value - 1.5; outv[1]=1.; outv[2]= 2.5 - 4.*value; }
        else if(value<7.*max8)
          { outv[0]= 1.; outv[1]= 3.5 -4.*value; outv[2]= 0; }
        else if(value<1.)
          { outv[0]= 4.5-4.*value; outv[1]= outv[2]= 0.; }
        else { outv[0]=0.5; outv[1]=outv[2]=0.; }
      }
      break;
    default:
      break;
    };


  // Delete old data buffer
  delete[] (float*) in.data;
  in.data = outptr;
  in.channels = out_chan;
  in.dataLength = ndata * out_chan * in.bpc / 8;
}

// Inversion function
void filter_inv( RawTile& in ){
  float* infptr;
  unsigned int np = in.dataLength * 8 / in.bpc;

  infptr = (float*)in.data;

  // Loop through our pixels for floating values 
#pragma ivdep
  for(int n=np; n--;) {
    float v = *infptr;
    *(infptr++) = 1. - v;
  }
}

// Resize image using nearest neighbour interpolation
void filter_interpolate_nearestneighbour( RawTile& in, unsigned int resampled_width, unsigned int resampled_height ){

  unsigned char* data8;
  unsigned short* data16;
  unsigned int* data32;
  unsigned char* buf8;
  unsigned short* buf16;
  unsigned int* buf32;

  unsigned int channels = (unsigned int) in.channels;
  unsigned int width = in.width;
  unsigned int height = in.height;

  if(in.bpc == 8){
    data8 = (unsigned char*) in.data;
    buf8 = new unsigned char[resampled_width*resampled_height*channels];
  }
  if(in.bpc == 16){
    data16 = (unsigned short*) in.data;
    buf16 = new unsigned short[resampled_width*resampled_height*channels];
  }
  if(in.bpc == 32){
    data32 = (unsigned int*) in.data;
    buf32 = new unsigned int[resampled_width*resampled_height*channels];
  }

  // Calculate our scale
  unsigned int xscale = (width << 16) / resampled_width;
  unsigned int yscale = (height << 16) / resampled_height;

  for( unsigned int j=0; j<resampled_height; j++ ){
    unsigned int jj = (j*yscale) >> 16;
    for( unsigned int i=0; i<resampled_width; i++ ){

      // Indexes in the current pyramid resolution and resampled spaces
      // Make sure to limit our input index to the image surface
      unsigned int ii = (i*xscale) >> 16;
      unsigned int pyramid_index = channels * ( ii + jj*width );
      unsigned int resampled_index = (i + j*resampled_width)*channels;

      for( int k=0; k<in.channels; k++ ){
        if(in.bpc == 8){
          buf8[resampled_index+k] = data8[pyramid_index+k];
        }
        else if(in.bpc == 16){
          buf16[resampled_index+k] = data16[pyramid_index+k];
        }
        else if(in.bpc == 32){
          buf32[resampled_index+k] = data32[pyramid_index+k];
        }
      }
    }
  }

  // Correctly set our Rawtile info
  if( in.memoryManaged && (in.bpc == 8 || in.bpc == 16 || in.bpc == 32) ) delete[] in.data;
  if(in.bpc == 8){
    in.data = buf8;
    in.memoryManaged = true;
  }
  else if(in.bpc == 16){
    in.data = buf16;
    in.memoryManaged = true;
  }
  else if(in.bpc == 32){
    in.data = buf32;
    in.memoryManaged = true;
  }
  in.width = resampled_width;
  in.height = resampled_height;
  in.dataLength = resampled_width * resampled_height * channels * in.bpc/8;
}


// Resize image using bilinear interpolation
//  - Floating point implementation which benchmarks about 2.5x slower than nearest neighbour
void filter_interpolate_bilinear( RawTile& in, unsigned int resampled_width, unsigned int resampled_height ){
  unsigned char* data8;
  unsigned short* data16;
  unsigned int* data32;
  unsigned char* buf8;
  unsigned short* buf16;
  unsigned int* buf32;
  unsigned char color8;
  unsigned short color16;
  unsigned int color32;

  int channels = in.channels;
  int width = in.width;
  int height = in.height;

  if(in.bpc == 8){
    data8 = (unsigned char*) in.data;
    buf8 = new unsigned char[resampled_width*resampled_height*channels];
  }
  if(in.bpc == 16){
    data16 = (unsigned short*) in.data;
    buf16 = new unsigned short[resampled_width*resampled_height*channels];
  }
  if(in.bpc == 32){
    data32 = (unsigned int*) in.data;
    buf32 = new unsigned int[resampled_width*resampled_height*channels];
  }

  float x_ratio = (width) / (float) resampled_width;
  float y_ratio = (height) / (float) resampled_height;
  bool edgeY, edgeX;
  int a,b,c,d,index,x,y;
  int offset = 0;
  float x_diff, y_diff;
  for(int i = 0; i < resampled_height; i++){
          y = (int)(y_ratio * i);
          y_diff = (y_ratio * i) - y;
    //if we come to edge, remember it, so we don't call unexisting pixels
    if(y == height - 1) {
      edgeY = true;
      y_diff = 0;
    }
    else{
      edgeY = false;
    }

          for(int j = 0; j < resampled_width; j++){
                  x = (int)(x_ratio * j);
      x_diff = (x_ratio * j) - x;
                  index = x + y*width;
      //if we come to edge, remember it, so we don't call unexisting pixels
      if(x == width - 1){
        edgeX = true;
        x_diff = 0;
      }
      else{
        edgeX = false;
      }

                  for(int k = 0; k < channels; k++) {

        if(in.bpc == 8){
                            a = data8[(index)*channels + k];
          if (!edgeX) b = data8[(index+1)*channels + k];
          else b = 0;
          if(!edgeY) c = data8[(index+width)*channels + k];
          else c = 0;
          if(!edgeX && !edgeY) d = data8[(index + width + 1)*channels + k];
          else d = 0;
          color8 = (unsigned char) (a*(1-x_diff)*(1-y_diff) + b*(x_diff)*(1-y_diff) + c*(1-x_diff)*(y_diff) + d*(x_diff)*(y_diff));
          buf8[offset++] = color8;
        }
        else if(in.bpc == 16){
                            a = data16[(index)*channels + k];
                            if (!edgeX) b = data16[(index+1)*channels + k];
          else b = 0;
          if(!edgeY) c = data16[(index+width)*channels + k];
          else c = 0;
          if(!edgeX && !edgeY) d = data16[(index + width + 1)*channels + k];
          else d = 0;
                            color16 = (unsigned char) (a*(1-x_diff)*(1-y_diff) + b*(x_diff)*(1-y_diff) + c*(1-x_diff)*(y_diff) + d*(x_diff)*(y_diff));
                            buf16[offset++] = color16;
        }
        else if(in.bpc == 32){
                            a = data32[(index)*channels + k];
                            if (!edgeX) b = data32[(index+1)*channels + k];
          else b = 0;
          if(!edgeY) c = data32[(index+width)*channels + k];
          else c = 0;
          if(!edgeX && !edgeY) d = data32[(index + width + 1)*channels + k];
          else d = 0;
                            color32 = (unsigned char) (a*(1-x_diff)*(1-y_diff) + b*(x_diff)*(1-y_diff) + c*(1-x_diff)*(y_diff) + d*(x_diff)*(y_diff));
                            buf32[offset++] = color32;
        }
                  }
          }
  }
  if( in.memoryManaged && (in.bpc == 8 || in.bpc == 16 || in.bpc == 32) ) delete[] in.data;
  if(in.bpc == 8){
    in.data = buf8;
    in.memoryManaged = true;
  }
  else if(in.bpc == 16){
    in.data = buf16;
    in.memoryManaged = true;
  }
  else if(in.bpc == 32){
    in.data = buf32;
    in.memoryManaged = true;
  }
  in.width = resampled_width;
  in.height = resampled_height;
  in.dataLength = resampled_width * resampled_height * channels * in.bpc/8;
}

// Function to apply a contrast adjustment and clip to 8 bit
void filter_contrast( RawTile& in, float c ){

  unsigned int np = in.dataLength * 8 / in.bpc;

  unsigned char* buffer = new unsigned char[np];

  float* infptr = (float*)in.data;

#pragma ivdep
  for( unsigned int n=0; n<np; n++ ){
    float v = infptr[n] * 255.0 * c;
    buffer[n] = (unsigned char) (v<255.0) ? (v<0.0? 0.0 : v) : 255.0;
  }

  // Replace original buffer with new
  delete[] (float*) in.data;
  in.data = buffer;
  in.bpc = 8;
  in.dataLength = np * in.bpc/8;
}


// Gamma correction
void filter_gamma( RawTile& in, float g ){

  float* infptr;
  unsigned int np = in.dataLength * 8 / in.bpc;

<<<<<<< HEAD
    // Replace original buffer with new
    if( in.bpc == 32 && in.sampleType == FLOATPOINT ) delete[] (float*) in.data;
    else if( in.bpc == 32 && in.sampleType == FIXEDPOINT ) delete[] (unsigned int*) in.data;
    else if( in.bpc == 16 ) delete[] (unsigned short*) in.data;
=======
  if( g == 1.0 ) return;
>>>>>>> 3fb67b04d2cfff0cd8b909e3ac6cd0ac739c8226

  infptr = (float*)in.data;

  // Loop through our pixels for floating values 
#pragma ivdep
  for(int n=np; n--;){
    float v = *infptr;
    *(infptr++) = powf(v<0.0? 0.0 : v, g );
  }
}


// Rotation function
void filter_rotate( RawTile& in, float angle=0.0 ){

  // Currently implemented only for rectangular rotations
  if( (int)angle % 90 == 0 && (int)angle % 360 != 0 ){

    // Intialize our counter and data buffer
    unsigned int n = 0;
    void* buffer = NULL;

    // Allocate memory for our temporary buffer
    if(in.bpc == 8) buffer = new unsigned char[in.width*in.height*in.channels];
    else if(in.bpc == 16) buffer = new unsigned short[in.width*in.height*in.channels];
    else if(in.bpc == 32 && in.sampleType == FIXEDPOINT ) buffer = new unsigned int[in.width*in.height*in.channels];
    else if(in.bpc == 32 && in.sampleType == FLOATINGPOINT ) buffer = new float[in.width*in.height*in.channels];

    // Rotate 90
    if( (int) angle % 360 == 90 ){
      for( unsigned int i=0; i < in.width; i++ ){
	for( unsigned int j=in.height; j>0; j-- ){
	  unsigned int index = (in.width*j + i)*in.channels;
	  for( int k=0; k < in.channels; k++ ){
	    if(in.bpc == 8) ((unsigned char*)buffer)[n++] = ((unsigned char*)in.data)[index+k];
	    else if(in.bpc == 16) ((unsigned short*)buffer)[n++] = ((unsigned short*)in.data)[index+k];
	    else if(in.bpc == 32 && in.sampleType == FIXEDPOINT) ((unsigned int*)buffer)[n++] = ((unsigned int*)in.data)[index+k];
	    else if(in.bpc == 32 && in.sampleType == FLOATINGPOINT ) ((float*)buffer)[n++] = ((float*)in.data)[index+k];
	  }
	}
      }
    }

    // Rotate 270
    else if( (int) angle % 360 == 270 ){
      for( int i=in.width; i>0; i-- ){
	for( int j=0; j < in.height; j++ ){
	  unsigned int index = (in.width*j + i)*in.channels;
	  for( int k=0; k < in.channels; k++ ){
	    if(in.bpc == 8) ((unsigned char*)buffer)[n++] = ((unsigned char*)in.data)[index+k];
	    else if(in.bpc == 16) ((unsigned short*)buffer)[n++] = ((unsigned short*)in.data)[index+k];
	    else if(in.bpc == 32 && in.sampleType == FIXEDPOINT ) ((unsigned int*)buffer)[n++] = ((unsigned int*)in.data)[index+k];
	    else if(in.bpc == 32 && in.sampleType == FLOATINGPOINT ) ((float*)buffer)[n++] = ((float*)in.data)[index+k];
	  }
	}
      }
    }

    // Rotate 180
    else if( (int) angle % 360 == 180 ){
      for( unsigned int i=(in.width*in.height)-1; i > 0; i-- ){
	unsigned index = i * in.channels;
	for( int k=0; k < in.channels; k++ ){
	  if(in.bpc == 8) ((unsigned char*)buffer)[n++]  = ((unsigned char*)in.data)[index+k];
	  else if(in.bpc == 16) ((unsigned short*)buffer)[n++] = ((unsigned short*)in.data)[index+k];
	  else if(in.bpc == 32 && in.sampleType == FIXEDPOINT) ((unsigned int*)buffer)[n++] = ((unsigned int*)in.data)[index+k];
	  else if(in.bpc == 32 && in.sampleType == FLOATINGPOINT ) ((float*)buffer)[n++] = ((float*)in.data)[index+k];
	}
      }
    }

    // Delete old data buffer
    if( in.bpc == 8 ) delete[] (unsigned char*) in.data;
    else if( in.bpc == 16 ) delete[] (unsigned short*) in.data;
    else if( in.bpc == 32 && in.sampleType == FIXEDPOINT ) delete[] (unsigned int*) in.data;
    else if( in.bpc == 32 && in.sampleType == FLOATINGPOINT ) delete[] (float*) in.data;

    // Assign new data to Rawtile
    in.data = buffer;

    // For 90 and 270 rotation swap width and height
    if( (int)angle % 180 == 90 ){
      unsigned int tmp = in.height;
      in.height = in.width;
      in.width = tmp;
    }
  }
}

<<<<<<< HEAD
    if( in.bpc == 32 && in.sampleType == FLOATPOINT ) v = (float)((float*)in.data)[n];
    else if( in.bpc == 32 && in.sampleType == FIXEDPOINT ) v = (float)((unsigned int*)in.data)[n];
    else if( in.bpc == 16 ) v = (float)((unsigned short*)in.data)[n];
    else v = (float)((unsigned char*)in.data)[n];
=======
>>>>>>> 3fb67b04d2cfff0cd8b909e3ac6cd0ac739c8226

// Convert colour to grayscale using the conversion formula:
//   Luminance = 0.2126*R + 0.7152*G + 0.0722*B
// Note that we don't linearize before converting
void filter_greyscale( RawTile& rawtile ){

  if( rawtile.bpc != 8 || rawtile.channels != 3 ) return;

  unsigned int np = rawtile.width * rawtile.height;
  unsigned char* buffer = new unsigned char[rawtile.width * rawtile.height];

<<<<<<< HEAD
    if( in.bpc == 32 && in.sampleType == FLOATPOINT ) ((float*)in.data)[n] = (float) v;
    else if( in.bpc == 32 && in.sampleType == FIXEDPOINT ) ((unsigned int*)in.data)[n] = (float) v;
    else if( in.bpc == 16 ) ((unsigned short*)in.data)[n] = (unsigned short) v;
    else v = ((unsigned char*)in.data)[n] = (unsigned char) v;
=======
  // Calculate using fixed-point arithmetic
  //  - benchmarks to around 25% faster than floating point
  unsigned int n = 0;
  for( unsigned int i=0; i<np; i++ ){
    unsigned char R = ((unsigned char*)rawtile.data)[n++];
    unsigned char G = ((unsigned char*)rawtile.data)[n++];
    unsigned char B = ((unsigned char*)rawtile.data)[n++];
    buffer[i] = (unsigned char)( ( 1254097*R + 2462056*G + 478151*B ) >> 22 );
>>>>>>> 3fb67b04d2cfff0cd8b909e3ac6cd0ac739c8226
  }

  // Delete our old data buffer and instead point to our grayscale data
  delete[] (unsigned char*) rawtile.data;
  rawtile.data = (void*) buffer;

  // Update our number of channels and data length
  rawtile.channels = 1;
  rawtile.dataLength = np;
}

// Rotation function
void filter_rotate( RawTile& in, float angle ){

  // Currently implemented only for rectangular rotations
  if( (int)angle % 90 == 0 && (int)angle % 360 != 0 ){

    // Intialize our counter and data buffer
    unsigned int n = 0;
    void* buffer = NULL;

    // Allocate memory for our temporary buffer
    if(in.bpc == 8) buffer = new unsigned char[in.width*in.height*in.channels];
    else if(in.bpc == 16) buffer = new unsigned short[in.width*in.height*in.channels];
    else if(in.bpc == 32 && in.sampleType == FIXEDPOINT ) buffer = new unsigned int[in.width*in.height*in.channels];
    else if(in.bpc == 32 && in.sampleType == FLOATPOINT ) buffer = new float[in.width*in.height*in.channels];

    // Rotate 90
	  if ((int) angle % 360 == 90){
      for (int i = in.width; i > 0; i--){
        for (int j = in.height; j > 0; j--){
          unsigned int index = (in.width * j - i )*in.channels;
          for(int k = 0; k < in.channels; k++){
            if(in.bpc == 8) ((unsigned char*)buffer)[n++] = ((unsigned char*)in.data)[index+k];
	          else if(in.bpc == 16) ((unsigned short*)buffer)[n++] = ((unsigned short*)in.data)[index+k];
	          else if(in.bpc == 32 && in.sampleType == FIXEDPOINT ) ((unsigned int*)buffer)[n++] = ((unsigned int*)in.data)[index+k];
	          else if(in.bpc == 32 && in.sampleType == FLOATPOINT ) ((float*)buffer)[n++] = ((float*)in.data)[index+k];
			    }
        }
      }
    }    

    // Rotate 270
    else if( (int) angle % 360 == 270 ){
      for( int i=in.width - 1; i>=0; i-- ){
	for( int j=0; j < in.height; j++ ){
	  unsigned int index = (in.width*j + i)*in.channels;
	  for( int k=0; k < in.channels; k++ ){
	    if(in.bpc == 8) ((unsigned char*)buffer)[n++] = ((unsigned char*)in.data)[index+k];
	    else if(in.bpc == 16) ((unsigned short*)buffer)[n++] = ((unsigned short*)in.data)[index+k];
	    else if(in.bpc == 32 && in.sampleType == FIXEDPOINT ) ((unsigned int*)buffer)[n++] = ((unsigned int*)in.data)[index+k];
	    else if(in.bpc == 32 && in.sampleType == FLOATPOINT ) ((float*)buffer)[n++] = ((float*)in.data)[index+k];
	  }
	}
      }
    }

    // Rotate 180
    else if( (int) angle % 360 == 180 ){
      for( int i=(in.width*in.height)-1; i >= 0; i-- ){
        unsigned int index = i * in.channels;
        for( int k=0; k < in.channels; k++ ){
          if(in.bpc == 8) ((unsigned char*)buffer)[n++]  = ((unsigned char*)in.data)[index+k];
          else if(in.bpc == 16) ((unsigned short*)buffer)[n++] = ((unsigned short*)in.data)[index+k];
          else if(in.bpc == 32 && in.sampleType == FIXEDPOINT) ((unsigned int*)buffer)[n++] = ((unsigned int*)in.data)[index+k];
          else if(in.bpc == 32 && in.sampleType == FLOATPOINT ) ((float*)buffer)[n++] = ((float*)in.data)[index+k];
        }
      }
    }

    // Delete old data buffer
    if( in.bpc == 8 ) delete[] (unsigned char*) in.data;
    else if( in.bpc == 16 ) delete[] (unsigned short*) in.data;
    else if( in.bpc == 32 && in.sampleType == FIXEDPOINT ) delete[] (unsigned int*) in.data;
    else if( in.bpc == 32 && in.sampleType == FLOATPOINT ) delete[] (float*) in.data;

    // Assign new data to Rawtile
    in.data = buffer;

    // For 90 and 270 rotation swap width and height
    if( (int)angle % 180 == 90 ){
      unsigned int tmp = in.height;
      in.height = in.width;
      in.width = tmp;
    }
  }
}

void filter_crop( RawTile& in, int left, int top, int right, int bottom ){

  unsigned int n = 0;
  //Cropping
  for( int i=top; i < in.height - bottom; i++ ){
    unsigned int index1 = i * in.width;
    for ( int j=left; j < in.width - right; j++ ){
      unsigned int index = (index1 + j)*in.channels;
      for( int k=0; k < in.channels; k++ ){
        if(in.bpc == 8) ((unsigned char*)in.data)[n++]  = ((unsigned char*)in.data)[index+k];
        else if(in.bpc == 16) ((unsigned short*)in.data)[n++] = ((unsigned short*)in.data)[index+k];
        else if(in.bpc == 32 && in.sampleType == FIXEDPOINT) ((unsigned int*)in.data)[n++] = ((unsigned int*)in.data)[index+k];
        else if(in.bpc == 32 && in.sampleType == FLOATPOINT ) ((float*)in.data)[n++] = ((float*)in.data)[index+k];
      }
    }
  }
  //adjust dimensions
  in.height = in.height - top - bottom;
  in.width = in.width - left - right;
}
