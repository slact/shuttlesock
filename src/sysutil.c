#include <shuttlesock/build_config.h>
#ifdef SHUTTLESOCK_PTHREAD_SETNAME_STYLE_LINUX
#define _GNU_SOURCE
#endif
#include <pthread.h>
#ifdef _GNU_SOURCE
#undef _GNU_SOURCE
#endif

#include <unistd.h>

#ifndef _SC_NPROCESSORS_ONLN
#include <sys/param.h>
#endif
#if !defined(_SC_NPROCESSORS_ONLN) || defined(__APPLE__)
#include <sys/sysctl.h>
#endif


#ifdef SHUTTLESOCK_PTHREAD_SETNAME_INCLUDE_PTRHEAD_NP
#include <pthread_np.h>
#endif

#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <netinet/ip.h>
#include <shuttlesock/sysutil.h>
#include <shuttlesock/shared_slab.h>

shuso_system_sockopts_t shuso_system_sockopts[] = {

#if defined(SOL_SOCKET) && defined(SO_BROADCAST)
  { "SO_BROADCAST",     SOL_SOCKET, SO_BROADCAST,   SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "SO_BROADCAST", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_DEBUG)
  { "SO_DEBUG",         SOL_SOCKET, SO_DEBUG,       SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "SO_DEBUG", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_DONTROUTE)
  { "SO_DONTROUTE",     SOL_SOCKET, SO_DONTROUTE,   SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "SO_DONTROUTE", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_ERROR)
  { "SO_ERROR",         SOL_SOCKET, SO_ERROR,       SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "SO_ERROR", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_KEEPALIVE)
  { "SO_KEEPALIVE",     SOL_SOCKET, SO_KEEPALIVE,   SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "SO_KEEPALIVE", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_LINGER)
  { "SO_LINGER",        SOL_SOCKET, SO_LINGER,      SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_LINGER },
#else
  { "SO_LINGER", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_OOBINLINE)
  { "SO_OOBINLINE",     SOL_SOCKET, SO_OOBINLINE,   SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "SO_OOBINLINE", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_RCVBUF)
  { "SO_RCVBUF",        SOL_SOCKET, SO_RCVBUF,      SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "SO_RCVBUF", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_SNDBUF)
  { "SO_SNDBUF",        SOL_SOCKET, SO_SNDBUF,      SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "SO_SNDBUF", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_RCVLOWAT)
  { "SO_RCVLOWAT",      SOL_SOCKET, SO_RCVLOWAT,    SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "SO_RCVLOWAT", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_SNDLOWAT)
  { "SO_SNDLOWAT",      SOL_SOCKET, SO_SNDLOWAT,    SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "SO_SNDLOWAT", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_RCVTIMEO)
  { "SO_RCVTIMEO",      SOL_SOCKET, SO_RCVTIMEO,    SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_TIMEVAL },
#else
  { "SO_RCVTIMEO", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_SNDTIMEO)
  { "SO_SNDTIMEO",      SOL_SOCKET, SO_SNDTIMEO,    SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_TIMEVAL },
#else
  { "SO_SNDTIMEO", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_REUSEADDR)
  { "SO_REUSEADDR",     SOL_SOCKET, SO_REUSEADDR,   SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "SO_REUSEADDR", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_REUSEPORT)
  { "SO_REUSEPORT",     SOL_SOCKET, SO_REUSEPORT,   SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "SO_REUSEPORT", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_REUSEADDR)
  { "SO_REUSEADDR",     SOL_SOCKET, SO_REUSEADDR,   SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "SO_REUSEADDR", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_TYPE)
  { "SO_TYPE",          SOL_SOCKET, SO_TYPE,        SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "SO_TYPE", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(SOL_SOCKET) && defined(SO_USELOOPBACK)
  { "SO_USELOOPBACK",   SOL_SOCKET, SO_USELOOPBACK, SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "SO_USELOOPBACK", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_IP) && defined(IP_TOS)
  { "IP_TOS",           IPPROTO_IP, IP_TOS,         SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "IP_TOS", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_IP) && defined(IP_TTL)
  { "IP_TTL",           IPPROTO_IP, IP_TTL,         SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "IP_TTL", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_IPV6) && defined(IPV6_DONTFRAG)
  { "IPV6_DONTFRAG",    IPPROTO_IPV6,IPV6_DONTFRAG, SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "IPV6_DONTFRAG", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_IPV6) && defined(IPV6_UNICAST_HOPS)
  { "IPV6_UNICAST_HOPS",IPPROTO_IPV6,IPV6_UNICAST_HOPS,SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "IPV6_UNICAST_HOPS", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_IPV6) && defined(IPV6_V6ONLY)
  { "IPV6_V6ONLY",      IPPROTO_IPV6,IPV6_V6ONLY,   SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "IPV6_V6ONLY", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_TCP) && defined(TCP_MAXSEG)
  { "TCP_MAXSEG",       IPPROTO_TCP,TCP_MAXSEG,     SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "TCP_MAXSEG", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_TCP) && defined(TCP_NODELAY)
  { "TCP_NODELAY",      IPPROTO_TCP,TCP_NODELAY,    SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "TCP_NODELAY", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_SCTP) && defined(SCTP_AUTOCLOSE)
  { "SCTP_AUTOCLOSE",   IPPROTO_SCTP,SCTP_AUTOCLOSE,SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "SCTP_AUTOCLOSE", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_SCTP) && defined(SCTP_MAXBURST)
  { "SCTP_MAXBURST",    IPPROTO_SCTP,SCTP_MAXBURST, SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "SCTP_MAXBURST", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_SCTP) && defined(SCTP_MAXSEG)
  { "SCTP_MAXSEG",      IPPROTO_SCTP,SCTP_MAXSEG,   SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT },
#else
  { "SCTP_MAXSEG", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif

#if defined(IPPROTO_SCTP) && defined(SCTP_NODELAY)
  { "SCTP_NODELAY",     IPPROTO_SCTP,SCTP_NODELAY,  SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG },
#else
  { "SCTP_NODELAY", 0, 0, SHUSO_SYSTEM_SOCKOPT_MISSING},
#endif
  { NULL,               0,          0,              SHUSO_SYSTEM_SOCKOPT_MISSING }
};

shuso_system_setup_t shuso_system = {
  .initialized = false
};

int shuso_system_cores_online(void) {
  long cores;
#ifdef _SC_NPROCESSORS_ONLN
  cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (cores < 1 || cores > ((long )1<<31)) {
    cores = sysconf(_SC_NPROCESSORS_CONF);
  }
#else
  int nm[2];
  size_t len = 4;
  nm[0] = CTL_HW; nm[1] = HW_AVAILCPU;
  sysctl(nm, 2, &cores, &len, NULL, 0);

  if(cores < 1) {
    nm[1] = HW_NCPU;
    sysctl(nm, 2, &cores, &len, NULL, 0);
  }
#endif
  if (cores < 1 || cores > ((long )1<<31)) {
    cores = 4;
  }
  return cores;
}

bool shuso_system_thread_setname(const char *name) {
#if defined SHUTTLESOCK_PTHREAD_SETNAME_STYLE_LINUX
  return pthread_setname_np(pthread_self(), name) == 0;
#elif defined SHUTTLESOCK_PTHREAD_SETNAME_STYLE_FREEBSD
  return pthread_set_name_np(pthread_self(), name) == 0;
#elif defined SHUTTLESOCK_PTHREAD_SETNAME_STYLE_NETBSD
  return pthread_set_name_np(th, "%s", name) == 0;
#else
  return false;
#endif
}

static size_t shuso_system_cacheline_size(void) {
  size_t sz = 0;
#ifdef _SC_LEVEL1_DCACHE_LINESIZE

  sz = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
  if(sz) return sz;
#endif
// Original Author: Nick Strupat
// Date: October 29, 2010
#if defined(__APPLE__)
  size_t szsz = sizeof(sz);
  sysctlbyname("hw.cachelinesize", &sz, &szsz, 0, 0);
#elif defined(__linux__)
  FILE          *p = 0;
  int            lineSize = 0;
  p = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
  if(p) {
    fscanf(p, "%d", &lineSize);
    sz = lineSize;
    fclose(p);
  }
#endif
  if(!sz) {
    //default to 64-byte cacheline
    sz = 64;
  }
  return sz;
}

void shuso_system_initialize(void) {
  if(!shuso_system.initialized) {
    //NOT THREAD-SAFE!!
    shuso_system.page_size = sysconf(_SC_PAGESIZE);
    shuso_system.cacheline_size = shuso_system_cacheline_size();
    shuso_system.page_size_shift = 0;
    for(uintptr_t n = shuso_system.page_size; n >>= 1; shuso_system.page_size_shift++) { /* void */ }
    
    shuso_shared_slab_sizes_init();
    shuso_system.initialized = 1;
  }  
}
