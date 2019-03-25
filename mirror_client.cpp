#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <sys/inotify.h>

using namespace std;

bool endsWith(char* mainStr, char* toMatch);

int main(int argc, char * argv[]){

  int id, buffer_size;
  char common_dir[50], input_dir[50], mirror_dir[50], log_file[50];
  char any_path[100];


//---------------------------------------------parsing command line argumets------------------------------------------
  int opt;
  while((opt = getopt(argc, argv, "n:c:i:m:b:l:")) != -1) {
    switch(opt){
      case 'n':
        id=strtol(optarg, NULL, 10);
        break;
      case 'c':
        strcpy(common_dir,optarg);
        break;
      case 'i':
        strcpy(input_dir,optarg);
        break;
      case 'm':
        strcpy(mirror_dir,optarg);
        break;
      case 'b':
        buffer_size=strtol(optarg, NULL, 10);
        break;
      case 'l':
        strcpy(log_file,optarg);
        break;
    }
  }

  //check for command argument errors
  if(buffer_size<=0){
    cerr <<"!buffer size must be >0\n";
    return 1;
  }
  if(id<0){
    cerr <<"!id must be >=0\n";
    return 1;
  }
  struct stat tmp_stat;
  if(stat(input_dir, &tmp_stat) != 0){
    cerr <<"!input_dir does not exist "<<input_dir<<"\n";
    return 1;
  }
  if(stat(mirror_dir, &tmp_stat) == 0){
    cerr <<"!mirror_dir exists "<<mirror_dir<<"\n";
    return 1;
  }
  sprintf(any_path, "%s/%d.id", common_dir, id);//path to common_dir/.id file
  if(stat(any_path, &tmp_stat) == 0){
    cerr <<"!.id file exists "<<id<<"\n";
    return 1;
  }
  // optind is for the extra arguments
  for(; optind < argc; optind++){
    cerr <<"!extra arguments: "<<argv[optind]<<endl;
    return 1;
  }

//---------------------------------------------making dirs and files------------------------------------------
  //make mirror_dir
  if(mkdir(mirror_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
    cerr <<"!Unable to create mirror_dir "<<mirror_dir<<"\n";
    return 1;
  }
  //make common_dir
  if(stat(common_dir, &tmp_stat) != 0)
    if(mkdir(common_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
      cerr <<"!Unable to create directory "<<common_dir<<"\n";
      return 1;
    }

  //make .id file
  FILE *common_dot_id_file=NULL;
  sprintf(any_path, "%s/%d.id", common_dir, id);//path to common_dir/.id file
  common_dot_id_file=fopen(any_path, "a");
  if(common_dot_id_file==NULL){
    cerr <<"!Can not create .id file "<<id<<"\n";
    return 1;
  }
  fprintf(common_dot_id_file, "%d", getpid());

//---------------------------------------------watching common directory------------------------------------------
  int fdnotify = -1;

  fdnotify = inotify_init();
  if (fdnotify < 0){
    cerr<<"inotify_init failed:" <<strerror(errno)<<endl;
    return 1;
  }
  int wd = inotify_add_watch(fdnotify, common_dir, IN_CREATE | IN_DELETE);
  if (wd < 0) {
    cerr<< "inotify_add_watch failed: "<< strerror(errno)<<endl;
    return 1;
  }

  while(1) {
    char buffer[4096];
    struct inotify_event *event = NULL;

    int len = read(fdnotify, buffer, sizeof(buffer));
    if (len < 0) {
      cerr<<"read: "<<strerror(errno)<<endl;
      return 1;
    }

    event = (struct inotify_event *) buffer;
    while(event != NULL) {
      //---------------file .id created
      if ((event->mask & IN_CREATE) && endsWith(event->name, ".id")){
        printf("File created: %s\n", event->name);
      }
      //---------------file .id deleted
      else if ((event->mask & IN_DELETE) && endsWith(event->name, ".id")){
        printf("File deleted: %s\n", event->name);
      }
      // else {
      //   printf("Unknown Mask 0x%.8x\n", event->mask);
      // }

      // Move to next struct
      len -= sizeof(*event) + event->len;
      if (len > 0)
        event =(inotify_event*)( ((void *) event) + sizeof(event) + event->len);
      else
        event = NULL;
    }

  }






  return 0;
}

bool endsWith(char* mainStr, char* toMatch){
	if(strlen(mainStr) >= strlen(toMatch) &&
			strcmp(mainStr + strlen(mainStr) - strlen(toMatch), toMatch) == 0)
			return true;
		else
			return false;
}
