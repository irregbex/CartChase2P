#include "windows_music_pack.h"
#include <windows.h>
#include <objbase.h>
#include <miniz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int Dkc1WindowsMusicTrackName(const char *name){
 const char *leaf=name;for(const char *p=name;*p;p++)if(*p=='/'||*p=='\\')leaf=p+1;
 const char *digits=!strncmp(leaf,"track-",6)?leaf+6:!strncmp(leaf,"dkc_msu-",8)?leaf+8:NULL;
 if(!digits||*digits<'0'||*digits>'9')return 0;char *end;long n=strtol(digits,&end,10);
 return n>=1&&n<=32&&!strcmp(end,".pcm")?(int)n:0;
}
char *Dkc1WindowsExtractMusicPack(const char *archive,const char *root,char *error,size_t cap){
 mz_zip_archive zip={0};if(!mz_zip_reader_init_file(&zip,archive,0)){snprintf(error,cap,"Not a supported ZIP/MSU1 archive");return NULL;}
 int indices[32];for(int i=0;i<32;i++)indices[i]=-1;int count=0;uint64_t total=0;
 for(mz_uint i=0;i<zip.m_total_files;i++){
  mz_zip_archive_file_stat st;if(!mz_zip_reader_file_stat(&zip,i,&st))goto invalid;
  int track=Dkc1WindowsMusicTrackName(st.m_filename);if(!track)continue;
  if(st.m_is_directory||st.m_is_encrypted||((st.m_external_attr>>16)&0170000)==0120000||
     st.m_uncomp_size<12||st.m_uncomp_size>1073741824ULL||indices[track-1]>=0)goto invalid;
  total+=st.m_uncomp_size;if(total>8589934592ULL)goto invalid;
  indices[track-1]=(int)i;count++;
 }
 if(!count)goto invalid;
 GUID guid;if(FAILED(CoCreateGuid(&guid)))goto invalid;
 char directory[4096],parent[4096];snprintf(parent,sizeof parent,"%s/music-packs",root);CreateDirectoryA(parent,NULL);
 snprintf(directory,sizeof directory,"%s/%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",parent,guid.Data1,guid.Data2,guid.Data3,
  guid.Data4[0],guid.Data4[1],guid.Data4[2],guid.Data4[3],guid.Data4[4],guid.Data4[5],guid.Data4[6],guid.Data4[7]);
 if(!CreateDirectoryA(directory,NULL)){snprintf(error,cap,"Cannot create private music directory");mz_zip_reader_end(&zip);return NULL;}
 for(int i=0;i<32;i++)if(indices[i]>=0){char path[4096];snprintf(path,sizeof path,"%s/track-%d.pcm",directory,i+1);
  if(!mz_zip_reader_extract_to_file(&zip,indices[i],path,0)){snprintf(error,cap,"Music extraction failed; partial folder was not selected");mz_zip_reader_end(&zip);return NULL;}}
 mz_zip_reader_end(&zip);return _strdup(directory);
invalid:
 snprintf(error,cap,"Archive has no supported tracks, unsafe entries, duplicates, or exceeds size limits");mz_zip_reader_end(&zip);return NULL;
}
