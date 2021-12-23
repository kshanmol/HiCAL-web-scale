#include <stdio.h>
#include <stdlib.h>

#define NN 1024*1024
char a[1024], b[2014];
int buf[NN];
int main(int argc, char **argv){
   int N = 4*1024*1024;
   int j,n;
   float *p = malloc(N*sizeof(float));
   FILE *model = fopen(argv[1],"r");
   if (argc == 3) freopen(argv[2],"r",stdin);
   for (n=0;1 == fscanf(model,"%g",p+n);n++) {
      if (n+1 >= N) {
         N*=2;
         p = realloc(p,N*sizeof(float));
      }
   }
   //printf("n %d\n",n);
   //for (int i=0;i<n;i++) printf("%d: %g\n",i,p[i]);
   float w = 0;
   while (n = fread(buf,sizeof(int),NN-1,stdin)) {
      for (j=0;j<n;j++) {
         if (buf[j] < 0) {
            printf("%d %0.7f\n",-buf[j],w);
            w = 0;
            continue;
         }
         if (j == n-1) fread(buf+n,sizeof(int),1,stdin);
         //printf("i %d x %0.8f\n",buf[j],((float *)buf)[j+1]);
         w += p[buf[j]] * ((float *)buf)[j+1];
         j++;
      }
   }
}
