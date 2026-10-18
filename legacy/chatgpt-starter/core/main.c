#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <errno.h>

static volatile sig_atomic_t running = 1;
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static void on_signal(int sig) { (void)sig; running = 0; }

typedef struct { int id, x, y; const char *title; } Window;
static Window windows[] = {
    {1, 80, 95, "Files (demo)"},
    {2, 420, 180, "Terminal (demo)"}
};

static void respond(FILE *f, char *cmd) {
    int id, x, y;
    if (!strncmp(cmd,"LIST",4) && (cmd[4]=='\n'||cmd[4]=='\0')) {
        for (size_t i=0; i<sizeof(windows)/sizeof(windows[0]); i++)
            fprintf(f,"WINDOW %d %d %d %s\n",windows[i].id,windows[i].x,windows[i].y,windows[i].title);
        fputs("END\n",f);
    } else if (sscanf(cmd,"MOVE %d %d %d", &id,&x,&y)==3) {
        for(size_t i=0;i<sizeof(windows)/sizeof(windows[0]);i++)
            if(windows[i].id==id){ windows[i].x=x;windows[i].y=y;fputs("OK\n",f);fflush(f);return; }
        fputs("ERROR unknown-window\n",f);
    } else if(sscanf(cmd,"FOCUS %d",&id)==1) {
        int found=0;
        for(size_t i=0;i<sizeof(windows)/sizeof(windows[0]);i++) if(windows[i].id==id) found=1;
        fputs(found?"OK\n":"ERROR unknown-window\n",f);
    } else fputs("ERROR unknown-command\n",f);
    fflush(f);
}

int main(void) {
    signal(SIGINT,on_signal); signal(SIGTERM,on_signal);
    snprintf(socket_path,sizeof(socket_path),"/tmp/lordos-core-%lu.sock",(unsigned long)getuid());
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if(s<0){perror("socket");return 1;}
    struct sockaddr_un addr={0};addr.sun_family=AF_UNIX;
    snprintf(addr.sun_path,sizeof(addr.sun_path),"%s",socket_path);
    unlink(socket_path);
    if(bind(s,(struct sockaddr*)&addr,sizeof(addr))<0){perror("bind");close(s);return 1;}
    if(listen(s,8)<0){perror("listen");close(s);unlink(socket_path);return 1;}
    fprintf(stderr,"LordOS demo IPC: %s\n",socket_path);
    while(running){
        int c=accept(s,NULL,NULL);
        if(c<0){if(errno==EINTR)continue;perror("accept");break;}
        FILE *f=fdopen(c,"r+");
        if(f){char line[256];if(fgets(line,sizeof(line),f)) respond(f,line);fclose(f);}else close(c);
    }
    close(s);unlink(socket_path);return 0;
}
