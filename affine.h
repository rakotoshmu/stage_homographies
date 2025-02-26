#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <omp.h>

#include "umax_vmax.h"

/**
  * how are indexed coefficients of matrices :
  * for the affinities :
  * 0 1 2
  * 3 4 5
  *
  * the image is supposed to have three level of colors
  */

//the filter zero for x not in [-TAPS,TAPS]
#define TAPS 4

#define FILTER_TYPE 0 //the index to change the interpolation filter
//list of possible index for interpolation filters
#define RAISED_COSINE 0
#define GAUSSIAN 1

//parameters of filters
#define VARIANCE 0.36 //variance of the gaussian
#define PERIOD 1. //period of the raised cosine
#define BETA 0.36 //roll-off factor of the raised cosine

//precision of the computation
#define PREC 10 //the precomputation of the values of the filter will be at precision 2^(-PREC)
#define PREC2 20 //an equality will be at precision 2^(-PREC2)
#define PI 3.14159265358979323

//a relaxed equality on doubles
bool eq(double a,double b){if(a<b+pow(2,-PREC2)&& a>b-pow(2,-PREC2)){return true;}{return false;}}



//transpose A and img if necessary (to avoid bottleneck problem)
void transpo_opt(float *img,double *a,int wh[2]){
/**
  * @param
  *		img an image
  *		a an affine map (matrix 2*3)
  */
  	//normalize the linear part of a
	double c1,c2;
	c1 = sqrt(pow(a[0],2)+pow(a[1],2));
	c2 = sqrt(pow(a[3],2)+pow(a[4],2));
	double a0,a1,a3,a4;
	a1=fabs(a[1])/c1; a0=fabs(a[0])/c1; a3=fabs(a[3])/c2; a4=fabs(a[4])/c2;
	if(a0+a4<a1+a3){ //transposition is necessary
		int w = wh[0];
		int h = wh[1];

		//transpose A
		double aa[6];
		aa[0]=a[3]; aa[1]=a[4]; aa[2]=a[5]; aa[3]=a[0]; aa[4]=a[1]; aa[5]=a[2];
		a[0]=aa[0]; a[1]=aa[1]; a[2]=aa[2]; a[3]=aa[3]; a[4]=aa[4]; a[5]=aa[5];

		//transpose img
		float *img_t = malloc(3*w*h*sizeof(float));
		for(int i=0;i<w;i++){
			for(int j=0;j<h;j++){
				for(int l=0;l<3;l++){
					img_t[(j+h*i)*3+l]=img[(i+j*w)*3+l];
				}
			}
		}
		for(int i=0;i<w*h*3;i++){img[i]=img_t[i];}

		//transpose dimensions (w and h)
		wh[0]=h; wh[1]=w;
	}
}

//the filter function
float filter_fun(float x){
    switch(FILTER_TYPE){
    case GAUSSIAN:
        return exp(-pow(x,2)/(2*VARIANCE));
        break;
    case RAISED_COSINE:
    default:
        if(eq(x,0)){return 1.;}
        else if(eq(fabs(x),PERIOD/2./BETA)){return BETA/2.*sin(PI/2./BETA);}
        else{return sin(PI*x/PERIOD)/(PI*x/PERIOD)*cos(PI*BETA*x/PERIOD)/(1.-pow(2.*BETA*x/PERIOD,2));}
        break;
    }
}



//horizontal convolution
float filter_h(float *img,int w,int h,double xc_i,double yc_i,double xc_f,double yc_f,float *H,int N,int prec,double a0,double a1,double x,int j,int l){
/**
  * @param
  *     img : the image to interpolate
  *     w,h : dimensions of img
  *     xc_i,yc_i : coordinates of the center of img before convolution
  *     xc_f,yc_f : coordinates of the center of img after convolution
  *     H : array with precomputed values of the filter
  *     N : the support of the filter is [-N,N]
  *     prec : precision on the precomputation H
  *     a0,a1 : coefficient of the shear
  *     x,j,l : coordinates of the point waiting to be valued
  */
    //cast
	double y = j;
	double wf = (double) w, hf = (double) h;
	//where to center the convolution
	double M = a0*(x+xc_f-wf/2.) + a1*(y+yc_f-hf/2.) - xc_i+wf/2.;
	int k = floor(M);
	int p = floor(prec*(M-k));
    //convolution
	float tot = 0;
	for(int u = k-N<0?0:k-N;u<=k+N && u<w;u++){
	       tot += H[(k-u)*prec + p + N*prec]*img[(u+j*w)*3+l];
	}

    return tot;
}

