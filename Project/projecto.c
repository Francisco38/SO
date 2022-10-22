/*
Francisco António Da Cruz Faria 2019227649
Iago Silva Bebiano 2019219478
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/fcntl.h>
#include <sys/msg.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#define LOGFILE "log.txt"

//modos da box
#define FREE 0
#define OCCUPIED 1
#define RESERVED 2

//modos do carro
#define RACE 0
#define SECURITY 1
#define BOX 2
#define SURRENDER 3
#define FINISH 4


typedef struct {
    char name;
    int num_car;
    int speed;
    int malfunction;
    int need_fuel;
    float comsumption;
    int reliabity;
    int state;
    int pos;
    float fuel;
    int turns;
    int stops;
} car;

typedef struct{
    char name;
    int box;
    int n_carros;
    car *cars;
}team;

typedef struct{
  int car;
  char news[20];
}pipe_msg;

typedef struct {
    long priority;
} msg;

typedef struct {
	pthread_mutex_t mutex_start;
	pthread_cond_t cond_start;
} cond_struct;

cond_struct* shared_rec;

//named pipe vars
int fd;
fd_set read_set;

//message queue
int mqid;

//memoria partilhada
key_t shmkey;
int shmid;

//variaveis no ficheiro de texto
int tempo_por_seg,turn_dist,n_turns,n_teams,n_cars_max,t_problem,t_box_min,t_box_max,max_fuel;

//semaforos
sem_t *go_log,go_box,mutex;

//variaveis em memoria partilhada
int *ready,*total_problems,*total_refuel,*running_cars,*received_ctrl,*car_lug;
team *team_memory;

//variaveis usadas para cada equipa
team current_team;
int team_pipe[2];
int car_id=-1;
pthread_t *cars;

//pids para usar no controlo de sinais
pid_t main_pid;
pid_t race_pid;
pid_t malf_pid;

void esta();
void termination_handler(int signum);
void box_treatment();

void *car_work(void* c){
  int my_id=*((int *)c);


  //espera pelo inicio da corrida
  pthread_mutex_lock(&shared_rec->mutex_start);
  while(*ready==0){
    pthread_cond_wait(&shared_rec->cond_start,&shared_rec->mutex_start);
  }
  pthread_mutex_unlock(&shared_rec->mutex_start);

  pipe_msg env_msg;
  msg my_msg;

  //reinicializa todas as variaveis necessarias
  env_msg.car=current_team.cars[my_id].num_car;
  current_team.cars[my_id].pos=0;
  current_team.cars[my_id].fuel=max_fuel;
  current_team.cars[my_id].turns=0;
  current_team.cars[my_id].stops=0;
  current_team.cars[my_id].malfunction=0;
  current_team.cars[my_id].need_fuel=0;
  current_team.cars[my_id].state=RACE;
  int current_speed=current_team.cars[my_id].speed;
  float current_consumption=current_team.cars[my_id].comsumption;
  int want_in;
  int i=0;

  while(current_speed!=0){
    if(msgrcv(mqid, &my_msg, sizeof(msg),current_team.cars[my_id].num_car+1,IPC_NOWAIT)!=-1){
      current_team.cars[my_id].malfunction=1;
      current_team.cars[my_id].state=SECURITY;
      want_in=1;
      env_msg.car=current_team.cars[my_id].num_car;
      snprintf(env_msg.news,20, "NEW PROBLEM");
      if(write(team_pipe[1],&env_msg,sizeof(env_msg))==-1){
        perror("ERRO A ESCREVER NO PIPE");
      }
    }

    current_team.cars[my_id].pos+=current_speed;
    if(current_team.cars[my_id].pos >= turn_dist){
      current_team.cars[my_id].turns=current_team.cars[my_id].turns+ current_team.cars[my_id].pos / turn_dist;
      current_team.cars[my_id].pos= current_team.cars[my_id].pos % turn_dist;
      if(current_team.cars[my_id].turns>=n_turns){
        current_team.cars[my_id].state=FINISH;
        snprintf(env_msg.news,20, "FINISHED");
        if(write(team_pipe[1],&env_msg,sizeof(env_msg))==-1){
          perror("ERRO A ESCREVER NO PIPE");
        }
        break;
      }
      if(*received_ctrl==1){
        current_team.cars[my_id].state=FINISH;
        snprintf(env_msg.news,20, "FINISHED");
        if(write(team_pipe[1],&env_msg,sizeof(env_msg))==-1){
          perror("ERRO A ESCREVER NO PIPE");
        }
        pthread_exit(0);
      }
      if(want_in==1){
        //tenta entrar na box sem espera ativa
        if(sem_trywait(&mutex)==0){
          if(current_team.box==RESERVED && current_team.cars[my_id].state!=RACE){
            sem_post(&mutex);
          }
          else{
            snprintf(env_msg.news,20, "GETING FIXED");
            if(write(team_pipe[1],&env_msg,sizeof(env_msg))==-1){
              perror("ERRO A ESCREVER NO PIPE");
            }
            current_team.cars[my_id].stops++;
            current_team.box=OCCUPIED;
            current_team.cars[my_id].state=BOX;
            car_id=my_id;
            sem_post(&go_box);
            sem_wait(&mutex);
            want_in=0;
            i=0;
            car_id=-1;
            sem_post(&mutex);
          }
        }
      }
    }

    current_team.cars[my_id].fuel=current_team.cars[my_id].fuel-current_consumption;
    if(current_team.cars[my_id].fuel<=0){
      current_team.cars[my_id].state=SURRENDER;
      snprintf(env_msg.news,20, "RAN OUT OF FUEL");
      if(write(team_pipe[1],&env_msg,sizeof(env_msg))==-1){
        perror("ERRO A ESCREVER NO PIPE");
      }
      if(*received_ctrl==1){
        pthread_exit(0);
      }
      break;
    }
    if(i==0){
      if(current_team.cars[my_id].fuel/current_team.cars[my_id].comsumption<((turn_dist * 4) / current_team.cars[my_id].speed)){
        current_team.cars[my_id].need_fuel=1;
        want_in=1;
        if(current_team.cars[my_id].fuel/current_team.cars[my_id].comsumption<((turn_dist * 2) / current_team.cars[my_id].speed)){
          current_team.cars[my_id].state=SECURITY;
          i=1;
          snprintf(env_msg.news,20, "LOW FUEL");
          if(write(team_pipe[1],&env_msg,sizeof(env_msg))==-1){
            perror("ERRO A ESCREVER NO PIPE");
          }
        }
      }
    }

    if(current_team.cars[my_id].state==RACE){
      current_speed=current_team.cars[my_id].speed;
      current_consumption=current_team.cars[my_id].comsumption;
    }
    if(current_team.cars[my_id].state==SECURITY){
      current_speed=0.3*current_team.cars[my_id].speed;
      current_consumption=0.4*current_team.cars[my_id].comsumption;
    }
    sleep(1/tempo_por_seg);
  }

    //espera que a corrida acabe
    pthread_mutex_lock(&shared_rec->mutex_start);
    while(*ready==1){
      pthread_cond_wait(&shared_rec->cond_start,&shared_rec->mutex_start);
    }
    pthread_mutex_unlock(&shared_rec->mutex_start);

    if(*received_ctrl==1){
      pthread_exit(0);
    }

    car_work(c);
    pthread_exit(0);
}

void inilog(char dados[]){
    sem_wait(go_log);
    FILE* fichlog=fopen(LOGFILE,"a");
    if(fichlog==NULL){
        perror("Failed to open file");
        exit(1);
    }

    printf("%s\n",dados);

    char t[26];
    time_t timer;
    struct tm* time_info;

    time(&timer);
    time_info=localtime(&timer);

    strftime(t,26,"%H:%M:%S ",time_info);

    fprintf(fichlog,t,256);
    fprintf(fichlog,"%s\n",dados);

    sem_post(go_log);

    fclose(fichlog);
}

void gestor_de_equipa(int pipe[2],int t){
    team_pipe[0]=pipe[0];
    team_pipe[1]=pipe[1];
    int n;
    int id[n_cars_max];
    current_team=team_memory[t];
    cars=(pthread_t*)malloc(sizeof(pthread_t)*(n_cars_max));

    if(sem_init(&go_box, 0, 0)==-1){
      perror("ERROR CREATING SEMAPHORE");
      exit(0);
    }
    if(sem_init(&mutex, 0, 1)==-1){
      perror("ERROR CREATING SEMAPHORE");
      exit(0);
    }

    for(int i=0; i < n_cars_max; i++){
        id[i]=i;
        n=pthread_create(&cars[i],NULL,car_work,&id[i]);
        if(n!=0){
            printf("ERROR CREATING THREAD!");
            exit(0);
        }
    }

    pthread_mutex_lock(&shared_rec->mutex_start);
    while(*ready==0){
      pthread_cond_wait(&shared_rec->cond_start,&shared_rec->mutex_start);
    }
    pthread_mutex_unlock(&shared_rec->mutex_start);

    pipe_msg env_msg;
    while(1){
      pthread_mutex_lock(&shared_rec->mutex_start);
      while(*ready==0){
        pthread_cond_wait(&shared_rec->cond_start,&shared_rec->mutex_start);
      }
      pthread_mutex_unlock(&shared_rec->mutex_start);

      while(*running_cars != 0){
        if(current_team.box==FREE){
          for(int i=0;i<current_team.n_carros;i++){
            if(current_team.cars[i].state==SECURITY){
              current_team.box=RESERVED;
              break;
            }
          }
        }
        if(current_team.box==RESERVED){
          for(int i=0;i<current_team.n_carros;i++){
            if(current_team.cars[i].state==SECURITY){
              current_team.box=RESERVED;
              break;
            }
          }
        }
        if(sem_trywait(&go_box)==0){
          env_msg.car= current_team.cars[car_id].num_car;
          if(current_team.cars[car_id].malfunction==1){
            int random_num=(rand() % t_box_max)+t_box_min;
            sleep(random_num);
            snprintf(env_msg.news,20, "FIXED");
            if(write(team_pipe[1],&env_msg,sizeof(pipe_msg))==-1){
              perror("ERRO A ESCREVER NO PIPE");
            }
            *total_problems=*total_problems+1;
            current_team.cars[car_id].malfunction=0;
          }
          if(current_team.cars[car_id].need_fuel==1){
            sleep(2);
            current_team.cars[car_id].fuel=max_fuel;
            current_team.cars[car_id].need_fuel=0;
            snprintf(env_msg.news,20, "FUEL REFILLED");
            if(write(team_pipe[1],&env_msg,sizeof(pipe_msg))==-1){
              perror("ERRO A ESCREVER NO PIPE");
            }
          }
          current_team.cars[car_id].state=RACE;
          sem_post(&mutex);
        }
        sleep(1/tempo_por_seg);
      }

      pthread_mutex_lock(&shared_rec->mutex_start);
      while(*ready==1){
        pthread_cond_wait(&shared_rec->cond_start,&shared_rec->mutex_start);
      }
      pthread_mutex_unlock(&shared_rec->mutex_start);
    }

    for (int i = 0; i < n_cars_max; i++) {
        pthread_join(cars[i], NULL);
    }
}

void gestor_de_corrida(){
    signal(SIGUSR1,termination_handler);
    pid_t childpid;
    int pipes [n_teams][2];
    for(int i=0; i < n_teams; i++){
        pipe(pipes[i]);
        childpid = fork();
        if (childpid == 0) {
            gestor_de_equipa(pipes[i],i);
            exit(0);
        }
    }

    int max=0;
    char input[100];
    char log[150];
    int count=0;
    int car_n=0;
    int n;
    int n_lugar=0;
    int n_final=0;
    while (1) {
        FD_ZERO(&read_set);
        for(int i=0; i < n_teams; i++){
            FD_SET(pipes[i][0],&read_set);
        }
        FD_SET(fd,&read_set);
        if(fd>pipes[n_teams - 1][0]){
            max=fd;
        }
        else{
            max=pipes[n_teams - 1][0];
        }

        strcpy(input,"");
        if(select(max+1, &read_set, NULL, NULL, NULL) > 0){
            //le dos unamed pipes
            for(int i=0; i < n_teams; i++){
                if(FD_ISSET(pipes[i][0], &read_set)){
                  pipe_msg msg;
                  char output[50];
                  read(pipes[i][0], &msg, sizeof(msg));
                  if(strcmp(msg.news,"FINISHED")==0){
                    *running_cars= *running_cars - 1;
                    car_lug[n_lugar]=msg.car;
                    n_lugar++;
                  }
                  else if(strcmp(msg.news,"RAN OUT OF FUEL")==0){
                    *running_cars= *running_cars - 1;
                    car_lug[n_final]=msg.car;
                    n_final=n_final-1;
                    snprintf(output,50, "CAR %d %s",msg.car,msg.news);
                    inilog(output);
                  }
                  else if(strcmp(msg.news,"NEW PROBLEM")==0){
                    *total_problems=*total_problems+1;
                    snprintf(output,50, "%s IN CAR %d",msg.news,msg.car);
                    inilog(output);
                  }
                  else if(strcmp(msg.news,"FUEL REFILLED")==0){
                    *total_refuel=*total_refuel+1;
                    snprintf(output,50, "CAR %d %s",msg.car,msg.news);
                    inilog(output);
                  }
                  else{
                    snprintf(output,50, "Car %d %s",msg.car,msg.news);
                    inilog(output);
                  }
                  if(*running_cars==0){
                    *ready=0;
                    n_lugar=0;
                    pthread_cond_broadcast(&shared_rec->cond_start);
                    if(*received_ctrl==1){
                      for(int x=0;x<n_teams;x++){
                        wait(NULL);
                      }
                      exit(0);
                    }
                    else{
                      inilog("RACE FINISHED");
                      snprintf(output,50, "CAR %d WON",car_lug[0]);
                      inilog(output);
                    }
                  }
                }
            }
            //le do named pipe
            if(FD_ISSET(fd, &read_set)){
                n=read(fd, &input, sizeof(char)*100);
                input[n-1]='\0';
                if(strncmp(input,"START RACE!",11)==0){
                    inilog("NEW COMMAND RECEIVED:START RACE");
                    if(*ready==1){
                      inilog("RACE ALREADY STARTED!");
                    }
                    else{
                      for(int i=0; i < n_teams; i++){
                          if(team_memory[i].n_carros!=0){
                              count++;
                          }
                      }
                      if(count<3){
                          inilog("CANNOT START NOT ENOUTH TEAMS!");
                      }
                      else{
                          *ready=1;
                          n_final=car_n-1;
                          *running_cars=car_n;
                          inilog("STARTING RACE!");
                          pthread_cond_broadcast(&shared_rec->cond_start);
                      }
                    }
                }
                else{
                    int n_val=0;
                    int n_team=-1;
                    int fail=0;
                    car teste_car;

                    n_val=sscanf(input,"ADDCAR TEAM: %c, CAR: %d, SPEED: %d, CONSUMPTION: %f, RELIABILITY: %d",&teste_car.name,&teste_car.num_car,&teste_car.speed,&teste_car.comsumption,&teste_car.reliabity);
                    if(n_val==5){
                        if(*ready==0){
                            for(int i =0; i < n_teams; i++){
                                if(team_memory[i].n_carros!=0){
                                    for(int c=0;c<team_memory[i].n_carros;c++){
                                        if(team_memory[i].cars[c].num_car==teste_car.num_car){
                                            fail=1;
                                            break;
                                        }
                                    }
                                    if(fail==1){
                                        break;
                                    }
                                    if(team_memory[i].name==teste_car.name){
                                        if(team_memory[i].n_carros >= n_cars_max){
                                            fail=1;
                                            break;
                                        }
                                        else{
                                            n_team=i;
                                        }
                                    }
                                }
                            }
                            if(teste_car.speed<0){
                                fail=1;
                            }
                            if(teste_car.comsumption<0){
                                fail=1;
                            }
                            if(teste_car.reliabity>100 && teste_car.reliabity<0){
                                fail=1;
                            }
                            if(fail==0){
                                car_n++;
                                *running_cars= *running_cars + 1;
                                if(n_team==-1){
                                    for(int i=0; i < n_teams; i++){
                                        if(team_memory[i].n_carros==0){
                                            team_memory[i].name=teste_car.name;
                                            team_memory[i].box=FREE;
                                            team_memory[i].n_carros=1;
                                            team_memory[i].cars[0].num_car=teste_car.num_car;
                                            team_memory[i].cars[0].speed=teste_car.speed;
                                            team_memory[i].cars[0].malfunction=0;
                                            team_memory[i].cars[0].comsumption=teste_car.comsumption;
                                            team_memory[i].cars[0].reliabity=teste_car.reliabity;
                                            team_memory[i].cars[0].state=BOX;
                                            team_memory[i].cars[0].fuel=max_fuel;
                                            team_memory[i].cars[0].name=teste_car.name;

                                            break;
                                        }
                                    }
                                }
                                else{
                                    team_memory[n_team].cars[team_memory[n_team].n_carros].name=teste_car.name;
                                    team_memory[n_team].cars[team_memory[n_team].n_carros].num_car=teste_car.num_car;
                                    team_memory[n_team].cars[team_memory[n_team].n_carros].speed=teste_car.speed;
                                    team_memory[n_team].cars[team_memory[n_team].n_carros].malfunction=0;
                                    team_memory[n_team].cars[team_memory[n_team].n_carros].comsumption=teste_car.comsumption;
                                    team_memory[n_team].cars[team_memory[n_team].n_carros].reliabity=teste_car.reliabity;
                                    team_memory[n_team].cars[team_memory[n_team].n_carros].state=BOX;
                                    team_memory[n_team].cars[team_memory[n_team].n_carros].fuel=max_fuel;
                                    team_memory[n_team].n_carros++;
                                }
                                strcpy(log,"NEW CAR LOADED =>");
                                strcat(log, input);
                                inilog(log);
                            }
                            else{
                              strcpy(log,"WRONG COMMAND => ");
                              strcat(log, input);
                              inilog(log);
                            }
                        }
                        else{
                            inilog("RACE ALREADY STARTED!");
                        }
                    }
                    else{
                        strcpy(log,"WRONG COMMAND => ");
                        strcat(log, input);
                        inilog(log);
                    }
                }
            }
        }
    }

}

void gestor_de_avarias(){
    signal(SIGUSR1,termination_handler);

    pthread_mutex_lock(&shared_rec->mutex_start);
    while(*ready==0){
      pthread_cond_wait(&shared_rec->cond_start,&shared_rec->mutex_start);
    }
    pthread_mutex_unlock(&shared_rec->mutex_start);

    msg my_msg;
    while(*running_cars != 0){
        for(int i=0; i < n_teams; i++){
            if(team_memory[i].n_carros!=0){
                for(int x=0;x<team_memory[i].n_carros;x++){
                  if(team_memory[i].cars[x].malfunction==0){
                    int random_num=(rand() % 100)+1;
                    if(random_num>team_memory[i].cars[x].reliabity){
                        my_msg.priority=team_memory[i].cars[x].num_car+1;
                        msgsnd(mqid,&my_msg,sizeof(msg),0);
                    }
                  }
                }
            }
        }
        sleep(t_problem);
    }
    if(*received_ctrl==1){
      exit(0);
    }
    else{
      sleep(1);
      gestor_de_avarias();
    }
}

void readFile(char *filename){

    FILE *textfp;
    char * line = NULL;
    size_t len = 0;

    int i= 0;
    char *token;
    int input[9];

    int read;

    if ((textfp=fopen(filename,"r"))==NULL) {
        perror("Failed to open file\n");
        exit(1);
    }


    while((read=getline(&line, &len, textfp)) !=-1) {

        token = strtok(line,",\n");
        while (token) {

            input[i]=atoi(token);
            if(input[i]<=0){
                printf("Wrong element!\n");
                exit(1);
            }
            i=i+1;
            token = strtok(NULL,",\n");
        }
    }

    if (i!=9){
        printf("Wrong number of elements!\n");
        exit(1);
    }


    if (input[3]<3){
        printf("Wrong element! Number of teams has to be at least 3\n");
        exit(1);
    }

    if(input[6]>input[7]){
        printf("Wrong element! Tempo minimo de reparacao maior que o tempo maximo\n");
        exit(1);
    }

    tempo_por_seg=input[0];
    turn_dist=input[1];
    n_turns=input[2];
    n_teams=input[3];
    n_cars_max=input[4];
    t_problem=input[5];
    t_box_min=input[6];
    t_box_max=input[7];
    max_fuel=input[8];
}

void termination_handler(int signum) {
    if(signum == SIGINT){
        inilog("SIGNAL SIGINT RECEIVED");
        *received_ctrl=1;
        kill(0,SIGUSR1);
        wait(NULL);
        wait(NULL);
        esta();
        msgctl(mqid,IPC_RMID,0);
        unlink("named_pipe");
        sem_close(go_log);
        sem_unlink("GO_LOG");
        shmctl(shmid,IPC_RMID,NULL);
        exit(0);
    }
    if(signum == SIGTSTP){
        inilog("SIGNAL SIGTSTP RECEIVED");
        esta();
    }
    if(signum==SIGUSR1){
      pid_t teste=getpid();
      if(race_pid==teste){
        if(*ready==0){
          for(int x=0;x<n_teams;x++){
            wait(NULL);
          }
          exit(0);
        }
      }
     else if(teste==malf_pid){
        if(*ready==0){
          exit(0);
        }
      }
      else{
        if(*ready==1){
          if(car_id!=-1){
            pthread_cancel(cars[car_id]);
            *running_cars= *running_cars - 1;
            pipe_msg env_msg;
            env_msg.car=current_team.cars[car_id].num_car;
            snprintf(env_msg.news,20, "FINISHED");
            if(write(team_pipe[1],&env_msg,sizeof(env_msg))==-1){
              perror("ERRO A ESCREVER NO PIPE");
            }
          }
          pthread_cond_broadcast(&shared_rec->cond_start);
          for(int i=0;i<n_cars_max;i++){
            pthread_join(cars[i], NULL);
          }
        }
        else{
          for(int i=0;i<n_cars_max;i++){
            pthread_cancel(cars[i]);
          }
        }
        sem_destroy(&go_box);
        sem_destroy(&mutex);
        close(team_pipe[0]);
        close(team_pipe[1]);
        exit(0);
      }
    }
}

void esta(){
  int check=0;
  if(*ready==0){
    for(int i=0;i<5;i++){
      for(int m=0;m<n_teams;m++){
        if(team_memory[i].n_carros!=0){
          for(int n=0;n<team_memory[i].n_carros;n++){
            if(car_lug[i]==team_memory[m].cars[n].num_car){
              printf("%dº car:%d team:%c laps:%d stops:%d\n",i+1,team_memory[m].cars[n].num_car,team_memory[m].cars[n].name,team_memory[m].cars[n].turns,team_memory[m].cars[n].stops);
            }
          }
        }
      }
    }
    int i=n_teams-1;
    for(int m=0;m<n_teams;m++){
      if(team_memory[i].n_carros!=0){
        for(int n=0;n<n_cars_max;n++){
          if(car_lug[i]==team_memory[i].cars[n].num_car){
            printf("%dº car:%d team:%c laps:%d stops:%d\n",i+1,team_memory[i].cars[n].num_car,team_memory[i].cars[n].name,team_memory[i].cars[n].turns,team_memory[i].cars[n].stops);
          }
        }
      }
    }
  }
  else {
    int n_voltas=0;
    int pos=0;
    int a[6]={};
    for(int i=0;i<5;i++){
      for(int m=0;m<n_teams;m++){
          for(int n=0;n<team_memory[m].n_carros;n++){
            if(i==0){
                check=0;
            }
            else{
              for(int k=0;k<i;k++){
                if(team_memory[m].cars[n].num_car==a[k]){
                  check=1;
                  break;
                }
              }
            }
            if(check==0){
              if(team_memory[m].cars[n].turns>n_voltas){
                a[i]=team_memory[m].cars[n].num_car;
                n_voltas=team_memory[m].cars[n].turns;
                pos=team_memory[m].cars[n].pos;
              }
              else if(team_memory[m].cars[n].turns==n_voltas){
                if(team_memory[m].cars[n].turns>=pos){
                  a[i]=team_memory[m].cars[n].num_car;
                  n_voltas=team_memory[m].cars[n].turns;
                  pos=team_memory[m].cars[n].pos;
                }
              }
            }
            check=0;
          }
        }
      n_voltas=0;
      pos=0;
    }

    for(int m=0;m<n_teams;m++){
      if(team_memory[m].n_carros!=0){
        for(int n=0;n<team_memory[m].n_carros;n++){
          for(int k=0;k<5;k++){
            if(team_memory[m].cars[n].num_car==a[k]){
              check=1;
              break;
            }
          }
            if(check==0){
              if(team_memory[m].cars[n].turns<n_voltas){
                a[5]=team_memory[m].cars[n].num_car;
                n_voltas=team_memory[m].cars[n].turns;
                pos=team_memory[m].cars[n].pos;
              }
              else if(team_memory[m].cars[n].turns==n_voltas){
                if(team_memory[m].cars[n].turns<=pos){
                  a[5]=team_memory[m].cars[n].num_car;
                  n_voltas=team_memory[m].cars[n].turns;
                  pos=team_memory[m].cars[n].pos;
                }
              }
            }
          }
        }
      }

    for(int i=0;i<6;i++){
      for(int m=0;m<n_teams;m++){
        if(team_memory[m].n_carros!=0){
          for(int n=0;n<team_memory[m].n_carros;n++){
            if(a[i]==team_memory[m].cars[n].num_car){
              printf("%dº car:%d team:%c laps:%d stops:%d\n",i+1,team_memory[m].cars[n].num_car,team_memory[m].cars[n].name,team_memory[m].cars[n].turns,team_memory[m].cars[n].stops);
            }
          }
        }
      }
    }
  }
  printf("total avarias=%d\n",*total_problems);
  printf("total abastecimentos=%d\n",*total_refuel);
  printf("n carros pista=%d\n",*running_cars);
}

int main(int argc, char *argv[]) {
    main_pid=getpid();

    char *filename;

    signal(SIGTSTP,SIG_IGN);
    signal(SIGINT,SIG_IGN);
    signal(SIGUSR1,SIG_IGN);


    sem_unlink("GO_LOG");
    go_log=sem_open("GO_LOG",O_CREAT|O_EXCL,0700,1);
    if(go_log==SEM_FAILED){
        perror("Failure creating the semaphore MUTEX");
        exit(1);
    }

    if (argc!=2) {
        perror("Wrong number of arguments. You must give the file to decrypt.\n");
        exit(1);
    }
    filename = argv[1];

    readFile(filename);

    if( (shmkey = ftok(".", getpid())) == (key_t) -1){
        perror("IPC error: ftok");
        exit(1);
    }

    shmid = shmget(shmkey, sizeof(cond_struct)+((sizeof(car) * n_cars_max) + sizeof(team)) * n_teams + (sizeof(int) * 5)+(n_teams*n_cars_max)*sizeof(int), IPC_CREAT | IPC_EXCL | 0700);
    if (shmid < 1){
        perror("Error creating shm memory!\n");
        exit(1);
    }

    ready=(int*)shmat(shmid,NULL,0);
    if (ready < (int*) 1){
        perror("Error attaching memory!\n");
        exit(1);
    }
    *ready=0;

    total_problems=(int*)(ready + 1);
    *total_problems=0;

    shared_rec = (cond_struct *)(total_problems+1);

    pthread_mutexattr_t attrmutex;
    pthread_condattr_t attrcondv;

    pthread_mutexattr_init(&attrmutex);
    pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED);

    pthread_condattr_init(&attrcondv);
    pthread_condattr_setpshared(&attrcondv, PTHREAD_PROCESS_SHARED);

    /* Initialize mutex. */
    pthread_mutex_init(&shared_rec->mutex_start, &attrmutex);

    /* Initialize condition variables. */
    pthread_cond_init(&shared_rec->cond_start, &attrcondv);

    total_refuel=(int*)(shared_rec + 1);
    *total_refuel=0;

    running_cars=(int*)(total_refuel + 1);
    *running_cars=0;

    received_ctrl=(int*)(running_cars + 1);
    *received_ctrl=0;

    team_memory = (team*) (received_ctrl + 1);
    team_memory[0].cars= (car*)team_memory + n_teams;
    for(int i=1; i < n_teams; i++){
      team_memory[i].cars=(car*)(team_memory[i-1].cars + n_cars_max);
    }

    car_lug=(int*)(team_memory[n_teams-1].cars + n_cars_max);


    if ((mkfifo("named_pipe", O_CREAT|O_EXCL|0600)<0) && (errno!= EEXIST)){
        inilog("Cannot create pie: ");
        exit(0);
    }

    if ((fd=open("named_pipe", O_RDWR)) < 0){
        inilog("Error reading pipe in central process");
    }

    mqid=msgget(IPC_PRIVATE,IPC_CREAT|0777);
    if(mqid<0){
        perror("Creating message queue");
        exit(0);
    }

    remove(LOGFILE);
    inilog("SIMULATOR STARTING");


    race_pid = fork();
    if (race_pid == 0) {
        race_pid =getpid();
        gestor_de_corrida();
        exit(0);
    }

    malf_pid = fork();
    if (malf_pid == 0) {
        malf_pid =getpid();
        gestor_de_avarias();
        exit(0);
    }

    signal(SIGTSTP,termination_handler);
    signal(SIGINT,termination_handler);
    wait(NULL);
    wait(NULL);

}
