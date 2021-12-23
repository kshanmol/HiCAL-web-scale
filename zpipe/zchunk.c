/* zpipe.c: example of proper use of zlib's inflate() and deflate()
   Not copyrighted -- provided to the public domain
   Version 1.4  11 December 2005  Mark Adler */

/* Version history:
   1.0  30 Oct 2004  First version
   1.1   8 Nov 2004  Add void casting for unused return values
                     Use switch statement for inflate() return values
   1.2   9 Nov 2004  Add assertions to document zlib guarantees
   1.3   6 Apr 2005  Remove incorrect assertion in inf()
   1.4  11 Dec 2005  Add hack to avoid MSDOS end-of-line conversions
                     Avoid some compiler warnings for input and output buffers
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "zlib.h"
#include <stdlib.h>
#include <ctype.h>
using namespace std;
#include <string>
#include <map>
#include <iostream>
#include <fstream>
#include "porter.h"
long ndocs;
map<string,long> df;

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

#define CHUNK (2*1024*1024)

string dffile;
char *ikey = NULL;
int seekoff=0;

unsigned char * findit(unsigned char *buf, unsigned long len){
   unsigned long off;
   for (off=2;off<len;off++){
      if (buf[off]!='\n') continue;
      if (buf[off-1]!='\n'&&buf[off-2]!='\n') continue;
      return buf+off+1;
   }
   return NULL;
}

/* Compress from file source to file dest until EOF on source.
   def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_STREAM_ERROR if an invalid compression
   level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
   version of the library linked do not match, or Z_ERRNO if there is
   an error reading or writing the files. */
int def(FILE *source, FILE *dest, int level)
{
    int ret, flush;
    unsigned have;
    int needreset = 0;
    int moreflag = 0;
    z_stream strm;
    unsigned char in[CHUNK+1];
    unsigned char * inptr;
    unsigned long inavail = 0;
    unsigned char out[CHUNK];
    unsigned long inneed = 0;
    unsigned long total_offset = 0;
    unsigned long total_in = 0;

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    //ret = deflateInit(&strm, level);
    ret = deflateInit2(&strm, level, Z_DEFLATED, 16+MAX_WBITS,9,Z_DEFAULT_STRATEGY);  // gvc +16 creates gzip header
    if (ret != Z_OK)
        return ret;

    /* compress until end of file */
againd:
    //fprintf(stderr,"total_offset: %lu total_in %lu\n",total_offset,total_in);
    do {
        if (inavail == 0) {
           inavail = fread(in, 1, CHUNK, source);
           if (ferror(source)) {
               (void)deflateEnd(&strm);
               return Z_ERRNO;
           }
           inptr = in;
           in[inavail] = 0;
        }
	if (inneed == 0) { // new warc header
           unsigned char *lflf = findit(inptr,inavail);  // end of header
	   //fprintf(stderr,"firstchr %x\n",*inptr);
	   //if (*inptr != 0x57) fprintf(stderr,"got this:%s\n",inptr);
	   if (!lflf || lflf < inptr + 10) {
	      if (inptr > in) {
	         memmove(in,inptr,inavail);
		 unsigned inmore = fread(in+inavail,1,CHUNK-inavail,source);
                 if (ferror(source)) {
                     (void)deflateEnd(&strm);
                     return Z_ERRNO;
                 }
	         inptr = in;
		 inavail += inmore;
		 in[inavail] = 0;
                 lflf = findit(inptr+10,inavail);  // end of header
	      }
           }
	   if (!lflf) {
		//inptr[8] = 0;
		//fprintf(stderr,"header too big:  in %lu inptr %lu inavail %lu stuff %s\n",in,inptr,inavail,inptr);
                inneed = inavail;
		//return -1;
           } else {
	       unsigned char *c = strcasestr((char *)inptr,"\ncontent-length:");
	       unsigned long cl;
	       if (!c) { // wtf?
		  //fprintf(stderr,"oops! \n%s\n",inptr);
		  unsigned char *p = inptr;
		  unsigned int av = inavail;
		  while (!c && strlen((char*)p) < av){
	             av = av - strlen((char*)p)-1;
		     p = p + strlen((char*)p)+1;
		     c = strcasestr((char *)p,"\ncontent-length:");
		  }
	       }
               if (c && 1 == sscanf(c+16,"%lu",&cl)) {
	          //fprintf(stderr,"content-length %p %lu\n",c,cl);
		  if (c > lflf){
		     //fprintf(stderr, "lflf problem\n");
		     lflf = findit(c,inptr+inavail-c);
		  }
		  inneed = lflf+cl-inptr;

	       } else {
		   fprintf(stderr,"content-length fail\n%s\n",inptr);
		   exit(1);
	       }
           }
	}
	       
	//if (!lflf) fprintf(stderr, "no header\n");
	//else fprintf(stderr,"header\n");
        strm.next_in = inptr;
        //fprintf(stderr,"inavail %lu inneed %lu\n",inavail, inneed);
	if (inavail >= inneed){
            if (moreflag) inneed = moreflag = 0;
            strm.avail_in = inneed;
	    inavail -= inneed;
	    inptr += inneed;
	    //flush = Z_NO_FLUSH;
            while (inavail && isspace(*inptr) ) {
               inptr++;
               strm.avail_in++;
               inavail--;
            }
            if (!inavail && !feof(source)) {
               moreflag=1;
               flush = Z_NO_FLUSH;
               inneed = 1;
            } else {
	       flush = Z_FINISH;
	       inneed = 0;
            }
	}else{
            strm.avail_in = inavail;
	    inptr += inavail;
	    inneed -= inavail;
	    inavail = 0;
            //flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
	    flush = Z_NO_FLUSH;
	    //fprintf(stderr,"flush %d %d\n",flush,Z_FINISH);
	}
	//fprintf(stderr,"%p %lu %p %u\n",inptr,inavail,strm.next_in,strm.avail_in);

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            if (needreset) {
               deflateReset(&strm);
               needreset = 0;
            }
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)deflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in file processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    //(void)deflateEnd(&strm);
    if (!feof(source) || inavail) {
       //fprintf(stderr,"total_out: %lu\n",strm.total_out);
       total_offset += strm.total_out;
       total_in += strm.total_in;
       //deflateReset(&strm);
       needreset = 1;
       goto againd;
    }
    return Z_OK;
}

