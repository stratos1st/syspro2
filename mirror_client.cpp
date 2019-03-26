#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#include <sys/inotify.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

using namespace std;

bool endsWith(char* mainStr, char* toMatch);
int is_file(const char *path);
char* read_hole_file(char* file_name);
int file_sz(char* file_name);
int rvc_process(char* common_dir, char* new_dot_id, int id);
int send_process(char* common_dir, char* new_dot_id, int id, char* input_dir);

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


  //check for all existing files
  DIR *d=NULL;
  struct dirent *dir;
  if((d= opendir(common_dir))==NULL){
    cerr<< "opendir failed: "<< strerror(errno)<<endl;
    return 1;
  }
  //for all existing files
  while ((dir = readdir(d)) != NULL) {
    sprintf(any_path, "%d.id", id);
    //if it is a .id file and not mine
    if(endsWith(dir->d_name, ".id") && strcmp(dir->d_name,any_path)!=0){
      //create send child
      pid_t pid_send = fork();
      if (pid_send == 0){//---------------------------------------send child process
        if(send_process(common_dir, dir->d_name, id, input_dir)!=0){
          cerr<< "send_process0 failed \n";
          return 1;
        }
        return 0;
      }
      else if (pid_send > 0){
          // parent process
      }
      else{
        cerr<< "fork send0 failed: "<< strerror(errno)<<endl;
        return 1;
      }

      //create rcv child
      pid_t pid_rcv = fork();
      if (pid_rcv == 0){//---------------------------------------rcv child process
        if(rvc_process(common_dir, dir->d_name, id)!=0){
          cerr<< "rvc_proces0 failed \n";
          return 1;
        }
        return 0;
      }
      else if (pid_rcv > 0){
          // parent process

      }
      else{
        cerr<< "fork send0 failed: "<< strerror(errno)<<endl;
        return 1;
      }

    }
  }
  closedir(d);

  wait(NULL);

  //check if file added or deleted
  while(1) {
    char monitor_buffer[4096];
    struct inotify_event *event = NULL;

    int len = read(fdnotify, monitor_buffer, sizeof(monitor_buffer));
    if (len < 0) {
      cerr<<"!read: "<<strerror(errno)<<endl;
      return 1;
    }

    event = (struct inotify_event *) monitor_buffer;
    while(event != NULL) {
      //---------------file .id created
      if ((event->mask & IN_CREATE) && endsWith(event->name, ".id")){
        printf("File created: %s\n", event->name);

        //create send child
        pid_t pid_send = fork();
        if (pid_send == 0){//---------------------------------------send child process
          if(send_process(common_dir, event->name, id, input_dir)!=0){
            cerr<< "send_process failed \n";
            return 1;
          }
          return 0;
        }
        else if (pid_send > 0){
            // parent process
        }
        else{
          cerr<< "fork send failed: "<< strerror(errno)<<endl;
          return 1;
        }

        //create rcv child
        pid_t pid_rcv = fork();
        if (pid_rcv == 0){//---------------------------------------rcv child process
          if(rvc_process(common_dir, event->name, id)!=0){
            cerr<< "rvc_proces failed \n";
            return 1;
          }
          return 0;
        }
        else if (pid_rcv > 0){
            // parent process

        }
        else{
          cerr<< "fork send failed: "<< strerror(errno)<<endl;
          return 1;
        }

        wait(NULL);

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

int is_file(const char *path){
    // FIXME den doulebi
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

char* read_hole_file(char* file_name){// TODO if return NULL
  FILE *f = fopen(file_name, "rb");
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);  /* same as rewind(f); */

  char *string = (char*) malloc(fsize + 1);
  fread(string, fsize, 1, f);
  fclose(f);

  string[fsize] = '\0';

  return string;
}

int file_sz(char* file_name){// TODO if return NULL
  FILE *fp = fopen(file_name, "rb");

  fseek(fp, 0L, SEEK_END);
  int sz = ftell(fp);
  fclose(fp);

  return sz;
}

int rvc_process(char* common_dir, char* new_dot_id, int id){
  struct stat tmp_stat;
  //---------------------------------------rcv child process
    char pipe_name[50];
    cout<<"rcv child\n";
      //make and open pipe rcv
      new_dot_id[strlen(new_dot_id)-3]='\0';
      sprintf(pipe_name, "./%s/id%s_to_id%d.fifo",common_dir,new_dot_id,id);
      if(stat(pipe_name, &tmp_stat) != 0)
        if(mkfifo(pipe_name, 0666) !=0){
          if(errno!=EEXIST){
            cerr<< "mkfifo rcv failed: "<< strerror(errno)<<endl;
            //return 1;
            exit(1);
          }
          else
            cout<<"file rcv exists\n";
        }
      int pipe_rcv;
      if((pipe_rcv=open(pipe_name,O_RDONLY))<0){
        cerr<< "open rcv failed: "<< strerror(errno)<<endl;
        //return 1;
        exit(1);
      }

      cout<<"reading\n";
      fflush(stdout);
      // char tmp[50];
      // if(read(pipe_rcv, tmp, 12)<0){
      //   cerr<< "read from rcv pipe failed: "<< strerror(errno)<<endl;
      //   //return 1;
      //   exit(1);
      // }

      // cout<<"rcv child "<<tmp;

      close(pipe_rcv);

  return 0;
}

int send_process(char* common_dir, char* new_dot_id, int id, char* input_dir){
  struct stat tmp_stat;
  cout<<new_dot_id<<endl;
  //---------------------------------------send child process
  char pipe_name[50];
  cout<<"send child\n";
  //make and open pipe send
  new_dot_id[strlen(new_dot_id)-3]='\0';
  sprintf(pipe_name, "./%s/id%d_to_id%s.fifo",common_dir,id,new_dot_id);
  if(stat(pipe_name, &tmp_stat) != 0)
    if(mkfifo(pipe_name, 0666) !=0){
      if(errno!=EEXIST){
        cerr<< "mkfifo send failed: "<< strerror(errno)<<endl;
        //return 1;
        exit(1);
      }
      else
        cout<<"file send exists\n";
    }
  int pipe_send;
  if((pipe_send=open(pipe_name,O_WRONLY))<0){
    cerr<< "open send failed: "<< strerror(errno)<<endl;
    //return 1;
    exit(1);
  }

  cout<<"writing\n";
  fflush(stdout);

  DIR *d=NULL;
  struct dirent *dir;
  if((d= opendir(input_dir))==NULL){
    cerr<< "opendir send failed: "<< strerror(errno)<<endl;
    //return 1;
    exit(1);
  }
  //for all the files i need to send
  while ((dir = readdir(d)) != NULL){
  char* filename=dir->d_name;
  // TODO parsing
    if(strcmp(filename,".")!=0 && strcmp(filename,"..")!=0){ // TODO && is_file(filename)){
      char buffer[100],*tmp;// TODO check size

      //write name
      sprintf(buffer, "%d",strlen(filename));
      if(write(pipe_send, buffer, 2)){
        cerr<< "write send failed: "<< strerror(errno)<<endl;
        //return 1;
        exit(1);
      }
      if(write(pipe_send, filename, strlen(filename)+1)){
        cerr<< "write send failed: "<< strerror(errno)<<endl;
        //return 1;
        exit(1);
      }

      //write file
      sprintf(buffer, "%d",file_sz(filename));
      if(write(pipe_send, buffer, 4)){
        cerr<< "write send failed: "<< strerror(errno)<<endl;
        //return 1;
        exit(1);
      }
      tmp= read_hole_file(filename);
      if(write(pipe_send, tmp, file_sz(filename))){
        cerr<< "write send failed: "<< strerror(errno)<<endl;
        //return 1;
        exit(1);
      }
    }
  }
  if(write(pipe_send, "00", 2)){
    cerr<< "write send failed: "<< strerror(errno)<<endl;
    //return 1;
    exit(1);
  }
  closedir(d);


  close(pipe_send);

  return 0;
}
