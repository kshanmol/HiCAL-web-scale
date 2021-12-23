#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdatomic.h>
#include <semaphore.h>

#define NN 1024*1024
#define SZ 1000
#define NP 200

int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_init(pthread_mutex_t *restrict mutex,
const pthread_mutexattr_t *restrict attr);
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

#define QUOTA 1000
double volatile best[SZ];
typedef struct node {
   int off;
   char *warcfile;
   float score;
   int m;
   volatile struct node *next;
} node;
node * besti[SZ];
int bestn[SZ];

volatile node* head;
volatile int worker_done;
sem_t sem, insem;
   
char a[1024], b[2014];
int buf[NN];
long long int nm;
char **av; int ac;
pid_t forkres; int waitres;
   //long long int N = 4*1024*1024;
   long long int N = 140000000l;
   int j,n;
   FILE *model;
   float *p;
   int np;
void doit(char *f);
void * ptmain(void *);
void * ptworker(void *);
   pthread_t ptwork;
int qq[SZ],nq;
volatile int q[SZ];
FILE *nf[SZ];
int main(int argc, char **argv){
   int i;
   ac = argc;
   av = argv;
   assert(argc>=2);
   for (i=1;i<argc && *argv[i] != '-';i++){
      fprintf(stderr,"model %s\n",argv[i]);
   }
   nm = i-1;
   fprintf(stderr,"models: %lld\n",nm);
   system("date");
   assert(nm <= SZ);
   //exit(0);
      
   p = calloc(N*nm,sizeof(float));
   for (np=0;np < nm;np++){
      best[np] = -999;
      nf[np] = fopen(argv[np+1],"r");
      if (!nf[np]) {perror(argv[np+1]); exit(1);}
      unsigned long long fea; float g;
      for (i=0;2==fscanf(nf[np],"%llu%f",&fea,&g);i++){
         assert (fea < N);
         if (g) p[nm*(fea-1)+np] = g;
      }
      fclose(nf[np]);
      fprintf(stderr,"read model %s\n",argv[np+1]);
   }
   system("date");
   int junk;
   if (sem_init(&sem, 0, 0) == -1) perror("semaphore");
   if (sem_init(&insem, 0, 0) == -1) perror("semaphore");
   for (i=0;i<1000;i++) sem_post(&insem);
   if (pthread_create(&ptwork, NULL, ptworker, (void *)&junk)) perror("ptworker create");

   pthread_t pt[NP];
   int pta[NP];
   for (i=nm+1;i<argc;i++) if(*argv[i] == '-') q[nq++] = i+1;
   qq[nq-1] = i;
   for (i=0;i<nq-1;i++) qq[i] = q[i+1]-1;
   for (i=0;i<NP;i++){ 
      fprintf(stderr,"starting %d\n",i);
      pta[i]=i;
      if (pthread_create(&pt[i], NULL, ptmain, (void *)&pta[i]))perror("pthread_create");
   }

   int r;
   for (i=0;i<NP;i++){
      if(pthread_join(pt[i],(void **)&r))perror("pthread_join");
   }
   worker_done = 1;
   //pthread_mutex_unlock(&mutex);
   if (sem_post(&sem) == -1) perror("sem_post final");
   if(pthread_join(ptwork,(void **)&r))perror("pthread_join worker");
   //while(1);
   perror("pthread_join_worker");
   fprintf(stderr,"cleaning up\n");
   fflush(stdout);
   system("date");
   exit(0);
}

void * ptworker(void *where){
   unsigned long long i;
   node *p, *nextp, *newhead=NULL;
   for (i=0;;i++){
      fprintf(stderr,"worker hello %llu\n",i);
      int B;
      while (!(B = atomic_compare_exchange_weak(&head,&newhead,NULL))) {
         fprintf(stderr,"worker qtry\n");
      }
      fprintf(stderr,"worker qdone\n");
      for (p=newhead;p;p=nextp){
         fprintf(stderr,"worker q %d %f\n",p->m,p->score);
         nextp=p->next;
         sem_post(&insem);
         if (p->score > best[p->m]) {
            fprintf(stderr,"newbest %d %f %d\n",p->m,p->score,bestn[p->m]);
            node **z = &besti[p->m];
            for (; *z && (*z)->score < p->score;z=&(*z)->next);
            p->next = *z;
            *z = p;
            if (bestn[p->m] < QUOTA) bestn[p->m]++;
            else {
               node *t = besti[p->m];
               besti[p->m] = besti[p->m]->next;
               free(t);
            }
            if (bestn[p->m] == QUOTA) best[p->m] = besti[p->m]->score;
         } else {
            free(p);
         }
      }
      if (worker_done && !head) {
         for (int m=0;m<nm;m++){
            int idx = QUOTA; 
            for (node*z = besti[m];z;z=z->next){
               printf("max %s %d %f %s %d\n",av[m+1],idx--,z->score,z->warcfile,z->off);
            }
         }
         fprintf(stderr,"worker bye\n");
         break;
      }
      //pthread_mutex_lock(&mutex);
      if (-1 == sem_wait(&sem)) perror("sem_wait");
   }
}

