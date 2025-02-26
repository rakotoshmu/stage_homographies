//gcc-5 -fopenmp -O3 viho.c -I/usr/local/include/libiomp -I/usr/X11/include -I/Users/Hhhh/ENS/Stage_L3_math/homographies/code/jpeg-6b -L/usr/X11/lib -lfftw3 -lX11 -L/usr/local/Cellar/libtiff/4.0.3 -ltiff -ljpeg -lpng


#include "iio.c"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "decomp.h"
#include <time.h>

#define WOUT 512
#define HOUT 512


int main(int argc,char *argv[]){

	if (argc != 11) {
		printf("usage : [image.png] a b p c d q r s t\n"); 
		return 1;
	}
	
	char *filename_in = argv[1];
	
	double H[3][3];
	H[0][0]=strtod(argv[2],NULL);
	H[0][1]=strtod(argv[3],NULL);
	H[0][2]=strtod(argv[4],NULL);
	H[1][0]=strtod(argv[5],NULL);
	H[1][1]=strtod(argv[6],NULL);
	H[1][2]=strtod(argv[7],NULL);
	H[2][0]=strtod(argv[8],NULL);
	H[2][1]=strtod(argv[9],NULL);
	H[2][2]=strtod(argv[10],NULL);

	float *img;
	int w,h,pd;

	img = iio_read_image_float_vec(filename_in, &w, &h, &pd);
	
	float *img_f = malloc(3*WOUT*HOUT*sizeof(float));

	clock_t debutcpu,fincpu;
	double debutreal,finreal;
	debutcpu = clock();
	debutreal = omp_get_wtime();
	if(pd==3){
        apply_homo_final(img,img_f,w,h,WOUT,HOUT,H);
	}else{//suppose pd=1
        float *img3 = malloc(3*w*h*sizeof(float));
        for(int i=0;i<w*h;i++){
            for(int l = 0;l<3;l++){
                img3[3*i+l]=img[i];
            }
        }
        apply_homo_final(img3,img_f,w,h,WOUT,HOUT,H);
	}
	
	fincpu = clock();
	finreal = omp_get_wtime();
	printf("cputime :%fs\ntime : %fs\n",(double)(fincpu-debutcpu)/CLOCKS_PER_SEC,(double)(finreal-debutreal));
	
	iio_save_image_float_vec("img_f.png",img_f,WOUT,HOUT,3);


	return 0;
}