/* Decompress from file source to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */
int inf(FILE *source, FILE *dest)
{
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK+1];
    unsigned long total_in=0;
    unsigned long lastread=0;
    int reset=1;

    out[CHUNK]=0;
    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    //ret = inflateInit2(&strm, 32+MAX_WBITS);  // gvc was inflateInit, 1 parm
    ret = inflateInit2(&strm, 32+MAX_WBITS);  // gvc was inflateInit, 1 parm

		//fprintf(stderr,"Init %d avail %d next %p \n",ret,strm.avail_in,strm.next_in);
    if (ret != Z_OK)
        return ret;

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = lastread = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)inflateEnd(&strm);
            return Z_ERRNO;
        }
        if (strm.avail_in == 0) 
            break;
        strm.next_in = in;

        /* run inflate() on input until output buffer not full */
      again:
	//fprintf(stderr,"total_in %lu %lu\n",total_in,total_in+1);
        do {
            if (!strm.avail_out){
               strm.avail_out = CHUNK;
               strm.next_out = out;
	    }
            //ret = inflate(&strm, Z_NO_FLUSH);
            ret = inflate(&strm, Z_SYNC_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return ret;
            }
            have = CHUNK - strm.avail_out;
	    if (reset && !seekoff) {
	       //fprintf(stderr,"try Reset\n");
               unsigned char *c = strcasestr((char *)out,"\ncontent-length:");
	       if (!c) { // wtf?
		  //fprintf(stderr,"oops! \n%s\n",out);
		  unsigned char *p = out;
		  unsigned int av = have;
		  while (!c && strlen((char*)p) < av){
	             av = av - strlen((char*)p)-1;
		     p = p + strlen((char*)p)+1;
		     c = strcasestr((char*)p,"\ncontent-length:");
		  }
	       }
	       unsigned long cl = 0;
	       if(c) {
		   sscanf(c+16,"%lu",&cl);
	           unsigned char *lflf = findit(c,out+have-c);
	           //fprintf(stderr,"out %p c %p cl %lu lflf %p have %d lflfcl %p\n",out,c,cl,lflf,have,lflf+cl);
		   //fflush(stderr);
		   if (lflf) {
		   //fsync(2);
            if (cl > 30000) cl=30000;
	    if (lflf+cl <= out+have) {

            char* fileData = lflf;
	    //printf("%s\n",fileData);
            //const char *filesuf = strrchr(filename.c_str(),'/');
            //if (!filesuf++) filesuf = filename.c_str();
            ndocs++;
            //fin.read(fileData, size);
            if (ikey){
               //fprintf(stderr,"point 0\n");
               char s[1000],ss[1000];
               sprintf(s,"\n%s:",ikey);
               //fprintf(stderr,"ikey:%sx\n",s);
               unsigned char *t = strstr((char*)out,s);
               //fprintf(stderr,"point 1\n");
               if (t) {
               //fprintf(stderr,"point 2\n");
                  assert (1 == sscanf(t+strlen(s),"%s",ss));
                  printf("%s %ld\n",ss,total_in+1);
               } else {
                    //fprintf(stderr,"point 3\n");
                  fprintf(stderr,"no document id\n",c);
               }
            }else{
                map<string,long> tf;
                int i,j;
                fileData[cl] = 0;
                for (i=0;i<cl;i=j) {
                   int skip = 0;
                   for (;i<cl && !isalnum(fileData[i]);i++);
                   for (j=i;isalnum(fileData[j]);j++) 
                      if (isdigit(fileData[j])) skip=1;
                      else fileData[j] = tolower(fileData[j]);
                   fileData[j]=0;
                   //cout << fileData+i << ' ';
                   if (!skip) {
                      fileData[stem(fileData,i,j-1)+1]=0;
                      if (fileData[i] && fileData[i+1]) {
                         //cout << fileData+i;
                         long x = tf[fileData+i]++;
                         if (!x) df[fileData+i]++;
                         //cout << ' ' << x;
                      }
                   }
                   //cout << '\n';
                }
                for( map<string,long>::const_iterator it = tf.begin(); it != tf.end(); ++it ) {
                   string key = it->first;
                   long value = it->second;
                   //cout << filesuf << ' ' << value << ' ' << key << '\n';
		   printf("%ld %d %s\n",total_in+1,value,key.c_str());
                }
            }


	       //fprintf(stderr,"did Reset\n");
	       reset=0;
	       strm.avail_out = 0;
	       //strm.next_out = out;
	       //strm.avail_out = CHUNK;
	       } //else {fprintf(stderr,"no room %llu\n",strm.avail_out);}
	       } //else {fprintf(stderr,"no crlf\n");}
             }//else{fprintf(stderr,"No c\n");}
	    } else if (seekoff){
               fwrite(out,1,have,stdout); 
	       strm.next_out = out;
	       strm.avail_out = CHUNK;
	    }
            //if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                //(void)inflateEnd(&strm);
                //return Z_ERRNO;
            //}
        } while (strm.avail_out == 0);

	if (ret == Z_STREAM_END) {
		if (!lastread || seekoff) break;
		//fprintf(stderr,"Reset %d avail %d next %p %08lx\n",ret,strm.avail_in,strm.next_in,*(unsigned long*)strm.next_in);
		total_in += strm.total_in;
               //strm.avail_out = CHUNK;
               //strm.next_out = out;
		ret = inflateReset(&strm);
		reset=1;
		//fprintf(stderr,"Reset %d avail %d next %p %08lx\n",ret,strm.avail_in,strm.next_in,*(unsigned long*)strm.next_in);
		goto again;
	}

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    if (!ikey && !seekoff && dffile != "/dev/null"){
       ofstream fout;
       fout.open(dffile);
       for( map<string,long>::const_iterator it = df.begin(); it != df.end(); ++it ) {
          string key = it->first;
          long value = it->second;
          if (value > 0) fout << value << ' ' << key << '\n';
       }
       fout.close();
       fout.open(dffile=="/dev/null"?dffile:(dffile + "N"));
       fout << ndocs << '\n';
       fout.close();
    }
    return (reset || ret == Z_STREAM_END) ? Z_OK : Z_DATA_ERROR;
}

