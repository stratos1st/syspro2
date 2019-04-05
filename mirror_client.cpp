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
#include <sys/file.h>

using namespace std;
//FIXME otan lambano sima apo ta pedia kapies fores emfanizi mono ena apo ta 2
//TODO 30 sec
//TODO check if lockfile is ok
//TODO handler globals na ginoun pointers
//TODO send with buffer sz

bool endsWith(char* mainStr, char* toMatch);//returns true id mainStr ends with toMatch
int is_file(const char *path);//returns true if path is a file
char* read_hole_file(char* file_name);// returns pointer to the contents of the file
int file_sz(char* file_name);// returns the number of characters in the file
void write_to_logfile(int send, int send_bytes, int logfile);//writes to logfile and handles flock
int rvc_process(char* pipe_name, char* new_dot_id, int id, char* mirror_dir, int logfile);//handles all the rcv part
int send_process(char* pipe_name, int id, char* sub_dir, char* input_dir, int logfile);//handles all the send part
int delete_process( char* mirror_dir, char* dot_id_file);//handles all the delete part
void usr1_signal_handler(int sig);//signal handler for SIGUSR1
void kill_signal_handler(int sig);//signal handler for SIGKILL and SIGINT

//globals for signal handlers
char handler_common_dir[50], handler_mirror_dir[50];
int handler_id, handler_logfile_fd;

int main(int argc, char * argv[]){
  int id, buffer_size, logfile_fd;
  char common_dir[50], input_dir[50], mirror_dir[50], log_file[50];
  char any_path[100];

  //asigning signal handlers
  //FIXEME kamia fora stin 2h fora pou to trexo bgazi mono ena "child failed"
  static struct sigaction act;
  act.sa_handler = usr1_signal_handler;
  sigfillset (&(act.sa_mask ));
  sigaction (SIGUSR1, &act, NULL);

  static struct sigaction act2;
  act2.sa_handler = kill_signal_handler;
  sigfillset (&(act2.sa_mask ));
  sigaction (SIGKILL, &act2, NULL);
  sigaction (SIGINT, &act2, NULL);

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
  if(mkdir(mirror_dir, 0766)){
    cerr <<"!Unable to create mirror_dir "<<mirror_dir<<"\n";
    return 1;
  }
  //make common_dir
  if(stat(common_dir, &tmp_stat) != 0)
    if(mkdir(common_dir, 0766)){
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

  //make log_file
  logfile_fd=-1;
  if((logfile_fd=open(log_file,O_TRUNC | O_CREAT | O_WRONLY, 0766))<=0){
    cerr <<"!Can not create logfile file "<<log_file<<"\n";
    return 1;
  }
  char write_tmp[50];
  sprintf(write_tmp, "%d\n",id);
  write(logfile_fd, write_tmp, strlen(write_tmp));

  //initializing kill handler values
  strcpy(handler_common_dir,common_dir);
  strcpy(handler_mirror_dir,mirror_dir);
  handler_id=id;
  handler_logfile_fd=logfile_fd;

  //watching common directory
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

//------------------------------------check for all existing files-----------------------------------------
  DIR *d=NULL;
  struct dirent *dir;
  if((d=opendir(common_dir))==NULL){
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
        char pipe_name[50], new_dot_id[50];
        cout<<"send child\n";
        //make pipe send
        strcpy(new_dot_id,dir->d_name);
        new_dot_id[strlen(new_dot_id)-3]='\0';
        sprintf(pipe_name, "./%s/id%d_to_id%s.fifo",common_dir,id,new_dot_id);
        if(stat(pipe_name, &tmp_stat) != 0){
          if(mkfifo(pipe_name, 0666) !=0){
            if(errno!=EEXIST){
              cerr<< "mkfifo send failed: "<< strerror(errno)<<endl;
              return 1;
            }
          }
        }
        else
          cout<<"file send exists\n";
        //call send function
        if(send_process(pipe_name, id, "", input_dir,logfile_fd)!=0){
          cerr<< "send_proces0 failed \n";
          return 1;
        }
        unlink(pipe_name);
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
        char pipe_name[50], new_dot_id[50];
        cout<<"rcv child\n";
        //make and open pipe rcv
        strcpy(new_dot_id,dir->d_name);
        new_dot_id[strlen(new_dot_id)-3]='\0';
        sprintf(pipe_name, "./%s/id%s_to_id%d.fifo",common_dir,new_dot_id,id);
        if(stat(pipe_name, &tmp_stat) != 0){
          if(mkfifo(pipe_name, 0666) !=0){
            if(errno!=EEXIST){
              cerr<< "mkfifo rcv failed: "<< strerror(errno)<<endl;
              return 1;
            }
          }
        }
        else
          cout<<"file rcv exists\n";

        char tmp[50];
        //create mirror/id directory
        sprintf(tmp, "%s/%s",mirror_dir,new_dot_id);
        if(stat(tmp, &tmp_stat) != 0)
          if(mkdir(tmp, 0777)){
            cerr <<"!rcv acn not create "<<tmp<<"\n";
            return 1;
          }
        //call send function
        if(rvc_process(pipe_name, new_dot_id, id, mirror_dir,logfile_fd)!=0){
          cerr<< "rvc_proces0 failed \n";
          return 1;
        }
        unlink(pipe_name);
        return 0;
      }
      else if (pid_rcv > 0){
          // parent process

      }
      else{
        cerr<< "fork send0 failed: "<< strerror(errno)<<endl;
        return 1;
      }

      wait(NULL);
      wait(NULL);
      cout<<"Done for client "<<dir->d_name<<endl;

    }
  }
  closedir(d);

  cout<<"teliosa me tous proigoumenous\n";