//vertical convolution
float filter_v(float *img,int w,int h,double xc_i,double yc_i,double xc_f,double yc_f,float *H,int N,int prec,double a0,double a1,int i,double y,int l){
/**
  * @param
  *     img : the image to interpolate
  *     w,h : dimensions of img
  *     xc_i,yc_i : coordinates of the center of img before convolution
  *     xc_f,yc_f : coordinates of the center of img after convolution
  *     H : array with precomputed values of the filter
  *     N : the support of the filter is [-N,N]
  *     prec : precision on the precomputation H
  *     a0,a1 : coefficient of the shear
  *     x,j,l : coordinates of the point waiting to be valued
  */
    //cast
	double x = i;
	double wf = (double) w, hf = (double) h;
	//where to center the convolution
	double M = a1*(x+xc_f-wf/2.) + a0*(y+yc_f-hf/2.) - yc_i+hf/2.;
	int k = floor(M);
	int p = floor(prec*(M-k));
	//convolution
	float tot = 0;
	for(int u = k-N<0?0:k-N;u<=k+N && u<h;u++){
        tot += H[(k-u)*prec + p + N*prec]*img[(i+u*w)*3+l];
	}

    return tot;
}



//apply an horizontal shear
int apply_rh(float *img1,float *img2,int w,int h,double xc_i,double yc_i,double xc_f,double yc_f,double s,double a0,double a1){
/**
  * @param
  *     img1, img2 : initial and final images
  *     w,h : dimensions of those images
  *     xc_i,yc_i : coordinates of the center of img1
  *     xc_f,yc_f : coordinates of the center of img2
  *     s : parameter to increase the width of the filter (i.e. to decrease its band-width)
  *     a0,a1 : coefficients of the shear
  */
	if(eq(a0,1.) && eq(a1,0.) && eq(xc_i,xc_f)){ //the shear is the identity
		for(int i=0;i<3*w*h;i++){img2[i]=img1[i];}
		return 0;
	}



	//declare H (will contain precomputed values of the filter function)
	int N = ceil(abs(s*TAPS));
	int prec = pow(2,PREC);
	float precf = (float) prec;
	float *H = malloc((2*N+1)*prec*sizeof(float));
	if(H==NULL){printf("apply_rh : H has not been created");return 1;}

	//precompute values of the filter function
	for(int p=0;p<prec;p++){
		float Htot = 0;
		float pf = (float) p;
		for(int k=-N;k<=N;k++){
                float kf = (float) k;
			Htot += H[k*prec + p + N*prec] = filter_fun((kf+pf/precf)/s); //s>=1 increase the width of the filter
		}
		for(int k=-N;k<=N;k++){H[k*prec + p + N*prec] = H[k*prec + p + N*prec]/Htot;} //normalize the filter for each p
	}

	//convolve
	#pragma omp parallel for schedule(static,1)
	for(int j=0;j<h;j++){
		for(int i=0;i<w;i++){
		double x = i;
			for(int l=0;l<3;l++){
				img2[(i+j*w)*3 + l] = filter_h(img1,w,h,xc_i,yc_i,xc_f,yc_f,H,N,prec,a0,a1,x,j,l);
			}
		}
	}

	return 0;
}

//apply an vertical shear
int apply_rv(float *img1,float *img2,int w,int h,double xc_i,double yc_i,double xc_f,double yc_f,double s,double a0,double a1){
/**
  * @param
  *     img1, img2 : initial and final images
  *     w,h : dimensions of those images
  *     xc_i,yc_i : coordinates of the center of img1
  *     xc_f,yc_f : coordinates of the center of img2
  *     s : parameter to increase the width of the filter (i.e. to decrease its band-width)
  *     a0,a1 : coefficients of the shear
  */
	if(eq(a0,1.) && eq(a1,0.) && eq(yc_i,yc_f)){ //the shear is the identity
		for(int i=0;i<3*w*h;i++){img2[i]=img1[i];
	}
	return 0;
	}



    //declare H (will contain precomputed values of the filter function)
	int N = ceil(abs(s*TAPS));
	int prec = pow(2,PREC);
	float sf = (float) s;
	float precf = (float) prec;
	float *H = malloc((2*N+1)*prec*sizeof(float));
	if(H==NULL){printf("apply_rv : H has not been created");return 1;}

    //precompute values of the filter function
	for(int p=0;p<prec;p++){
		float Htot = 0;
		float pf = (float) p;
		for(int k=-N;k<=N;k++){
            float kf = (float) k;
			Htot += H[k*prec + p + N*prec] = filter_fun((kf+pf/precf)/s); //s>=1 increase the width of the filter
		}
		for(int k=-N;k<=N;k++){H[k*prec + p + N*prec] = H[k*prec + p + N*prec]/Htot;} //normalize the filter for each p
	}

    //convolve
	#pragma omp parallel for schedule(static,1)
	for(int j=0;j<h;j++){
		double y = j;
		for(int i=0;i<w;i++){
			for(int l=0;l<3;l++){
				img2[(i+j*w)*3+l] = filter_v(img1,w,h,xc_i,yc_i,xc_f,yc_f,H,N,prec,a0,a1,i,y,l);
			}
		}
	}

	return 0;
}



