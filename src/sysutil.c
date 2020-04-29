#include <shuttlesock/build_config.h>
#ifdef SHUTTLESOCK_PTHREAD_SETNAME_STYLE_LINUX
#define _GNU_SOURCE
#define __UNDEF_GNU_SOURCE
#endif
#include <pthread.h>

#include <stdio.h>

#ifdef __UNDEF_GNU_SOURCE
#undef _GNU_SOURCE
#undef __UNDEF_GNU_SOURCE
#endif

//for strsignal()
#ifdef SHUTTLESOCK_HAVE_STRSIGNAL
  #ifndef _GNU_SOURCE
  #define _GNU_SOURCE
  #define __UNDEF_GNU_SOURCE
  #endif
  #ifndef _POSIX_C_SOURCE
  #define _POSIX_C_SOURCE 200809L
  #define __UNDEF_POSIX_C_SOURCE
  #endif
  #include <string.h>
  #ifdef __UNDEF_GNU_SOURCE
  #undef _GNU_SOURCE
  #endif
  #ifdef __UNDEF_POSIX_C_SOURCE
  #undef _POSIX_C_SOURCE
  #endif
#endif
#include <signal.h>
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

#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <netinet/ip.h>
#include <shuttlesock/sysutil.h>
#include <shuttlesock/shared_slab.h>

#include <errno.h>

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


#ifdef SHUTTLESOCK_HAVE_STRSIGNAL
const char *shuso_system_strsignal(int sig) {
  return strsignal(sig);
}
#else
const char *shuso_system_strsignal(int sig) {
  if(sig == SIGABRT)
    return "SIGABRT";
  else if(sig == SIGALRM)
    return "SIGALRM";
#ifdef SIGBUS
  else if(sig == SIGBUS)
    return "SIGBUS";
#endif
  else if(sig == SIGCHLD)
    return "SIGCHLD";
  else if(sig == SIGCONT)
    return "SIGCONT";
  else if(sig == SIGFPE)
    return "SIGFPE";
  else if(sig == SIGHUP)
    return "SIGHUP";
  else if(sig == SIGILL)
    return "SIGILL";
#ifdef SIGINFO
  else if(sig == SIGINFO)
    return "SIGINFO";
#endif
  else if(sig == SIGINT)
    return "SIGINT";
  else if(sig == SIGKILL)
    return "SIGKILL";
  else if(sig == SIGPIPE)
    return "SIGPIPE";
#ifdef SIGPOLL
  else if(sig == SIGPOLL)
    return "SIGPOLL";
#endif
#ifdef SIGIO
  else if(sig == SIGIO)
    return "SIGIO";
#endif
#ifdef SIGPROF
  else if(sig == SIGPROF)
    return "SIGPROF";
#endif
#ifdef SIGPWR
  else if(sig == SIGPWR)
    return "SIGPWR";
#endif
  else if(sig == SIGQUIT)
    return "SIGQUIT";
  else if(sig == SIGSEGV)
    return "SIGSEGV";
  else if(sig == SIGSTOP)
    return "SIGSTOP";
#ifdef SIGSYS
  else if(sig == SIGSYS)
    return "SIGSYS";
#endif
  else if(sig == SIGTERM)
    return "SIGTERM";
#ifdef SIGTRAP
  else if(sig == SIGTRAP)
    return "SIGTRAP";
#endif
  else if(sig == SIGTTIN)
    return "SIGTTIN";
  else if(sig == SIGTTOU)
    return "SIGTTOU";
#ifdef SIGURG
  else if(sig == SIGURG)
    return "SIGURG";
#endif
  else if(sig == SIGUSR1)
    return "SIGUSR1";
  else if(sig == SIGUSR2)
    return "SIGUSR2";
#ifdef SIGVTALRM
  else if(sig == SIGVTALRM)
    return "SIGVTALRM";
#endif
#ifdef SIGXCPU
  else if(sig == SIGXCPU)
    return "SIGXCPU";
#endif
#ifdef SIGXFSZ
  else if(sig == SIGXFSZ)
    return "SIGXFSZ";
#endif
#ifdef SIGWINCH
  else if(sig == SIGWINCH)
    return "SIGWINCH";
#endif
  else
    return "UNKNOWN_SIGNAL";
}
#endif  

