#include <stdio.h>
#include <stdlib.h>

char a[1024], b[2014];
int main(int argc, char **argv){
   int N = 4*1024*1024;
   int n;
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
   scanf("%s",a);
   while (1) {
     //printf("%s",a);
     float x,w = 0;
     int i,j;
     char c;
     int docno;
     sscanf(a,"%d",&docno);
     while ((1 == (j = scanf(" %s",a))) && (2 == sscanf(a,"%d:%g",&i,&x))) {
       //printf(" x=%g i= %d pi = %g %g %g\n",x,i,p[i-1],p[i],p[i+1]);
       //w += x * p[i];
       //printf("blah!\n");
       //printf("a %s\n",a);
       fwrite(&i,sizeof(int),1,stdout);
       fwrite(&x,sizeof(float),1,stdout);
     }
     //i=-1;
     i = -docno;
     fwrite(&i,sizeof(int),1,stdout);
     if (j != 1) break;
   }
}
