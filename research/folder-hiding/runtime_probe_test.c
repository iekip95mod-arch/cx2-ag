#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define main runtime_probe_main
#include "runtime_probe.c"
#undef main
#undef stat
#undef mkdir
#undef fopen
#undef rename
static char suite[] = "/tmp/cx2-runtime-probes-XXXXXX", root[PATH_MAX];
static unsigned cases, calls, failure, rollback_failure, refreshed;
static int startup;
static void mapped(const char *path, char *full) {
    assert((!strncmp(path,"/documents",10) && (!path[10] || path[10]=='/')) ||
           (!strncmp(path,"/appdata",8) && (!path[8] || path[8]=='/')));
    assert(!strstr(path,"/.."));
    int n=snprintf(full,PATH_MAX,"%s%s",root,path);
    assert(n>0 && n<PATH_MAX);
}
static int fixture_stat(const char *path,struct stat *st) { char full[PATH_MAX]; mapped(path,full); return stat(full,st); }
static int fixture_mkdir(const char *path,mode_t mode) { char full[PATH_MAX]; mapped(path,full); return mkdir(full,mode); }
static FILE *fixture_fopen(const char *path,const char *mode) { char full[PATH_MAX]; mapped(path,full); return fopen(full,mode); }
static int fixture_rename(const char *source,const char *destination) {
    char from[PATH_MAX],to[PATH_MAX]; mapped(source,from); mapped(destination,to);
    if (++calls==failure || calls==rollback_failure) { errno=EIO; return -1; }
    struct stat st; assert(lstat(to,&st)<0 && errno==ENOENT);
    return rename(from,to);
}
static int nl_isstartup(void) { return startup; }
static void refresh_osscr(void) { ++refreshed; }
static void directories(const char *path) {
    char full[PATH_MAX]; mapped(path,full);
    for(char *p=full+strlen(root)+1;*p;++p) if(*p=='/') {
        *p=0; assert(mkdir(full,0700)==0 || errno==EEXIST); *p='/';
    }
    assert(mkdir(full,0700)==0 || errno==EEXIST);
}
static void put(const char *path,const char *bytes) {
    char parent[PATH_MAX],full[PATH_MAX]; strcpy(parent,path); *strrchr(parent,'/')=0;
    directories(parent); mapped(path,full); FILE *f=fopen(full,"wx"); assert(f);
    assert(fwrite(bytes,1,strlen(bytes),f)==strlen(bytes)); assert(!fclose(f));
}
static void expect(const char *path,const char *bytes) {
    char actual[128]; FILE *f=fixture_fopen(path,"rb"); assert(f);
    size_t n=fread(actual,1,sizeof actual,f); assert(feof(f) && !ferror(f));
    assert(n==strlen(bytes) && !memcmp(actual,bytes,n)); assert(!fclose(f));
}
static void absent(const char *path) { struct stat st; assert(fixture_stat(path,&st)<0 && errno==ENOENT); }
static void fresh(void) {
    int n=snprintf(root,sizeof root,"%s/case-%02u",suite,++cases); assert(n>0 && n<(int)sizeof root);
    assert(!mkdir(root,0700)); assert(!fixture_mkdir("/documents",0700));
    calls=failure=rollback_failure=refreshed=0; startup=0;
}
#if PROBE_MODE == 0
static const char *stage="/documents/calc_helpers.luax.tns", *live="/appdata/ndl/calc_helpers.luax.tns";
static const char *backup="/appdata/ndl/calc_helpers.backup0001.luax.tns";
static void bridge_fixture(void) { fresh(); put(stage,"new bridge"); put(live,"old bridge"); }
static void tests(void) {
    bridge_fixture(); startup=1; assert(runtime_probe_main()==1 && !calls && !refreshed);
    expect(stage,"new bridge"); expect(live,"old bridge"); absent("/documents/RuntimeProbeStatus.tns");
    for(unsigned missing=0;missing<2;++missing) {
        fresh(); put(missing?stage:live,missing?"new bridge":"old bridge");
        assert(runtime_probe_main()==1 && !calls);
    }
    for(unsigned invalid=0;invalid<4;++invalid) {
        fresh(); const char *bad=invalid<2?stage:live;
        put(invalid<2?live:stage,invalid<2?"old bridge":"new bridge");
        if(invalid%2) put(bad,""); else directories(bad);
        assert(runtime_probe_main()==1 && !calls);
    }
    bridge_fixture(); put(backup,"prior backup");
    directories("/appdata/ndl/calc_helpers.backup0002.luax.tns");
    assert(runtime_probe_main()==0 && calls==2 && refreshed==1);
    expect(live,"new bridge"); expect(backup,"prior backup");
    expect("/appdata/ndl/calc_helpers.backup0003.luax.tns","old bridge"); absent(stage);
    for(unsigned failed=1;failed<=2;++failed) {
        bridge_fixture(); failure=failed; assert(runtime_probe_main()==1 && calls==2*failed-1);
        expect(stage,"new bridge"); expect(live,"old bridge"); absent(backup);
    }
    bridge_fixture(); failure=2; rollback_failure=3; assert(runtime_probe_main()==1 && calls==3);
    expect(stage,"new bridge"); expect(backup,"old bridge"); absent(live);
}
#else
static void layout_fixture(const char *location) {
    char path[128]; fresh();
    snprintf(path,sizeof path,"%s/ndl_resources.tns",location); put(path,"resources");
    snprintf(path,sizeof path,"%s/persistent.tns",location); put(path,"persistence");
    snprintf(path,sizeof path,"%s/startup/keysvc.tns",location); put(path,"resident");
}
static void check_layout(const char *location) {
    char path[128]; snprintf(path,sizeof path,"%s/ndl_resources.tns",location); expect(path,"resources");
    snprintf(path,sizeof path,"%s/persistent.tns",location); expect(path,"persistence");
    snprintf(path,sizeof path,"%s/startup/keysvc.tns",location); expect(path,"resident");
}
static void tests(void) {
    layout_fixture("/appdata/ndl"); startup=1; assert(runtime_probe_main()==1 && !calls && !refreshed);
    check_layout("/appdata/ndl"); absent("/documents/RuntimeProbeStatus.tns");
    fresh(); assert(runtime_probe_main()==1 && !calls);
    layout_fixture("/appdata/ndl"); directories("/documents/ndl");
    assert(runtime_probe_main()==1 && !calls); check_layout("/appdata/ndl");
    fresh(); put("/documents/ndl","not a directory"); assert(runtime_probe_main()==1 && !calls);
    for(unsigned invalid=0;invalid<6;++invalid) {
        fresh(); const char *bad=invalid<3?"/documents/ndl/ndl_resources.tns":"/documents/ndl/persistent.tns";
        put(invalid<3?"/documents/ndl/persistent.tns":"/documents/ndl/ndl_resources.tns","valid");
        if(invalid%3==1) put(bad,""); else if(invalid%3==2) directories(bad);
        assert(runtime_probe_main()==1 && !calls);
    }
    layout_fixture("/documents/ndl");
    assert(runtime_probe_main()==0 && calls==1); check_layout("/appdata/ndl"); absent("/documents/ndl");
    assert(runtime_probe_main()==0 && calls==2); check_layout("/documents/ndl"); absent("/appdata/ndl");
    for(unsigned direction=0;direction<2;++direction) {
        const char *from=direction?"/documents/ndl":"/appdata/ndl";
        layout_fixture(from); failure=1; assert(runtime_probe_main()==1 && calls==1);
        check_layout(from); absent(direction?"/appdata/ndl":"/documents/ndl");
    }
}
#endif
int main(void) { assert(mkdtemp(suite)); tests(); printf("runtime mode%d: %u fixtures passed, retained %s\n",PROBE_MODE,cases,suite); return 0; }