//------------------------------------check if file added or deleted-----------------------------------------
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
          char pipe_name[50], new_dot_id[50];
          cout<<"send child\n";
          //make pipe send
          strcpy(new_dot_id,event->name);
          new_dot_id[strlen(new_dot_id)-3]='\0';
          sprintf(pipe_name, "./%s/id%d_to_id%s.fifo",common_dir,id,new_dot_id);
          if(stat(pipe_name, &tmp_stat) != 0){
            if(mkfifo(pipe_name, 0666) !=0){
              if(errno!=EEXIST){
                cerr<< "mkfifo send failed: "<< strerror(errno)<<endl;
                return 1;
              }
            }
          }
          else
            cout<<"file send exists\n";
          //call send function
          if(send_process(pipe_name, id, "", input_dir,logfile_fd)!=0){
            cerr<< "send_proces0 failed \n";
            return 1;
          }
          unlink(pipe_name);
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
          char pipe_name[50], new_dot_id[50];
          cout<<"rcv child\n";
          //make and open pipe rcv
          strcpy(new_dot_id,event->name);
          new_dot_id[strlen(new_dot_id)-3]='\0';
          sprintf(pipe_name, "./%s/id%s_to_id%d.fifo",common_dir,new_dot_id,id);
          if(stat(pipe_name, &tmp_stat) != 0){
            if(mkfifo(pipe_name, 0666) !=0){
              if(errno!=EEXIST){
                cerr<< "mkfifo rcv failed: "<< strerror(errno)<<endl;
                return 1;
              }
            }
          }
          else
            cout<<"file rcv exists\n";

          char tmp[50];
          //create mirror/id directory
          sprintf(tmp, "%s/%s",mirror_dir,new_dot_id);
          if(stat(tmp, &tmp_stat) != 0)
            if(mkdir(tmp, 0777)){
              cerr <<"!rcv acn not create "<<tmp<<"\n";
              return 1;
            }
          //call send function
          if(rvc_process(pipe_name, new_dot_id, id, mirror_dir,logfile_fd)!=0){
            cerr<< "rvc_proces0 failed \n";
            return 1;
          }
          unlink(pipe_name);
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
        wait(NULL);
        cout<<"Done for client "<<event->name<<endl;

      }
      //---------------file .id deleted
      else if ((event->mask & IN_DELETE) && endsWith(event->name, ".id")){
        printf("File deleted: %s\n", event->name);

        //create delete child
        pid_t pid_send = fork();
        if (pid_send == 0){//---------------------------------------send child process
          if(delete_process(mirror_dir, event->name)!=0){
            cerr<< "delete_process failed \n";
            return 1;
          }
          return 0;
        }
        else if (pid_send > 0){
            // parent process
        }
        else{
          cerr<< "fork delete failed: "<< strerror(errno)<<endl;
          return 1;
        }
      }
      else {
        printf("Unknown Mask 0x%.8x %s\n", event->mask,event->name);
        // printf("Mask 0x%.8x\n", IN_IGNORED);
        // printf("Mask 0x%.8x\n", IN_CREATE);
        // printf("Mask 0x%.8x\n", IN_DELETE);
      }

      // Move to next struct
      len -= sizeof(struct inotify_event) + event->len;
      if (len > 0)
        event =(inotify_event*)( ((void *) event) + sizeof(struct inotify_event) + event->len);
      else
        event = NULL;
    }

  }

  return 0;
}

//returns true id mainStr ends with toMatch
bool endsWith(char* mainStr, char* toMatch){
	if(strlen(mainStr) >= strlen(toMatch) &&
			strcmp(mainStr + strlen(mainStr) - strlen(toMatch), toMatch) == 0)
			return true;
		else
			return false;
}