/* report a zlib or i/o error */
void zerr(int ret)
{
    fputs("zpipe: ", stderr);
    switch (ret) {
    case Z_ERRNO:
        if (ferror(stdin))
            fputs("error reading stdin\n", stderr);
        if (ferror(stdout))
            fputs("error writing stdout\n", stderr);
        break;
    case Z_STREAM_ERROR:
        fputs("invalid compression level\n", stderr);
        break;
    case Z_DATA_ERROR:
        fputs("invalid or incomplete deflate data\n", stderr);
        break;
    case Z_MEM_ERROR:
        fputs("out of memory\n", stderr);
        break;
    case Z_VERSION_ERROR:
        fputs("zlib version mismatch!\n", stderr);
    }
}

/* compress or decompress from stdin to stdout */
int main(int argc, char **argv)
{
    int ret;

    /* avoid end-of-line conversions */
    SET_BINARY_MODE(stdin);
    SET_BINARY_MODE(stdout);

    /* do compression if no arguments */
    if (argc == 1) {
        ret = def(stdin, stdout, Z_BEST_COMPRESSION);
        if (ret != Z_OK)
            zerr(ret);
        return ret;
    }

    /* do decompression if -d specified */
    else if (argc >= 2 && strcmp(argv[1], "-d") == 0) {
	if (argc >= 3) dffile = argv[2];
	else dffile = "/dev/null";
        ret = inf(stdin, stdout);
        if (ret != Z_OK)
            zerr(ret);
        return ret;
    }
    else if (argc >= 2 && strcmp(argv[1], "-i") == 0) {
	if (argc >= 3) ikey = argv[2];
	else ikey = "WARC-TREC-ID";  
        ret = inf(stdin, stdout);
        if (ret != Z_OK)
            zerr(ret);
        return ret;
    }
    else if (argc >= 2 && strcmp(argv[1], "-s") == 0) {
	if (argc >= 3) seekoff = atoi(argv[2])-1;
        else seekoff = 0;
        fseek(stdin, seekoff, SEEK_SET);
        seekoff = 1;
        ret = inf(stdin, stdout);
        if (ret != Z_OK)
            zerr(ret);
        return ret;
    }

    /* otherwise, report usage */
    else {
        fputs("zpipe usage: zpipe [-d] < source > dest\n", stderr);
        return 1;
    }
}