const char *shuso_system_errnoname(int errno_) {
/*****************************************************************************\
 * Copyright 2019 Alexander Kozhevnikov <mentalisttraceur@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
\*****************************************************************************/
  switch(errno_) {
#ifdef E2BIG
    case E2BIG: return "E2BIG";
#endif
#ifdef EACCES
    case EACCES: return "EACCES";
#endif
#ifdef EADDRINUSE
    case EADDRINUSE: return "EADDRINUSE";
#endif
#ifdef EADDRNOTAVAIL
    case EADDRNOTAVAIL: return "EADDRNOTAVAIL";
#endif
#ifdef EADI
    case EADI: return "EADI";
#endif
#ifdef EADV
    case EADV: return "EADV";
#endif
#ifdef EAFNOSUPPORT
    case EAFNOSUPPORT: return "EAFNOSUPPORT";
#endif
#ifdef EAGAIN
    case EAGAIN: return "EAGAIN";
#endif
#ifdef EAIO
    case EAIO: return "EAIO";
#endif
#ifdef EALIGN
    case EALIGN: return "EALIGN";
#endif
#ifdef EALREADY
    case EALREADY: return "EALREADY";
#endif
#ifdef EASYNC
    case EASYNC: return "EASYNC";
#endif
#ifdef EAUTH
    case EAUTH: return "EAUTH";
#endif
#ifdef EBADARCH
    case EBADARCH: return "EBADARCH";
#endif
#ifdef EBADE
    case EBADE: return "EBADE";
#endif
#ifdef EBADEXEC
    case EBADEXEC: return "EBADEXEC";
#endif
#ifdef EBADF
    case EBADF: return "EBADF";
#endif
#ifdef EBADFD
    case EBADFD: return "EBADFD";
#endif
#ifdef EBADMACHO
    case EBADMACHO: return "EBADMACHO";
#endif
#ifdef EBADMSG
    case EBADMSG: return "EBADMSG";
#endif
#ifdef EBADR
    case EBADR: return "EBADR";
#endif
#ifdef EBADRPC
    case EBADRPC: return "EBADRPC";
#endif
#ifdef EBADRQC
    case EBADRQC: return "EBADRQC";
#endif
#ifdef EBADSLT
    case EBADSLT: return "EBADSLT";
#endif
#ifdef EBADVER
    case EBADVER: return "EBADVER";
#endif
#ifdef EBFONT
    case EBFONT: return "EBFONT";
#endif
#ifdef EBUSY
    case EBUSY: return "EBUSY";
#endif
#ifdef ECANCELED
    case ECANCELED: return "ECANCELED";
#endif
#if defined(ECANCELLED) && (!defined(ECANCELED) || ECANCELLED != ECANCELED)
    case ECANCELLED: return "ECANCELLED";
#endif
#ifdef ECAPMODE
    case ECAPMODE: return "ECAPMODE";
#endif
#ifdef ECHILD
    case ECHILD: return "ECHILD";
#endif
#ifdef ECHRNG
    case ECHRNG: return "ECHRNG";
#endif
#ifdef ECKPT
    case ECKPT: return "ECKPT";
#endif
#ifdef ECLONEME
    case ECLONEME: return "ECLONEME";
#endif
#ifdef ECOMM
    case ECOMM: return "ECOMM";
#endif
#ifdef ECONFIG
    case ECONFIG: return "ECONFIG";
#endif
#ifdef ECONNABORTED
    case ECONNABORTED: return "ECONNABORTED";
#endif
#ifdef ECONNREFUSED
    case ECONNREFUSED: return "ECONNREFUSED";
#endif
#ifdef ECONNRESET
    case ECONNRESET: return "ECONNRESET";
#endif
#ifdef ECORRUPT
    case ECORRUPT: return "ECORRUPT";
#endif
#ifdef ECVCERORR
    case ECVCERORR: return "ECVCERORR";
#endif
#ifdef ECVPERORR
    case ECVPERORR: return "ECVPERORR";
#endif
#ifdef EDEADLK
    case EDEADLK: return "EDEADLK";
#endif
#if defined(EDEADLOCK) && (!defined(EDEADLK) || EDEADLOCK != EDEADLK)
    case EDEADLOCK: return "EDEADLOCK";
#endif
#ifdef EDESTADDREQ
    case EDESTADDREQ: return "EDESTADDREQ";
#endif
#ifdef EDESTADDRREQ
    case EDESTADDRREQ: return "EDESTADDRREQ";
#endif
#ifdef EDEVERR
    case EDEVERR: return "EDEVERR";
#endif
#ifdef EDIRIOCTL
    case EDIRIOCTL: return "EDIRIOCTL";
#endif
#ifdef EDIRTY
    case EDIRTY: return "EDIRTY";
#endif
#ifdef EDIST
    case EDIST: return "EDIST";
#endif
#ifdef EDOM
    case EDOM: return "EDOM";
#endif
#ifdef EDOOFUS
    case EDOOFUS: return "EDOOFUS";
#endif
#ifdef EDOTDOT
    case EDOTDOT: return "EDOTDOT";
#endif
#ifdef EDQUOT
    case EDQUOT: return "EDQUOT";
#endif
#ifdef EDUPFD
    case EDUPFD: return "EDUPFD";
#endif
#ifdef EDUPPKG
    case EDUPPKG: return "EDUPPKG";
#endif
#ifdef EEXIST
    case EEXIST: return "EEXIST";
#endif
#ifdef EFAIL
    case EFAIL: return "EFAIL";
#endif
#ifdef EFAULT
    case EFAULT: return "EFAULT";
#endif
#ifdef EFBIG
    case EFBIG: return "EFBIG";
#endif
#ifdef EFORMAT
    case EFORMAT: return "EFORMAT";
#endif
#ifdef EFSCORRUPTED
    case EFSCORRUPTED: return "EFSCORRUPTED";
#endif
#ifdef EFTYPE
    case EFTYPE: return "EFTYPE";
#endif
#ifdef EHOSTDOWN
    case EHOSTDOWN: return "EHOSTDOWN";
#endif
#ifdef EHOSTUNREACH
    case EHOSTUNREACH: return "EHOSTUNREACH";
#endif
#ifdef EHWPOISON
    case EHWPOISON: return "EHWPOISON";
#endif
#ifdef EIDRM
    case EIDRM: return "EIDRM";
#endif
#ifdef EILSEQ
    case EILSEQ: return "EILSEQ";
#endif
#ifdef EINIT
    case EINIT: return "EINIT";
#endif
#ifdef EINPROG
    case EINPROG: return "EINPROG";
#endif
#ifdef EINPROGRESS
    case EINPROGRESS: return "EINPROGRESS";
#endif
#ifdef EINTEGRITY
    case EINTEGRITY: return "EINTEGRITY";
#endif
#ifdef EINTR
    case EINTR: return "EINTR";
#endif
#ifdef EINVAL
    case EINVAL: return "EINVAL";
#endif
#ifdef EIO
    case EIO: return "EIO";
#endif
#ifdef EIPSEC
    case EIPSEC: return "EIPSEC";
#endif
#ifdef EISCONN
    case EISCONN: return "EISCONN";
#endif
#ifdef EISDIR
    case EISDIR: return "EISDIR";
#endif
#ifdef EISNAM
    case EISNAM: return "EISNAM";
#endif
#ifdef EJUSTRETURN
    case EJUSTRETURN: return "EJUSTRETURN";
#endif
#ifdef EKEEPLOOKING
    case EKEEPLOOKING: return "EKEEPLOOKING";
#endif
#ifdef EKEYEXPIRED
    case EKEYEXPIRED: return "EKEYEXPIRED";
#endif
#ifdef EKEYREJECTED
    case EKEYREJECTED: return "EKEYREJECTED";
#endif
#ifdef EKEYREVOKED
    case EKEYREVOKED: return "EKEYREVOKED";
#endif
#ifdef EL2HLT
    case EL2HLT: return "EL2HLT";
#endif
#ifdef EL2NSYNC
    case EL2NSYNC: return "EL2NSYNC";
#endif
#ifdef EL3HLT
    case EL3HLT: return "EL3HLT";
#endif
#ifdef EL3RST
    case EL3RST: return "EL3RST";
#endif
#ifdef ELIBACC
    case ELIBACC: return "ELIBACC";
#endif
#ifdef ELIBBAD
    case ELIBBAD: return "ELIBBAD";
#endif
#ifdef ELIBEXEC
    case ELIBEXEC: return "ELIBEXEC";
#endif
#ifdef ELIBMAX
    case ELIBMAX: return "ELIBMAX";
#endif
#ifdef ELIBSCN
    case ELIBSCN: return "ELIBSCN";
#endif
#ifdef ELNRNG
    case ELNRNG: return "ELNRNG";
#endif
#ifdef ELOCKUNMAPPED
    case ELOCKUNMAPPED: return "ELOCKUNMAPPED";
#endif
#ifdef ELOOP
    case ELOOP: return "ELOOP";
#endif
#ifdef EMEDIA
    case EMEDIA: return "EMEDIA";
#endif
#ifdef EMEDIUMTYPE
    case EMEDIUMTYPE: return "EMEDIUMTYPE";
#endif
#ifdef EMFILE
    case EMFILE: return "EMFILE";
#endif
#ifdef EMLINK
    case EMLINK: return "EMLINK";
#endif
#ifdef EMOUNTEXIT
    case EMOUNTEXIT: return "EMOUNTEXIT";
#endif
#ifdef EMOVEFD
    case EMOVEFD: return "EMOVEFD";
#endif
#ifdef EMSGSIZE
    case EMSGSIZE: return "EMSGSIZE";
#endif
#ifdef EMTIMERS
    case EMTIMERS: return "EMTIMERS";
#endif
#ifdef EMULTIHOP
    case EMULTIHOP: return "EMULTIHOP";
#endif
#ifdef ENAMETOOLONG
    case ENAMETOOLONG: return "ENAMETOOLONG";
#endif
#ifdef ENAVAIL
    case ENAVAIL: return "ENAVAIL";
#endif
#ifdef ENEEDAUTH
    case ENEEDAUTH: return "ENEEDAUTH";
#endif
#ifdef ENETDOWN
    case ENETDOWN: return "ENETDOWN";
#endif
#ifdef ENETRESET
    case ENETRESET: return "ENETRESET";
#endif
#ifdef ENETUNREACH
    case ENETUNREACH: return "ENETUNREACH";
#endif
#ifdef ENFILE
    case ENFILE: return "ENFILE";
#endif
#ifdef ENFSREMOTE
    case ENFSREMOTE: return "ENFSREMOTE";
#endif
#ifdef ENOANO
    case ENOANO: return "ENOANO";
#endif
#ifdef ENOATTR
    case ENOATTR: return "ENOATTR";
#endif
#ifdef ENOBUFS
    case ENOBUFS: return "ENOBUFS";
#endif
#ifdef ENOCONNECT
    case ENOCONNECT: return "ENOCONNECT";
#endif
#ifdef ENOCSI
    case ENOCSI: return "ENOCSI";
#endif
#ifdef ENODATA
    case ENODATA: return "ENODATA";
#endif
#ifdef ENODEV
    case ENODEV: return "ENODEV";
#endif
#ifdef ENOENT
    case ENOENT: return "ENOENT";
#endif
#ifdef ENOEXEC
    case ENOEXEC: return "ENOEXEC";
#endif
#ifdef ENOIOCTL
    case ENOIOCTL: return "ENOIOCTL";
#endif
#ifdef ENOKEY
    case ENOKEY: return "ENOKEY";
#endif
#ifdef ENOLCK
    case ENOLCK: return "ENOLCK";
#endif
#ifdef ENOLINK
    case ENOLINK: return "ENOLINK";
#endif
#ifdef ENOLOAD
    case ENOLOAD: return "ENOLOAD";
#endif
#ifdef ENOMATCH
    case ENOMATCH: return "ENOMATCH";
#endif
#ifdef ENOMEDIUM
    case ENOMEDIUM: return "ENOMEDIUM";
#endif
#ifdef ENOMEM
    case ENOMEM: return "ENOMEM";
#endif
#ifdef ENOMSG
    case ENOMSG: return "ENOMSG";
#endif
#ifdef ENONET
    case ENONET: return "ENONET";
#endif
#ifdef ENOPKG
    case ENOPKG: return "ENOPKG";
#endif
#ifdef ENOPOLICY
    case ENOPOLICY: return "ENOPOLICY";
#endif
#ifdef ENOPROTOOPT
    case ENOPROTOOPT: return "ENOPROTOOPT";
#endif
#ifdef ENOREG
    case ENOREG: return "ENOREG";
#endif
#ifdef ENOSPC
    case ENOSPC: return "ENOSPC";
#endif
#ifdef ENOSR
    case ENOSR: return "ENOSR";
#endif
#ifdef ENOSTR
    case ENOSTR: return "ENOSTR";
#endif
#ifdef ENOSYM
    case ENOSYM: return "ENOSYM";
#endif
#ifdef ENOSYS
    case ENOSYS: return "ENOSYS";
#endif
#ifdef ENOTACTIVE
    case ENOTACTIVE: return "ENOTACTIVE";
#endif
#ifdef ENOTBLK
    case ENOTBLK: return "ENOTBLK";
#endif
#ifdef ENOTCAPABLE
    case ENOTCAPABLE: return "ENOTCAPABLE";
#endif
#ifdef ENOTCONN
    case ENOTCONN: return "ENOTCONN";
#endif
#ifdef ENOTDIR
    case ENOTDIR: return "ENOTDIR";
#endif
#ifdef ENOTEMPTY
    case ENOTEMPTY: return "ENOTEMPTY";
#endif
#ifdef ENOTNAM
    case ENOTNAM: return "ENOTNAM";
#endif
#ifdef ENOTREADY
    case ENOTREADY: return "ENOTREADY";
#endif
#ifdef ENOTRECOVERABLE
    case ENOTRECOVERABLE: return "ENOTRECOVERABLE";
#endif
#ifdef ENOTRUST
    case ENOTRUST: return "ENOTRUST";
#endif
#ifdef ENOTSOCK
    case ENOTSOCK: return "ENOTSOCK";
#endif
#ifdef ENOTSUP
    case ENOTSUP: return "ENOTSUP";
#endif
#ifdef ENOTTY
    case ENOTTY: return "ENOTTY";
#endif
#ifdef ENOTUNIQ
    case ENOTUNIQ: return "ENOTUNIQ";
#endif
#ifdef ENOUNLD
    case ENOUNLD: return "ENOUNLD";
#endif
#ifdef ENOUNREG
    case ENOUNREG: return "ENOUNREG";
#endif
#ifdef ENXIO
    case ENXIO: return "ENXIO";
#endif
#ifdef EOPCOMPLETE
    case EOPCOMPLETE: return "EOPCOMPLETE";
#endif
#if defined(EOPNOTSUPP) && (!defined(ENOTSUP) || EOPNOTSUPP != ENOTSUP)
    case EOPNOTSUPP: return "EOPNOTSUPP";
#endif
#ifdef EOVERFLOW
    case EOVERFLOW: return "EOVERFLOW";
#endif
#ifdef EOWNERDEAD
    case EOWNERDEAD: return "EOWNERDEAD";
#endif
#ifdef EPASSTHROUGH
    case EPASSTHROUGH: return "EPASSTHROUGH";
#endif
#ifdef EPATHREMOTE
    case EPATHREMOTE: return "EPATHREMOTE";
#endif
#ifdef EPERM
    case EPERM: return "EPERM";
#endif
#ifdef EPFNOSUPPORT
    case EPFNOSUPPORT: return "EPFNOSUPPORT";
#endif
#ifdef EPIPE
    case EPIPE: return "EPIPE";
#endif
#ifdef EPOWERF
    case EPOWERF: return "EPOWERF";
#endif
#ifdef EPROCLIM
    case EPROCLIM: return "EPROCLIM";
#endif
#ifdef EPROCUNAVAIL
    case EPROCUNAVAIL: return "EPROCUNAVAIL";
#endif
#ifdef EPROGMISMATCH
    case EPROGMISMATCH: return "EPROGMISMATCH";
#endif
#ifdef EPROGUNAVAIL
    case EPROGUNAVAIL: return "EPROGUNAVAIL";
#endif
#ifdef EPROTO
    case EPROTO: return "EPROTO";
#endif
#ifdef EPROTONOSUPPORT
    case EPROTONOSUPPORT: return "EPROTONOSUPPORT";
#endif
#ifdef EPROTOTYPE
    case EPROTOTYPE: return "EPROTOTYPE";
#endif
#ifdef EPWROFF
    case EPWROFF: return "EPWROFF";
#endif
#ifdef EQFULL
    case EQFULL: return "EQFULL";
#endif
#ifdef EQSUSPENDED
    case EQSUSPENDED: return "EQSUSPENDED";
#endif
#ifdef ERANGE
    case ERANGE: return "ERANGE";
#endif
#ifdef ERECYCLE
    case ERECYCLE: return "ERECYCLE";
#endif
#ifdef EREDRIVEOPEN
    case EREDRIVEOPEN: return "EREDRIVEOPEN";
#endif
#ifdef EREFUSED
    case EREFUSED: return "EREFUSED";
#endif
#ifdef ERELOC
    case ERELOC: return "ERELOC";
#endif
#ifdef ERELOCATED
    case ERELOCATED: return "ERELOCATED";
#endif
#ifdef ERELOOKUP
    case ERELOOKUP: return "ERELOOKUP";
#endif
#ifdef EREMCHG
    case EREMCHG: return "EREMCHG";
#endif
#ifdef EREMDEV
    case EREMDEV: return "EREMDEV";
#endif
#ifdef EREMOTE
    case EREMOTE: return "EREMOTE";
#endif
#ifdef EREMOTEIO
    case EREMOTEIO: return "EREMOTEIO";
#endif
#ifdef EREMOTERELEASE
    case EREMOTERELEASE: return "EREMOTERELEASE";
#endif
#ifdef ERESTART
    case ERESTART: return "ERESTART";
#endif
#ifdef ERFKILL
    case ERFKILL: return "ERFKILL";
#endif
#ifdef EROFS
    case EROFS: return "EROFS";
#endif
#ifdef ERPCMISMATCH
    case ERPCMISMATCH: return "ERPCMISMATCH";
#endif
#ifdef ESAD
    case ESAD: return "ESAD";
#endif
#ifdef ESHLIBVERS
    case ESHLIBVERS: return "ESHLIBVERS";
#endif
#ifdef ESHUTDOWN
    case ESHUTDOWN: return "ESHUTDOWN";
#endif
#ifdef ESOCKTNOSUPPORT
    case ESOCKTNOSUPPORT: return "ESOCKTNOSUPPORT";
#endif
#ifdef ESOFT
    case ESOFT: return "ESOFT";
#endif
#ifdef ESPIPE
    case ESPIPE: return "ESPIPE";
#endif
#ifdef ESRCH
    case ESRCH: return "ESRCH";
#endif
#ifdef ESRMNT
    case ESRMNT: return "ESRMNT";
#endif
#ifdef ESTALE
    case ESTALE: return "ESTALE";
#endif
#ifdef ESTART
    case ESTART: return "ESTART";
#endif
#ifdef ESTRPIPE
    case ESTRPIPE: return "ESTRPIPE";
#endif
#ifdef ESYSERROR
    case ESYSERROR: return "ESYSERROR";
#endif
#ifdef ETIME
    case ETIME: return "ETIME";
#endif
#ifdef ETIMEDOUT
    case ETIMEDOUT: return "ETIMEDOUT";
#endif
#ifdef ETOOMANYREFS
    case ETOOMANYREFS: return "ETOOMANYREFS";
#endif
#ifdef ETXTBSY
    case ETXTBSY: return "ETXTBSY";
#endif
#ifdef EUCLEAN
    case EUCLEAN: return "EUCLEAN";
#endif
#ifdef EUNATCH
    case EUNATCH: return "EUNATCH";
#endif
#ifdef EUSERS
    case EUSERS: return "EUSERS";
#endif
#ifdef EVERSION
    case EVERSION: return "EVERSION";
#endif
#if defined(EWOULDBLOCK) && (!defined(EAGAIN) || EWOULDBLOCK != EAGAIN)
    case EWOULDBLOCK: return "EWOULDBLOCK";
#endif
#ifdef EWRONGFS
    case EWRONGFS: return "EWRONGFS";
#endif
#ifdef EWRPROTECT
    case EWRPROTECT: return "EWRPROTECT";
#endif
#ifdef EXDEV
    case EXDEV: return "EXDEV";
#endif
#ifdef EXFULL
    case EXFULL: return "EXFULL";
#endif
  }
  return 0;
}
  
socklen_t shuso_sockaddr_len(shuso_sockaddr_t *sa) {
  switch(sa->any.sa_family) {
    case AF_INET:
      return sizeof(struct sockaddr_in);
#ifdef SHUTTLESOCK_HAVE_IPV6
    case AF_INET6:
      return sizeof(struct sockaddr_in6);
#endif
    case AF_UNIX:
      return sizeof(struct sockaddr_un);
  }
  return 0;
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