//apply an affinity
int apply_affinity(float *img,float *img_f,int w,int h,int w_f,int h_f,double *a){
/**
  * @param
  *     img, img_f : initial and final images
  *     w,h, w_f,h_f : initial and final dimensions of image
  *     a : the inverse affinity to apply (img_f(x)=img(a(x)))
  */
	/*
	 * the following call to the function transpo_opt will possibly change img and a
	 * -->  if the user wants to use multi-pass resampling method for other purposes than
	 *      the decomposition, he may want to copy img and a and work on those copies
	 *      else, the previous content of img and a might be deleted
	 */
	//transpose img and a if necessary
	int wh[2] = {w,h};
	transpo_opt(img,a,wh);
	w=wh[0];
	h=wh[1];

    //compute maximal preserved frequencies u_max and v_max
	double umax,vmax;
	double A[2][2] = {a[0],a[1],a[3],a[4]};
    int test = umax_vmax(&umax,&vmax,A);  //in "umax_vmax.h"
    if(test==1){
        printf("@apply_affinity : error dans umax_vmax\n");
        exit(1);
    }

	//some values related to sizes of images
	//ww and hh are the dimension of the image that will be processed (large enough to not loose information)
	int ww,hh;
	if(w<w_f){ww = w_f;} else {ww = w;}
	if(h<h_f){hh = h_f;} else {hh = h;}
	//dw et dh : difference of size between img and img_f
	int dw = (w_f-w)/2, dh = (h_f-h)/2; //x_f and x have the same parity
	//dwp et dhp : their positive part (dxp=max(0,dx))
	int dwp = (dw<0) ? 0 : dw;
	int dhp = (dh<0) ? 0 : dh;

	//declare intermediate images, 9 times taller
	float *img1 = malloc(3*9*ww*hh*sizeof(float));
	float *img2 = malloc(3*9*ww*hh*sizeof(float));
	if(img1==NULL || img2==NULL){
        printf("apply_affinity : img1 et img2 have not been created");
        exit(1);
    }



    //copy the image in a much taller image (9 times taller)
    //to avoid ringing, there are symmetrical boundary conditions
    int i_sym,j_sym;
	for(int j=0;j<3*hh;j++){
		for(int i=0;i<3*ww;i++){
			for(int l=0;l<3;l++){
                i_sym = i-ww-dwp;
                while(i_sym<0 || i_sym>w-1){i_sym = (i_sym<0) ? -1-i_sym : 2*w-1-i_sym;} //symmetry
                j_sym = j-hh-dhp;
                while(j_sym<0 || j_sym>h-1){j_sym = (j_sym<0) ? -1-j_sym : 2*h-1-j_sym;} //symmetry
				img1[(i+3*ww*j)*3+l]=img[(i_sym+j_sym*w)*3+l];
			}
		}
	}



	//compute values to decompose a in shears
	double b0 = a[0] - a[1]*a[3]/a[4];
	double t2 = a[2] - a[1]*a[5]/a[4];
	double rv = fmin(3, fmax(1, fabs(a[1])*umax + fmin(1, fabs(a[4])*vmax)));
    double rh = fmin(3, fmax(1, fabs(a[3]/a[4])*rv*vmax + fmin(1, fabs(b0)*umax)));

	//cast
	double wf = (double) w, hf = (double) h;
	double wwf = (double) ww, hhf = (double) hh;

	//coordinates of the centers of initial and final images
	double xc_i, yc_i, xc_f, yc_f;

	//first shear
	xc_i = wf/2.;
	yc_i = hf/2.;
	xc_f = xc_i;
	yc_f = rv/a[4] * yc_i;
	apply_rv(img1,img2,3*ww,3*hh,xc_i,yc_i,xc_f,yc_f,1/vmax,a[4]/rv,0.);

	//second shear
	xc_i = xc_f - t2; //translation of -t2 between the first and the second shear
	yc_i = yc_f;
	xc_f = rh/b0 * xc_i - a[1]/rv*rh/b0 * yc_i;
	yc_f = yc_i;
    apply_rh(img2,img1,3*ww,3*hh,xc_i,yc_i,xc_f,yc_f,1/umax,b0/rh,a[1]/rv);

    //third shear
	xc_i = xc_f;
	yc_i = yc_f - a[5]*rv/a[4]; //translation of -a[5]*rv/a[4] between the second and the third shear
	xc_f = xc_i;
	yc_f = hhf/2.; //recenter on the final view
	apply_rv(img1,img2,3*ww,3*hh,xc_i,yc_i,xc_f,yc_f,rv,rv,a[3]*rv/a[4]/rh);

	//last shear
	xc_i = xc_f;
	yc_i = yc_f;
	xc_f = wwf/2.; //recenter on the final view
	yc_f = hhf/2.;
	apply_rh(img2,img1,3*ww,3*hh,xc_i,yc_i,xc_f,yc_f,rh,rh,0.);



	//output
	for(int i=0;i<w_f;i++){
		for(int j=0;j<h_f;j++){
			for(int l=0;l<3;l++){
				img_f[(i+j*w_f)*3+l] = img1[(i+ww+(j+hh)*ww*3)*3+l];
			}
		}
	}

    free(img1);
    free(img2);
	return 0;
}