void * ptmain(void *where){
//return;
        int qi = *(int*)where%nq;
        int i;
        fprintf(stderr,"ptmain %d %d\n",*(int*)where, qi); fflush(stderr);
        while((i = atomic_fetch_add(&q[qi],1))<qq[qi]){
           fprintf(stderr,"inp %s\n",av[i]);
           doit(av[i]);
        }
}

void doit(char *s){
   FILE *pout[SZ];
   //int buf[NN];
   int *buf = malloc(NN*sizeof(int));
   int np;
   int n,j;
   for (np=0;np < nm;np++){
      //best[np] = -999;
      char thing[1000];
continue;
      sprintf(thing,"%s.%s.pout",s,av[np+1]);
      //if (np) continue; //added
      pout[np] = fopen(thing,"w");
      if (!pout[np]){
         perror(thing);
         exit(1);
      }
   }
   FILE *inf = fopen(s,"r");
   //printf("n %d\n",n);
   //for (int i=0;i<n;i++) printf("%d: %g\n",i,p[i]);
   int m = 0;
   float w[SZ];
   for (m=0;m<np;m++) w[m] = 0;
   m = 0;
   while (n = fread(buf,sizeof(int),NN-1,inf)) {
      for (j=0;j<n;j++) {
         if (buf[j] < 0) {
            for (m=0;m<nm;m++){
               //fprintf(pout[m],"%d",-buf[j]);
               //fprintf(pout[m]," %0.7f",w[m]);
               //fprintf(pout[m],"\n");
               if (w[m] > best[m]) {
                  node *x = malloc(sizeof(node));
                  x->off = -buf[j];
                  x->score = w[m];
                  x->m = m;
                  x->warcfile = s;
                  x->next = NULL;
                  int B; 
                  while (!(B = atomic_compare_exchange_weak(&head,&x->next,x))) {
                      fprintf(stderr,"BEST %lf %lf %d\n",w[m],best[m],B);
                  }
                  fprintf(stderr,"best %lf %lf %d %d\n",w[m],best[m],B,-buf[j]);
                  sem_wait(&insem);
                  //pthread_mutex_unlock(&mutex); 
                  if (sem_post(&sem) == -1) perror("sem_post nonfinal");
                  //best[m] = w[m];
               }
               w[m] = 0;
            }
            continue;
         }
         if (j == n-1) fread(buf+n,sizeof(int),1,inf);
         //printf("i %d x %0.8f\n",buf[j],((float *)buf)[j+1]);
         for (m=0;m<nm;m++){
            w[m] += p[m+nm*buf[j]] * ((float *)buf)[j+1];
         }
         j++;
      }
   }
   for (np=0;np < nm;np++){
continue;
      //if (np) continue;
      char thing[1000];
      sprintf(thing,"%s.%s.pout",s,av[np+1]);
      fclose(pout[np]);
   }
   for (np=0;np<nm;np++){
      //printf("%s %s %d %0.4lf\n",av[np+1],s,besti[np],best[np]);
      //fflush(stdout);
   }
   //printf("\n");
   free(buf);
   fclose(inf);
}


#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

/*
_Bool atomic_compare_exchange_weak( volatile A *obj,
                                    C* expected, C desired ); 

typedef struct node{int a; volatile struct node*b;} node;
volatile node *head;
volatile node *new;
volatile node *old;
int B;

int main(){
   head = NULL;
   new = malloc(sizeof(node));
   new->a = 1;
   old = head;
   new->b = old;
   B = atomic_compare_exchange_weak(&head,&new,new);
   printf("%d %p\n",B,head);
}
*/

/*
       #include <semaphore.h>

       sem_t sem;

           if (sem_post(&sem) == -1) {
               write(STDERR_FILENO, "sem_post() failed\n", 18);
               _exit(EXIT_FAILURE);
           }

           if (sem_init(&sem, 0, 0) == -1)
               handle_error("sem_init");

           while ((s = sem_timedwait(&sem, &ts)) == -1 && errno == EINTR)
*/