//returns true if path is a file
int is_file(const char *path){
    struct stat path_stat;
    if(stat(path, &path_stat)!=0){
      cerr<< "is_file stat failed: "<< strerror(errno)<<path<<endl;
      //return 1;
      exit(1);
    }
    return S_ISREG(path_stat.st_mode);
}

// returns pointer to the contents of the file
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

// returns the number of characters in the file
int file_sz(char* file_name){
  int fd =-1;
  if((fd=open(file_name, O_RDONLY))<0){
    cerr<< "file_sz openfile failed: "<< strerror(errno)<<file_name<<endl;
    //return 1;
    exit(1);
  }

  int sz =lseek(fd, 0L, SEEK_END);
  close(fd);

  return sz;
}

//writes to logfile and handles flock
void write_to_logfile(int send, int send_bytes, int logfile){
  flock(logfile,LOCK_EX);
  char tmp[50];
  if(send==1)
    sprintf(tmp, "send %d\n",send_bytes);
  else if(send==0)
    sprintf(tmp, "rcv %d\n",send_bytes);
  else if(send==2)
    sprintf(tmp, "rcved file\n");
  else if(send==3)
    sprintf(tmp, "sended file\n");

  write(logfile, tmp, strlen(tmp));
  fdatasync(logfile);
  flock(logfile,LOCK_UN);
}

//handles all the rcv part
int rvc_process(char* pipe_name, char* new_dot_id, int id, char* mirror_dir, int logfile){
  //---------------------------------------rcv child process
  char tmp[50];
  struct stat tmp_stat;
  //open pipe
  int pipe_rcv=-1;
  if((pipe_rcv=open(pipe_name,O_RDWR))<0){
    cerr<< "open rcv failed: "<< strerror(errno)<<endl;
    return 1;
  }

  cout<<"reading\n";

  char buffer[999];
  if(read(pipe_rcv, buffer, 2)<0){
    cerr<< "read rcv failed: "<< strerror(errno)<<endl;
    return 1;
  }
  write_to_logfile(0, 2, logfile);
  buffer[2]='\0';
  int sz=stol(buffer, NULL, 10);
  while(sz!=0){
    //read name
    if(read(pipe_rcv, buffer, sz)<0){
      cerr<< "read rcv failed: "<< strerror(errno)<<endl;
      return 1;
    }
    write_to_logfile(0, sz, logfile);
    buffer[sz]='\0';
    cout<<"\t"<<buffer<<endl;

    //make dir
    sprintf(tmp, "%s",buffer);
    char *tmpp=strrchr(tmp,'/');
    if(tmpp!=NULL){// if not in the starting directory
      *tmpp='\0';
      char tmp2[100];
      sprintf(tmp2, "%s/%s/%s",mirror_dir,new_dot_id,tmp);
      if(stat(tmp2, &tmp_stat) != 0)
        if(mkdir(tmp2, 0766)){
          cerr <<"!rcv can not create directory "<<tmp2<<"\n";
          return 1;
        }
    }

    //make file
    FILE *new_file=NULL;
    sprintf(tmp, "%s/%s/%s",mirror_dir,new_dot_id,buffer);
    new_file=fopen(tmp, "w");
    if(new_file==NULL){
      cerr <<"!rcv can not create "<<tmp<<" file "<<strerror(errno)<<"\n";
      return 1;
    }

    //read file
    if(read(pipe_rcv, buffer, 4)<0){
      cerr<< "read rcv failed: "<< strerror(errno)<<endl;
      return 1;
    }
    write_to_logfile(0, 4, logfile);
    buffer[4]='\0';
    sz=stol(buffer, NULL, 10);
    if(read(pipe_rcv, buffer, sz)<0){
      cerr<< "read rcv failed: "<< strerror(errno)<<endl;
      return 1;
    }
    write_to_logfile(0, sz, logfile);
    buffer[sz]='\0';

    write_to_logfile(2, 0, logfile);

    //write to file
    fprintf(new_file, "%s", buffer);
    fclose(new_file);

    //read next name or 00
    if(read(pipe_rcv, buffer, 2)<0){
      cerr<< "read rcv failed: "<< strerror(errno)<<endl;
      return 1;
    }
    write_to_logfile(0, 2, logfile);
    buffer[2]='\0';
    sz=stol(buffer, NULL, 10);
  }

  cout<<"TELIOSA\n";

  close(pipe_rcv);

  return 0;
}

