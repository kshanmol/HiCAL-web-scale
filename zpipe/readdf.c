#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *buf;
long b = 0;
long nn = 1024*1024;
char **idx;
long nidx,d;
double N=567528;  // trec4

int comp(char **a, char **b){
   return strcmp(*a,*b);
}

long cnt(char **a){
   if (!a) return 0;
   if (a == idx) return atol(buf);
   char *p;
   for (p = *a-2;*p;p--) {}
   return atoi(p+1);
}
int xmain();
int main(int argc, char **argv){
   long n;
   buf = malloc(1024*1024);
   idx = malloc(1024*sizeof(char*));
   nidx = 1024;
   FILE *f = fopen(argc==1?"df":argv[1],"r");
   while (1) {
      n = fread(buf+b,1,nn,f);
      b += n;
      if (n < nn) break;
      buf = realloc(buf,1024*1024+2*b);
      nn = b;
   }
   FILE *nf = fopen(argc==1?"N":argv[2],"r");
   fscanf(nf,"%lg",&N);
   //fwrite(buf,1,b,stdout);
   for (char *p=strtok(buf," \n\t");p;p=strtok(NULL," \n\t")) {
      p = strtok(NULL," \n\t");
      if (d >= nidx){
         nidx *= 2;
         idx = realloc(idx,nidx*sizeof(char*));
      }
      idx[d] = p; 
      d++;
   }
   //for (int i=0;i<d;i++) printf("%d %s\n",i,idx[i]);
   //char *key = "zzzz";
   //char ** x = bsearch(&key,idx,d,sizeof(char*),(__compar_fn_t)comp);
   //printf("res %s\n",*x);
   //printf("cnt %ld\n",cnt(x));
   xmain();
}


// a 1 653668 ./rcv1v2/002/2286.vocab: 11

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

long tok, df, tf;
//double N=804414; // rcv1
char doc[1000], prev[1000];
char word[1000];
char *w = word;
int n,T[100000];
double W[100000];

int doit(){
   int i;
   //*strstr(prev,":") = 0;
   printf("%s",prev);
   double sum = 0;
   for (i=0;i<n;i++) {
      sum += W[i]*W[i];
   }
   sum = sqrt(sum);
   for (i=0;i<n;i++) {
      printf(" %d:%0.8lf",T[i],W[i]/sum);
   }
   printf("\n");
}

int xmain(){
   //while (4 == scanf("%*s %d %d %s %d",&tok,&df,doc,&tf)) {
   while (3 == scanf("%s %ld %s",doc,&tf,word)) {
      if (strcmp(doc,prev)) {
         if (*prev) doit();
         strcpy(prev,doc);
         n = 0;
      }
      char ** x = bsearch(&w,idx,d,sizeof(char*),(__compar_fn_t)comp);
      if (!x) fprintf(stderr,"oops word %s not found\n",word);
      if (x) {
         tok = x-idx+1;
         //printf("word %s %ld\n",word,tok);
      } else continue;
      df = cnt(x);
      if (df < 2) continue;
      double tfidf = (1 + log(tf)) * log(N/df);
      //printf("%d %d %d %0.4lf %s\n",tok,df,tf,tfidf,doc);
      T[n] = tok;
      W[n++] = tfidf;
   }
   doit();
}
