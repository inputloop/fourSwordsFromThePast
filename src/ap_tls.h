// TLS for the Archipelago connection (wss://). The system's own TLS library
// is loaded at run time (OpenSSL on Linux and macOS, Schannel on Windows), so
// building the game needs no extra library.
#ifndef ZELDA3_AP_TLS_H_
#define ZELDA3_AP_TLS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <SDL.h>  // SDL_atomic_t

typedef struct ApTls ApTls;

typedef enum ApTlsResult {
  kApTls_Ok,
  kApTls_NoLibrary,  // no usable TLS library on this system
  kApTls_BadCert,    // the server's certificate didn't pass the check
  kApTls_Failed,     // the handshake failed otherwise (often: the server doesn't speak TLS)
  kApTls_Cancelled,  // *cancel was set, or the time ran out
} ApTlsResult;

// fd: a connected, non-blocking TCP socket. The certificate is checked
// against the system's trusted roots (plus the file named by the environment
// variable ZELDA3_AP_CAFILE) and against host. Gives up after timeout_ms, or
// when *cancel is set (also later, while sending).
ApTls *ApTls_Connect(intptr_t fd, const char *host, int timeout_ms, SDL_atomic_t *cancel,
                     ApTlsResult *result, char *err, size_t err_size);
// Bytes taken (maybe fewer than len), 0 if the socket can't take any now,
// -1 on error.
int ApTls_Send(ApTls *t, const void *data, int len);
// Bytes received, 0 if there is nothing right now, -1 when the connection
// closed or broke.
int ApTls_Recv(ApTls *t, void *data, int len);
// True when ApTls_Recv can return data without the socket becoming readable.
bool ApTls_Pending(ApTls *t);
void ApTls_Close(ApTls *t);

// Waits until the socket can be read (or written). 1: ready, 0: timeout,
// -1: error. Shared with ap_ws.c.
int ApTls_WaitSocket(intptr_t fd, bool for_write, int timeout_ms);

#endif  // ZELDA3_AP_TLS_H_