//handles all the send part
int send_process(char* pipe_name, int id, char* sub_dir, char* input_dir, int logfile){
  struct stat tmp_stat;
  //FIXME an enas fakelos ine kenos
  //---------------------------------------send child process

  //opening send pipe
  int pipe_send=-1;
  if((pipe_send=open(pipe_name,O_WRONLY))<0){
    cerr<< "open send failed: "<< strerror(errno)<<endl;
    return 1;
  }

  cout<<"writing\n";

  DIR *d=NULL;
  char tmp[100];
  struct dirent *dir;
  sprintf(tmp,"%s%s",input_dir,sub_dir);
  if((d= opendir(tmp))==NULL){
    cerr<< "opendir send failed: "<< strerror(errno)<<endl;
    //return 1;
    exit(1);
  }
  //for all the files i need to send
  while ((dir = readdir(d)) != NULL){
    if(strcmp(dir->d_name,".")!=0 && strcmp(dir->d_name,"..")!=0){
      sprintf(tmp, "%s%s%s",input_dir,sub_dir,dir->d_name);
      if(is_file(tmp)){// if file
        char buffer[9999];// TODO check size
        cout<<"for "<<tmp << '\n';

        //write name
        sprintf(tmp, "%s%s",sub_dir,dir->d_name);
        int sz=strlen(tmp);
        sprintf(buffer, "%d",sz);
        if(write(pipe_send, buffer, 2)<0){
          cerr<< "write send faileda: "<< strerror(errno)<<endl;
          return 1;
        }
        write_to_logfile(1, 2, logfile);

        if(write(pipe_send, tmp, sz)<0){
          cerr<< "write send failed: "<< strerror(errno)<<endl;
          return 1;
        }
        write_to_logfile(1, sz, logfile);;

        //write file
        sprintf(tmp, "%s%s%s",input_dir,sub_dir,dir->d_name);
        sz=file_sz(tmp);
        sprintf(buffer, "%d",sz);
        if(write(pipe_send, buffer, 4)<0){
          cerr<< "write send failed: "<< strerror(errno)<<endl;
          return 1;
        }
        write_to_logfile(1, 4, logfile);
        char* tmpp= read_hole_file(tmp);
        if(write(pipe_send, tmpp, sz)<0){
          cerr<< "write send failed: "<< strerror(errno)<<endl;
          return 1;
        }
        write_to_logfile(1, sz, logfile);

        write_to_logfile(3, 0, logfile);
      }
      else{// if directory
        sprintf(tmp, "%s%s/",sub_dir,dir->d_name);
        cout<<"recursive "<<tmp<<endl;
        send_process(pipe_name, id, tmp, input_dir,logfile);
      }
    }
  }
  if(strcmp(sub_dir,"")==0){
    if(write(pipe_send, "00", 2)<0){
      cerr<< "write send failed: "<< strerror(errno)<<endl;
      return 1;
    }
    cout<<"TELIOSA kiego\n";
    write_to_logfile(1, 2, logfile);
  }

  closedir(d);
  close(pipe_send);

  return 0;
}

//handles all the delete part
int delete_process(char* mirror_dir, char* dot_id_file){
  char ttt[100];
  struct stat tmp_stat;

  //remove ".id" from string
  strcpy(ttt, dot_id_file);
  ttt[strlen(ttt)-3]='\0';

  //find mirror/id
  char deleted_usr_dir[100];
  sprintf(deleted_usr_dir,"%s/%s",mirror_dir,ttt);
  cout<<"diagrafo "<<deleted_usr_dir<<endl;

  //deleting mirror/id
  char command[50];
  sprintf(command,"rm -r %s",deleted_usr_dir);
  if(stat(deleted_usr_dir, &tmp_stat) == 0)
    if(system(command)!=0){
      cerr<< "deleting system failed: "<< strerror(errno)<<endl;
      return 1;
    }

  return 0;
}

//signal handler for SIGUSR1
void usr1_signal_handler(int sig){
  cout<<"child failed\n";
  fflush(stdout);
}

//signal handler for SIGKILL and SIGINT
void kill_signal_handler(int sig){
  struct stat tmp_stat;
  char command[50];

  //TODO kill children

  write(handler_logfile_fd, "exiting\n", 8);
  close(handler_logfile_fd);

  //deleting mirror
  sprintf(command,"rm -r %s",handler_mirror_dir);
  if(stat(handler_mirror_dir, &tmp_stat) == 0)
    if(system(command)!=0){
      cerr<< "system1 failed: "<< strerror(errno)<<endl;
      exit(1);
    }

  //deleting common/.id
  sprintf(command,"rm -r %s/%d.id",handler_common_dir,handler_id);
  if(stat(handler_common_dir, &tmp_stat) == 0)
    if(system(command)!=0){
      cerr<< "system2 failed: "<< strerror(errno)<<endl;
      exit(1);
    }

  cout<<"exiting\n";
}
