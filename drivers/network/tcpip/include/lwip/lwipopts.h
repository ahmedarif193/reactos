/*
   ------------------------------------
   ---------- Memory options ----------
   ------------------------------------
*/

/* This combo allows us to implement malloc, free, and realloc ourselves */
#define MEM_LIBC_MALLOC                 1
#define MEMP_MEM_MALLOC                 1

/* Define LWIP_COMPAT_MUTEX if the port has no mutexes and binary semaphores
 should be used instead */
#define LWIP_COMPAT_MUTEX               1
#define LWIP_COMPAT_MUTEX_ALLOWED       1

#define MEM_ALIGNMENT                   4

#define LWIP_ARP                        0

#define ETH_PAD_SIZE                    2

#define IP_REASS_MAX_PBUFS              0xFFFFFFFF

#define IP_DEFAULT_TTL                  128

#define IP_SOF_BROADCAST                1

#define IP_SOF_BROADCAST_RECV           1

#define LWIP_ICMP                       0

#define LWIP_RAW                        0

#define LWIP_UDP                        0

#define SO_REUSE                        1

#define SO_REUSE_RXTOALL                1

/* FIXME: These MSS and TCP Window definitions assume an MTU
 * of 1500. We need to add some code to lwIP which would allow us
 * to change these values based upon the interface we are
 * using. Currently ReactOS only supports Ethernet so we're
 * fine for now but it does need to be fixed later when we
 * add support for other transport mediums */
#define TCP_MSS                         1460

#define LWIP_WND_SCALE                  1
#define TCP_RCV_SCALE                   4
#define TCP_WND                         (512 * 1024)

#define TCP_SND_BUF                     (512 * 1024)

#define TCP_SNDLOWAT                    (32 * 1024)
#define TCP_SNDQUEUELOWAT               64

#define TCP_MAXRTX                      8

#define TCP_SYNMAXRTX                   4

#define TCP_LISTEN_BACKLOG              1

#define LWIP_TCP_TIMESTAMPS             1

#define LWIP_TCP_SACK_OUT               1

#define TCP_TMR_INTERVAL                25

#define LWIP_SOCKET                     0

#define LWIP_NETCONN                    0

#define LWIP_STATS                      0

#define ICMP_STATS                      0

/*
   ---------------------------------------
   ---------- Debugging options ----------
   ---------------------------------------
*/

/**
 * TCP_DEBUG: Enable debugging for TCP.
 */
#define TCP_DEBUG                       LWIP_DBG_ON
