// TLS for the Archipelago connection. Windows: Schannel, from secur32.dll and
// crypt32.dll. Elsewhere: the system's OpenSSL. Both are loaded at run time,
// so the game links no TLS library and still starts on systems without one
// (then only ws:// works). The sockets are non-blocking, so a stalled server
// never keeps the network thread from noticing that it should stop.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "ap_tls.h"

enum {
  kSliceMs = 50,          // how often waits look at *cancel
  kSendTimeoutMs = 15000, // a send that can't get rid of its data for this long gives up
};

static bool TimeUp(uint32_t deadline) {
  return (int32_t)(deadline - SDL_GetTicks()) <= 0;
}

static bool Cancelled(SDL_atomic_t *cancel) {
  return cancel && SDL_AtomicGet(cancel);
}

int ApTls_WaitSocket(intptr_t fd, bool for_write, int timeout_ms) {
  if (timeout_ms < 0)
    timeout_ms = 0;
#ifdef _WIN32
  fd_set set, err_set;
  FD_ZERO(&set);
  FD_ZERO(&err_set);
  FD_SET((SOCKET)fd, &set);
  FD_SET((SOCKET)fd, &err_set);
  struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
  // A failed non-blocking connect shows up in the error set only.
  int r = select(0, for_write ? NULL : &set, for_write ? &set : NULL, &err_set, &tv);
  return r < 0 ? -1 : r > 0;
#else
  struct pollfd p = { (int)fd, for_write ? POLLOUT : POLLIN, 0 };
  int r = poll(&p, 1, timeout_ms);
  if (r < 0)
    return errno == EINTR ? 0 : -1;
  return r > 0;
#endif
}

// Waits a short while for the socket. 1: ready, 0: not yet, -1: cancelled,
// out of time or broken.
static int WaitSlice(intptr_t fd, bool for_write, uint32_t deadline, SDL_atomic_t *cancel) {
  if (Cancelled(cancel) || TimeUp(deadline))
    return -1;
  int left = (int)(deadline - SDL_GetTicks());
  return ApTls_WaitSocket(fd, for_write, left < kSliceMs ? left : kSliceMs);
}

static void SetErr(char *err, size_t err_size, const char *fmt, const char *arg) {
  if (err && err_size)
    snprintf(err, err_size, fmt, arg ? arg : "");
}

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Schannel. The few declarations needed from sspi.h, schannel.h and
// wincrypt.h are repeated here (with our own names) because not every
// compiler the game is built with ships those headers.

typedef struct ApSecHandle { ULONG_PTR lower, upper; } ApSecHandle;
typedef struct ApSecBuffer { unsigned long size, type; void *data; } ApSecBuffer;
typedef struct ApSecBufferDesc { unsigned long version, count; ApSecBuffer *buffers; } ApSecBufferDesc;
typedef struct ApStreamSizes { unsigned long header, trailer, max_message, buffers, block_size; } ApStreamSizes;
typedef struct ApSchannelCred {
  DWORD version, num_creds;
  void *creds, *root_store;
  DWORD num_mappers;
  void **mappers;
  DWORD num_algs;
  void *algs;
  DWORD protocols, min_cipher_strength, max_cipher_strength, session_lifespan, flags, cred_format;
} ApSchannelCred;

typedef LONG (WINAPI *ApAcquireCredentialsHandleFn)(char *principal, char *package, unsigned long use,
    void *logon_id, void *auth_data, void *get_key, void *get_key_arg, ApSecHandle *cred, void *expiry);
typedef LONG (WINAPI *ApFreeCredentialsHandleFn)(ApSecHandle *cred);
typedef LONG (WINAPI *ApInitializeSecurityContextFn)(ApSecHandle *cred, ApSecHandle *ctx, char *target,
    unsigned long req, unsigned long reserved1, unsigned long data_rep, ApSecBufferDesc *input,
    unsigned long reserved2, ApSecHandle *new_ctx, ApSecBufferDesc *output, unsigned long *attrs, void *expiry);
typedef LONG (WINAPI *ApDeleteSecurityContextFn)(ApSecHandle *ctx);
typedef LONG (WINAPI *ApQueryContextAttributesFn)(ApSecHandle *ctx, unsigned long attr, void *buf);
typedef LONG (WINAPI *ApFreeContextBufferFn)(void *buf);
typedef LONG (WINAPI *ApEncryptMessageFn)(ApSecHandle *ctx, unsigned long qop, ApSecBufferDesc *msg, unsigned long seq);
typedef LONG (WINAPI *ApDecryptMessageFn)(ApSecHandle *ctx, ApSecBufferDesc *msg, unsigned long seq, unsigned long *qop);

// SecurityFunctionTableA, up to the last entry used.
typedef struct ApSspiTable {
  unsigned long version;
  void *EnumerateSecurityPackagesA, *QueryCredentialsAttributesA;
  ApAcquireCredentialsHandleFn AcquireCredentialsHandleA;
  ApFreeCredentialsHandleFn FreeCredentialsHandle;
  void *Reserved2;
  ApInitializeSecurityContextFn InitializeSecurityContextA;
  void *AcceptSecurityContext, *CompleteAuthToken;
  ApDeleteSecurityContextFn DeleteSecurityContext;
  void *ApplyControlToken;
  ApQueryContextAttributesFn QueryContextAttributesA;
  void *ImpersonateSecurityContext, *RevertSecurityContext, *MakeSignature, *VerifySignature;
  ApFreeContextBufferFn FreeContextBuffer;
  void *QuerySecurityPackageInfoA, *Reserved3, *Reserved4, *ExportSecurityContext;
  void *ImportSecurityContextA, *AddCredentialsA, *Reserved8, *QuerySecurityContextToken;
  ApEncryptMessageFn EncryptMessage;
  ApDecryptMessageFn DecryptMessage;
} ApSspiTable;
typedef ApSspiTable *(WINAPI *ApInitSecurityInterfaceFn)(void);

typedef struct ApCertContext {
  DWORD encoding;
  BYTE *encoded;
  DWORD encoded_size;
  void *info, *store;
} ApCertContext;
typedef struct ApTrustStatus { DWORD error_status, info_status; } ApTrustStatus;
// Chain structures: only their leading fields, which is all that is read.
typedef struct ApChainElement { DWORD size; const ApCertContext *cert; ApTrustStatus trust; } ApChainElement;
typedef struct ApSimpleChain { DWORD size; ApTrustStatus trust; DWORD num_elements; ApChainElement **elements; } ApSimpleChain;
typedef struct ApChainContext { DWORD size; ApTrustStatus trust; DWORD num_chains; ApSimpleChain **chains; } ApChainContext;
typedef struct ApChainPara {
  DWORD size;
  struct { DWORD type; struct { DWORD count; char **ids; } usage; } requested_usage;
} ApChainPara;
typedef struct ApSslPolicyPara { DWORD size, auth_type, checks; WCHAR *server_name; } ApSslPolicyPara;
typedef struct ApPolicyPara { DWORD size, flags; void *extra; } ApPolicyPara;
typedef struct ApPolicyStatus { DWORD size, error; LONG chain_index, element_index; void *extra; } ApPolicyStatus;

typedef BOOL (WINAPI *ApCertGetCertificateChainFn)(void *engine, const ApCertContext *cert, FILETIME *time,
    void *extra_store, ApChainPara *para, DWORD flags, void *reserved, const ApChainContext **chain);
typedef BOOL (WINAPI *ApCertVerifyCertificateChainPolicyFn)(const char *policy, const ApChainContext *chain,
    ApPolicyPara *para, ApPolicyStatus *status);
typedef void (WINAPI *ApCertFreeCertificateChainFn)(const ApChainContext *chain);
typedef BOOL (WINAPI *ApCertFreeCertificateContextFn)(const ApCertContext *cert);
typedef BOOL (WINAPI *ApCertAddEncodedCertificateToStoreFn)(void *store, DWORD encoding, const BYTE *data,
    DWORD size, DWORD disposition, const ApCertContext **added);

#define AP_SEC_E_OK                    ((LONG)0)
#define AP_SEC_I_CONTINUE_NEEDED       ((LONG)0x00090312)
#define AP_SEC_I_CONTEXT_EXPIRED       ((LONG)0x00090317)
#define AP_SEC_I_INCOMPLETE_CREDENTIALS ((LONG)0x00090320)
#define AP_SEC_I_RENEGOTIATE           ((LONG)0x00090321)
#define AP_SEC_E_INCOMPLETE_MESSAGE    ((LONG)0x80090318)
#define AP_SEC_E_ALGORITHM_MISMATCH    ((LONG)0x80090331)
#define AP_SECBUFFER_VERSION 0
#define AP_SECBUFFER_EMPTY 0
#define AP_SECBUFFER_DATA 1
#define AP_SECBUFFER_TOKEN 2
#define AP_SECBUFFER_EXTRA 5
#define AP_SECBUFFER_STREAM_TRAILER 6
#define AP_SECBUFFER_STREAM_HEADER 7
#define AP_SECPKG_CRED_OUTBOUND 2
#define AP_SECPKG_ATTR_STREAM_SIZES 4
#define AP_SECPKG_ATTR_REMOTE_CERT_CONTEXT 0x53
#define AP_ISC_REQ_FLAGS (0x4 | 0x8 | 0x10 | 0x80 | 0x100 | 0x8000)  // replay, sequence, confidentiality,
                                                                    // supplied creds, allocate memory, stream
#define AP_SCHANNEL_CRED_VERSION 4
#define AP_SP_PROT_TLS1_2_CLIENT 0x800
#define AP_SCH_CRED_MANUAL_CRED_VALIDATION 0x8
#define AP_SCH_CRED_NO_DEFAULT_CREDS 0x10
#define AP_SCH_USE_STRONG_CRYPTO 0x00400000
#define AP_UNISP_NAME "Microsoft Unified Security Protocol Provider"
#define AP_CERT_ENCODING (0x1 | 0x10000)  // X509_ASN_ENCODING | PKCS_7_ASN_ENCODING
#define AP_CERT_STORE_ADD_USE_EXISTING 2
#define AP_CERT_TRUST_IS_NOT_TIME_VALID 0x1
#define AP_CERT_TRUST_IS_REVOKED 0x4
#define AP_CERT_TRUST_IS_NOT_VALID_FOR_USAGE 0x10
#define AP_CERT_TRUST_IS_UNTRUSTED_ROOT 0x20
#define AP_CERT_TRUST_REVOCATION_UNKNOWN 0x40
#define AP_CERT_TRUST_IS_PARTIAL_CHAIN 0x10000
#define AP_CERT_TRUST_OFFLINE_REVOCATION 0x1000000
#define AP_USAGE_MATCH_TYPE_OR 1
#define AP_AUTHTYPE_SERVER 2
#define AP_SECURITY_FLAG_IGNORE_UNKNOWN_CA 0x100
#define AP_CERT_CHAIN_POLICY_SSL ((const char *)4)

static struct {
  bool tried, ok;
  ApSspiTable *sspi;
  ApCertGetCertificateChainFn CertGetCertificateChain;
  ApCertVerifyCertificateChainPolicyFn CertVerifyCertificateChainPolicy;
  ApCertFreeCertificateChainFn CertFreeCertificateChain;
  ApCertFreeCertificateContextFn CertFreeCertificateContext;
  ApCertAddEncodedCertificateToStoreFn CertAddEncodedCertificateToStore;
} g_sc;

enum { kInSize = 16384 + 2048 + 16384 };  // one full TLS record and then some

struct ApTls {
  SOCKET fd;
  SDL_atomic_t *cancel;
  ApSecHandle cred, ctx;
  bool have_cred, have_ctx;
  ApStreamSizes sizes;
  int received;      // ciphertext bytes in incoming
  bool need_more;    // the ciphertext there is only part of a record
  char *dec;         // decrypted bytes not returned yet (they live inside incoming)
  int dec_len;
  int extra;         // ciphertext that follows the decrypted record
  char incoming[kInSize];
  char outgoing[16384 + 1024];
};

static bool GetProc(HMODULE dll, const char *name, void *fn) {
  FARPROC p = GetProcAddress(dll, name);
  memcpy(fn, &p, sizeof(p));
  return p != NULL;
}

// From System32 only, not from the game's folder (where anyone could drop a
// look-alike). Windows 7 without its updates doesn't know that flag.
static HMODULE LoadSystemDll(const char *name) {
  HMODULE h = LoadLibraryExA(name, NULL, 0x800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
  return h ? h : LoadLibraryA(name);
}

static bool LoadSchannel(void) {
  if (g_sc.tried)
    return g_sc.ok;
  g_sc.tried = true;
  HMODULE secur = LoadSystemDll("secur32.dll"), crypt = LoadSystemDll("crypt32.dll");
  ApInitSecurityInterfaceFn init;
  if (!secur || !crypt || !GetProc(secur, "InitSecurityInterfaceA", &init) || !(g_sc.sspi = init()))
    return false;
  g_sc.ok = GetProc(crypt, "CertGetCertificateChain", &g_sc.CertGetCertificateChain) &&
            GetProc(crypt, "CertVerifyCertificateChainPolicy", &g_sc.CertVerifyCertificateChainPolicy) &&
            GetProc(crypt, "CertFreeCertificateChain", &g_sc.CertFreeCertificateChain) &&
            GetProc(crypt, "CertFreeCertificateContext", &g_sc.CertFreeCertificateContext) &&
            GetProc(crypt, "CertAddEncodedCertificateToStore", &g_sc.CertAddEncodedCertificateToStore);
  return g_sc.ok;
}

// Sends all of it, waiting for the socket as needed.
static bool SendRaw(ApTls *t, const char *p, int n) {
  uint32_t deadline = SDL_GetTicks() + kSendTimeoutMs;
  while (n > 0) {
    int r = send(t->fd, p, n, 0);
    if (r > 0) {
      p += r, n -= r;
    } else if (r < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
      if (WaitSlice((intptr_t)t->fd, true, deadline, t->cancel) < 0)
        return false;
    } else {
      return false;
    }
  }
  return true;
}

// Certificates from a PEM (or DER) file, for ZELDA3_AP_CAFILE.
typedef struct ApDer { BYTE *data; DWORD size; } ApDer;

static int Base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  return c == '+' ? 62 : c == '/' ? 63 : -1;
}

static int LoadCaFile(const char *path, ApDer *out, int max) {
  FILE *f = path && *path ? fopen(path, "rb") : NULL;
  if (!f)
    return 0;
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (file_size <= 0 || file_size > (16 << 20)) {
    fclose(f);
    return 0;
  }
  char *buf = malloc(file_size + 1);
  size_t len = fread(buf, 1, file_size, f);
  fclose(f);
  buf[len] = 0;
  int n = 0;
  const char *p = buf;
  if (!strstr(buf, "-----BEGIN CERTIFICATE-----")) {  // plain DER
    if (len && max) {
      out[0].data = malloc(len);
      memcpy(out[0].data, buf, len);
      out[0].size = (DWORD)len;
      n = 1;
    }
    free(buf);
    return n;
  }
  while (n < max && (p = strstr(p, "-----BEGIN CERTIFICATE-----")) != NULL) {
    p += 27;
    const char *end = strstr(p, "-----END CERTIFICATE-----");
    if (!end)
      break;
    BYTE *der = malloc((end - p) * 3 / 4 + 3);
    DWORD size = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (; p < end; p++) {
      int v = Base64Value(*p);
      if (v < 0)
        continue;
      acc = acc << 6 | v, bits += 6;
      if (bits >= 8)
        bits -= 8, der[size++] = (BYTE)(acc >> bits);
    }
    out[n].data = der, out[n].size = size;
    n++;
  }
  free(buf);
  return n;
}

static const char *CertErrorText(DWORD code) {
  switch (code) {
  case 0x800B0109: return "it isn't signed by a trusted authority";
  case 0x800B010F: return "it was made for another host name";
  case 0x800B0101: return "it has expired or isn't valid yet";
  case 0x800B010C: return "it was revoked";
  case 0x800B010A: return "its chain of certificates is incomplete";
  case 0x800B0110: return "it isn't meant for servers";
  default: return NULL;
  }
}

// Schannel was told to skip its own check (so that a test CA can be added),
// so the chain and the host name are checked here, like a browser would.
static ApTlsResult CheckCertificate(ApTls *t, const char *host, char *err, size_t err_size) {
  const ApCertContext *cert = NULL;
  if (g_sc.sspi->QueryContextAttributesA(&t->ctx, AP_SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert) != AP_SEC_E_OK || !cert) {
    SetErr(err, err_size, "The server sent no certificate%s", NULL);
    return kApTls_BadCert;
  }
  ApDer ca[8];
  int nca = LoadCaFile(getenv("ZELDA3_AP_CAFILE"), ca, 8);
  for (int i = 0; i < nca; i++)
    g_sc.CertAddEncodedCertificateToStore(cert->store, AP_CERT_ENCODING, ca[i].data, ca[i].size,
                                          AP_CERT_STORE_ADD_USE_EXISTING, NULL);
  char server_auth[] = "1.3.6.1.5.5.7.3.1";
  char *usages[1] = { server_auth };
  ApChainPara para;
  memset(&para, 0, sizeof(para));
  para.size = (DWORD)sizeof(para);
  para.requested_usage.type = AP_USAGE_MATCH_TYPE_OR;
  para.requested_usage.usage.count = 1;
  para.requested_usage.usage.ids = usages;
  const ApChainContext *chain = NULL;
  ApTlsResult result = kApTls_BadCert;
  if (!g_sc.CertGetCertificateChain(NULL, cert, NULL, cert->store, &para, 0, NULL, &chain) || !chain) {
    SetErr(err, err_size, "Windows couldn't check the server's certificate%s", NULL);
    goto done;
  }
  DWORD checks = 0, allowed = 0;
  // A root that only the CA file trusts is fine.
  if (nca && (chain->trust.error_status & AP_CERT_TRUST_IS_UNTRUSTED_ROOT) && chain->num_chains) {
    const ApSimpleChain *sc = chain->chains[0];
    const ApCertContext *root = sc->num_elements ? sc->elements[sc->num_elements - 1]->cert : NULL;
    for (int i = 0; root && i < nca; i++)
      if (root->encoded_size == ca[i].size && !memcmp(root->encoded, ca[i].data, ca[i].size))
        checks = AP_SECURITY_FLAG_IGNORE_UNKNOWN_CA, allowed = AP_CERT_TRUST_IS_UNTRUSTED_ROOT;
  }
  // The policy check below doesn't look at everything everywhere (Wine's
  // takes a chain that never reached a root), so the chain's own verdict
  // has to be clean too. Revocation isn't asked for, so its flags are ignored.
  DWORD trust_errors = chain->trust.error_status & ~allowed &
                       ~(AP_CERT_TRUST_REVOCATION_UNKNOWN | AP_CERT_TRUST_OFFLINE_REVOCATION);
  if (trust_errors) {
    char why[64];
    snprintf(why, sizeof(why), "%s",
             trust_errors & AP_CERT_TRUST_IS_NOT_TIME_VALID ? "it has expired or isn't valid yet" :
             trust_errors & AP_CERT_TRUST_IS_REVOKED ? "it was revoked" :
             trust_errors & AP_CERT_TRUST_IS_NOT_VALID_FOR_USAGE ? "it isn't meant for servers" :
             trust_errors & (AP_CERT_TRUST_IS_UNTRUSTED_ROOT | AP_CERT_TRUST_IS_PARTIAL_CHAIN) ?
                 "it isn't signed by a trusted authority" : "");
    if (!why[0])
      snprintf(why, sizeof(why), "trust error 0x%08lx", (unsigned long)trust_errors);
    SetErr(err, err_size, "The server's certificate was rejected: %s", why);
    g_sc.CertFreeCertificateChain(chain);
    goto done;
  }
  WCHAR wide_host[256];
  if (!MultiByteToWideChar(CP_UTF8, 0, host, -1, wide_host, 256))
    wide_host[0] = 0;
  ApSslPolicyPara ssl = { (DWORD)sizeof(ApSslPolicyPara), AP_AUTHTYPE_SERVER, checks, wide_host };
  ApPolicyPara policy = { (DWORD)sizeof(ApPolicyPara), 0, &ssl };
  ApPolicyStatus status;
  memset(&status, 0, sizeof(status));
  status.size = (DWORD)sizeof(status);
  if (!g_sc.CertVerifyCertificateChainPolicy(AP_CERT_CHAIN_POLICY_SSL, chain, &policy, &status)) {
    SetErr(err, err_size, "Windows couldn't check the server's certificate%s", NULL);
  } else if (status.error) {
    const char *why = CertErrorText(status.error);
    char code[64];
    if (!why)
      snprintf(code, sizeof(code), "error 0x%08lx", (unsigned long)status.error), why = code;
    SetErr(err, err_size, "The server's certificate was rejected: %s", why);
  } else {
    result = kApTls_Ok;
  }
  g_sc.CertFreeCertificateChain(chain);
done:
  g_sc.CertFreeCertificateContext(cert);
  for (int i = 0; i < nca; i++)
    free(ca[i].data);
  return result;
}

static ApTlsResult Handshake(ApTls *t, const char *host, uint32_t deadline, char *err, size_t err_size) {
  ApSspiTable *S = g_sc.sspi;
  for (bool first = true;; first = false) {
    ApSecBuffer in[2] = {
      { (unsigned long)t->received, AP_SECBUFFER_TOKEN, t->incoming },
      { 0, AP_SECBUFFER_EMPTY, NULL },
    };
    ApSecBuffer out[1] = { { 0, AP_SECBUFFER_TOKEN, NULL } };
    ApSecBufferDesc in_desc = { AP_SECBUFFER_VERSION, 2, in }, out_desc = { AP_SECBUFFER_VERSION, 1, out };
    unsigned long attrs = 0;
    LONG sec = S->InitializeSecurityContextA(&t->cred, first ? NULL : &t->ctx, (char *)host, AP_ISC_REQ_FLAGS,
                                             0, 0, first ? NULL : &in_desc, 0, &t->ctx, &out_desc, &attrs, NULL);
    if (first && (sec == AP_SEC_E_OK || sec == AP_SEC_I_CONTINUE_NEEDED || sec == AP_SEC_E_INCOMPLETE_MESSAGE))
      t->have_ctx = true;
    if (out[0].size && out[0].data) {
      bool ok = SendRaw(t, out[0].data, (int)out[0].size);
      S->FreeContextBuffer(out[0].data);
      if (!ok) {
        SetErr(err, err_size, "The connection broke during the TLS handshake%s", NULL);
        return Cancelled(t->cancel) ? kApTls_Cancelled : kApTls_Failed;
      }
    }
    if (sec != AP_SEC_E_INCOMPLETE_MESSAGE) {
      if (in[1].type == AP_SECBUFFER_EXTRA && in[1].size && !first) {
        memmove(t->incoming, t->incoming + t->received - in[1].size, in[1].size);
        t->received = (int)in[1].size;
      } else {
        t->received = 0;
      }
      if (sec == AP_SEC_E_OK)
        return kApTls_Ok;
      if (sec == AP_SEC_I_INCOMPLETE_CREDENTIALS) {
        SetErr(err, err_size, "The server wants a client certificate%s", NULL);
        return kApTls_Failed;
      }
      if (sec != AP_SEC_I_CONTINUE_NEEDED) {
        char text[96];
        snprintf(text, sizeof(text), sec == AP_SEC_E_ALGORITHM_MISMATCH ?
                 "no TLS version or cipher in common (0x%08lx)" : "error 0x%08lx", (unsigned long)sec);
        SetErr(err, err_size, "TLS handshake failed: %s", text);
        return kApTls_Failed;
      }
      if (t->received)
        continue;  // handshake data the server already sent
    }
    for (;;) {
      if (t->received >= kInSize) {
        SetErr(err, err_size, "TLS handshake failed: message too large%s", NULL);
        return kApTls_Failed;
      }
      int r = recv(t->fd, t->incoming + t->received, kInSize - t->received, 0);
      if (r > 0) {
        t->received += r;
        break;
      }
      if (r == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
        SetErr(err, err_size, "The server closed the connection during the TLS handshake%s", NULL);
        return kApTls_Failed;
      }
      if (WaitSlice((intptr_t)t->fd, false, deadline, t->cancel) < 0) {
        SetErr(err, err_size, "The TLS handshake timed out%s", NULL);
        return kApTls_Cancelled;
      }
    }
  }
}

ApTls *ApTls_Connect(intptr_t fd, const char *host, int timeout_ms, SDL_atomic_t *cancel,
                     ApTlsResult *result, char *err, size_t err_size) {
  if (!LoadSchannel()) {
    SetErr(err, err_size, "wss:// needs Schannel (secur32.dll), which couldn't be loaded%s", NULL);
    *result = kApTls_NoLibrary;
    return NULL;
  }
  ApTls *t = calloc(1, sizeof(ApTls));
  t->fd = (SOCKET)fd;
  t->cancel = cancel;
  ApSchannelCred sc;
  memset(&sc, 0, sizeof(sc));
  sc.version = AP_SCHANNEL_CRED_VERSION;
  sc.protocols = AP_SP_PROT_TLS1_2_CLIENT;  // TLS 1.3 would need the newer SCH_CREDENTIALS
  sc.flags = AP_SCH_USE_STRONG_CRYPTO | AP_SCH_CRED_NO_DEFAULT_CREDS | AP_SCH_CRED_MANUAL_CRED_VALIDATION;
  if (g_sc.sspi->AcquireCredentialsHandleA(NULL, (char *)AP_UNISP_NAME, AP_SECPKG_CRED_OUTBOUND, NULL, &sc,
                                           NULL, NULL, &t->cred, NULL) != AP_SEC_E_OK) {
    SetErr(err, err_size, "Schannel refused to start a TLS connection%s", NULL);
    *result = kApTls_NoLibrary;
    free(t);
    return NULL;
  }
  t->have_cred = true;
  *result = Handshake(t, host, SDL_GetTicks() + timeout_ms, err, err_size);
  if (*result == kApTls_Ok &&
      g_sc.sspi->QueryContextAttributesA(&t->ctx, AP_SECPKG_ATTR_STREAM_SIZES, &t->sizes) != AP_SEC_E_OK) {
    SetErr(err, err_size, "TLS handshake failed: no stream sizes%s", NULL);
    *result = kApTls_Failed;
  }
  if (*result == kApTls_Ok)
    *result = CheckCertificate(t, host, err, err_size);
  if (*result != kApTls_Ok) {
    ApTls_Close(t);
    return NULL;
  }
  return t;
}

int ApTls_Send(ApTls *t, const void *data, int len) {
  int use = len < (int)t->sizes.max_message ? len : (int)t->sizes.max_message;
  if (use > 16384)
    use = 16384;
  char *buf = t->outgoing;
  ApSecBuffer b[4] = {
    { t->sizes.header, AP_SECBUFFER_STREAM_HEADER, buf },
    { (unsigned long)use, AP_SECBUFFER_DATA, buf + t->sizes.header },
    { t->sizes.trailer, AP_SECBUFFER_STREAM_TRAILER, buf + t->sizes.header + use },
    { 0, AP_SECBUFFER_EMPTY, NULL },
  };
  ApSecBufferDesc desc = { AP_SECBUFFER_VERSION, 4, b };
  memcpy(b[1].data, data, use);
  if (g_sc.sspi->EncryptMessage(&t->ctx, 0, &desc, 0) != AP_SEC_E_OK)
    return -1;
  return SendRaw(t, buf, (int)(b[0].size + b[1].size + b[2].size)) ? use : -1;
}

int ApTls_Recv(ApTls *t, void *data, int len) {
  for (;;) {
    if (t->dec_len) {
      int n = len < t->dec_len ? len : t->dec_len;
      memcpy(data, t->dec, n);
      t->dec += n, t->dec_len -= n;
      if (!t->dec_len) {  // the record is used up: keep only the ciphertext after it
        memmove(t->incoming, t->incoming + t->received - t->extra, t->extra);
        t->received = t->extra, t->extra = 0;
      }
      return n;
    }
    if (t->received && !t->need_more) {
      ApSecBuffer b[4] = { { (unsigned long)t->received, AP_SECBUFFER_DATA, t->incoming } };
      for (int i = 1; i < 4; i++)
        b[i].size = 0, b[i].type = AP_SECBUFFER_EMPTY, b[i].data = NULL;
      ApSecBufferDesc desc = { AP_SECBUFFER_VERSION, 4, b };
      LONG sec = g_sc.sspi->DecryptMessage(&t->ctx, &desc, 0, NULL);
      if (sec == AP_SEC_E_OK) {
        t->extra = 0;
        for (int i = 0; i < 4; i++) {
          if (b[i].type == AP_SECBUFFER_DATA)
            t->dec = b[i].data, t->dec_len = (int)b[i].size;
          else if (b[i].type == AP_SECBUFFER_EXTRA)
            t->extra = (int)b[i].size;
        }
        if (!t->dec_len) {
          memmove(t->incoming, t->incoming + t->received - t->extra, t->extra);
          t->received = t->extra, t->extra = 0;
        }
        continue;
      }
      if (sec != AP_SEC_E_INCOMPLETE_MESSAGE)
        return -1;  // close_notify (SEC_I_CONTEXT_EXPIRED), renegotiation or a broken record
      t->need_more = true;
    }
    if (t->received >= kInSize)
      return -1;
    int r = recv(t->fd, t->incoming + t->received, kInSize - t->received, 0);
    if (r > 0) {
      t->received += r;
      t->need_more = false;
      continue;
    }
    if (r < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
      return 0;
    return -1;
  }
}

bool ApTls_Pending(ApTls *t) {
  return t->dec_len > 0 || (t->received > 0 && !t->need_more);
}

void ApTls_Close(ApTls *t) {
  if (!t)
    return;
  if (t->have_ctx)
    g_sc.sspi->DeleteSecurityContext(&t->ctx);
  if (t->have_cred)
    g_sc.sspi->FreeCredentialsHandle(&t->cred);
  free(t);
}

#else
// ---------------------------------------------------------------------------
// OpenSSL 3 or 1.1. Only what the headers would give us is repeated here.

enum {
  kSslVerifyPeer = 1,
  kSslErrorWantRead = 2,
  kSslErrorWantWrite = 3,
  kSslCtrlSetTlsextHostname = 55,
  kSslCtrlMode = 33,
  kSslModePartialWriteMovingBuffer = 1 | 2,
  kSslCtrlSetMinProtoVersion = 123,
  kTls1_2Version = 0x0303,
};

static struct {
  bool tried, ok;
  const void *(*TLS_client_method)(void);
  void *(*SSL_CTX_new)(const void *method);
  void (*SSL_CTX_free)(void *ctx);
  int (*SSL_CTX_set_default_verify_paths)(void *ctx);
  int (*SSL_CTX_load_verify_locations)(void *ctx, const char *file, const char *dir);
  void (*SSL_CTX_set_verify)(void *ctx, int mode, void *callback);
  long (*SSL_CTX_ctrl)(void *ctx, int cmd, long larg, void *parg);
  void *(*SSL_new)(void *ctx);
  void (*SSL_free)(void *ssl);
  int (*SSL_set_fd)(void *ssl, int fd);
  long (*SSL_ctrl)(void *ssl, int cmd, long larg, void *parg);
  int (*SSL_set1_host)(void *ssl, const char *host);
  void *(*SSL_get0_param)(void *ssl);
  int (*SSL_connect)(void *ssl);
  int (*SSL_read)(void *ssl, void *buf, int num);
  int (*SSL_write)(void *ssl, const void *buf, int num);
  int (*SSL_pending)(const void *ssl);
  int (*SSL_get_error)(const void *ssl, int ret);
  int (*SSL_shutdown)(void *ssl);
  long (*SSL_get_verify_result)(const void *ssl);
  const char *(*X509_verify_cert_error_string)(long n);
  int (*X509_VERIFY_PARAM_set1_ip_asc)(void *param, const char *ip);
  void (*ERR_clear_error)(void);
  unsigned long (*ERR_get_error)(void);
  void (*ERR_error_string_n)(unsigned long e, char *buf, size_t len);
} o;

struct ApTls {
  int fd;
  void *ctx, *ssl;
  bool established;
};

static bool IsIpLiteral(const char *host) {
  unsigned char buf[16];
  return inet_pton(AF_INET, host, buf) == 1 || inet_pton(AF_INET6, host, buf) == 1;
}

static void *Sym(void *a, void *b, const char *name) {
  void *p = a ? SDL_LoadFunction(a, name) : NULL;
  return p ? p : b ? SDL_LoadFunction(b, name) : NULL;
}

static bool LoadSymbols(void *ssl, void *crypto) {
#define L(name) { void *p = Sym(ssl, crypto, #name); if (!p) return false; memcpy(&o.name, &p, sizeof(p)); }
  L(TLS_client_method) L(SSL_CTX_new) L(SSL_CTX_free) L(SSL_CTX_set_default_verify_paths)
  L(SSL_CTX_load_verify_locations) L(SSL_CTX_set_verify) L(SSL_CTX_ctrl) L(SSL_new) L(SSL_free)
  L(SSL_set_fd) L(SSL_ctrl) L(SSL_set1_host) L(SSL_get0_param) L(SSL_connect) L(SSL_read) L(SSL_write)
  L(SSL_pending) L(SSL_get_error) L(SSL_shutdown) L(SSL_get_verify_result)
  L(X509_verify_cert_error_string) L(X509_VERIFY_PARAM_set1_ip_asc)
  L(ERR_clear_error) L(ERR_get_error) L(ERR_error_string_n)
#undef L
  return true;
}

static bool LoadOpenSsl(void) {
  if (o.tried)
    return o.ok;
  o.tried = true;
  static const char *const kLibs[][2] = {
#ifdef __APPLE__
    // Never the unversioned system libssl: macOS aborts programs that load it.
    { "/opt/homebrew/opt/openssl@3/lib/libssl.3.dylib", "/opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib" },
    { "/usr/local/opt/openssl@3/lib/libssl.3.dylib", "/usr/local/opt/openssl@3/lib/libcrypto.3.dylib" },
    { "/opt/local/lib/libssl.3.dylib", "/opt/local/lib/libcrypto.3.dylib" },
    { "libssl.3.dylib", "libcrypto.3.dylib" },
#else
    { "libssl.so.3", "libcrypto.so.3" },
    { "libssl.so.1.1", "libcrypto.so.1.1" },
    { "libssl.so", "libcrypto.so" },
#endif
  };
  for (size_t i = 0; i < sizeof(kLibs) / sizeof(kLibs[0]); i++) {
    void *ssl = SDL_LoadObject(kLibs[i][0]);
    if (!ssl)
      continue;
    void *crypto = SDL_LoadObject(kLibs[i][1]);  // its symbols are usually reachable through libssl too
    if (LoadSymbols(ssl, crypto))
      return o.ok = true;
    if (crypto)
      SDL_UnloadObject(crypto);
    SDL_UnloadObject(ssl);
  }
  SDL_ClearError();
  return false;
}

// The usual CA bundles, for OpenSSL builds whose default paths miss them.
static void LoadSystemBundle(void *ctx) {
  static const char *const kBundles[] = {
    "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem",
    "/etc/pki/tls/cacert.pem", "/etc/ssl/cert.pem", "/usr/local/share/certs/ca-root-nss.crt",
    "/opt/homebrew/etc/openssl@3/cert.pem", "/usr/local/etc/openssl@3/cert.pem",
  };
  for (size_t i = 0; i < sizeof(kBundles) / sizeof(kBundles[0]); i++) {
    FILE *f = fopen(kBundles[i], "rb");
    if (f) {
      fclose(f);
      o.SSL_CTX_load_verify_locations(ctx, kBundles[i], NULL);
      return;
    }
  }
}

ApTls *ApTls_Connect(intptr_t fd, const char *host, int timeout_ms, SDL_atomic_t *cancel,
                     ApTlsResult *result, char *err, size_t err_size) {
  if (!LoadOpenSsl()) {
    SetErr(err, err_size, "wss:// needs OpenSSL (libssl 3 or 1.1), which isn't installed%s", NULL);
    *result = kApTls_NoLibrary;
    return NULL;
  }
  ApTls *t = calloc(1, sizeof(ApTls));
  t->fd = (int)fd;
  o.ERR_clear_error();
  t->ctx = o.SSL_CTX_new(o.TLS_client_method());
  if (!t->ctx) {
    SetErr(err, err_size, "OpenSSL couldn't start a TLS connection%s", NULL);
    *result = kApTls_NoLibrary;
    free(t);
    return NULL;
  }
  o.SSL_CTX_ctrl(t->ctx, kSslCtrlSetMinProtoVersion, kTls1_2Version, NULL);
  o.SSL_CTX_ctrl(t->ctx, kSslCtrlMode, kSslModePartialWriteMovingBuffer, NULL);
  o.SSL_CTX_set_default_verify_paths(t->ctx);
  LoadSystemBundle(t->ctx);
  const char *ca_file = getenv("ZELDA3_AP_CAFILE");
  if (ca_file && *ca_file)
    o.SSL_CTX_load_verify_locations(t->ctx, ca_file, NULL);
  o.SSL_CTX_set_verify(t->ctx, kSslVerifyPeer, NULL);
  t->ssl = o.SSL_new(t->ctx);
  o.SSL_set_fd(t->ssl, t->fd);
  if (IsIpLiteral(host)) {
    o.X509_VERIFY_PARAM_set1_ip_asc(o.SSL_get0_param(t->ssl), host);
  } else {
    o.SSL_ctrl(t->ssl, kSslCtrlSetTlsextHostname, 0, (void *)host);  // SNI
    o.SSL_set1_host(t->ssl, host);
  }
  o.ERR_clear_error();
  uint32_t deadline = SDL_GetTicks() + timeout_ms;
  for (;;) {
    int r = o.SSL_connect(t->ssl);
    if (r == 1) {
      t->established = true;
      *result = kApTls_Ok;
      return t;
    }
    int e = o.SSL_get_error(t->ssl, r);
    if (e != kSslErrorWantRead && e != kSslErrorWantWrite)
      break;
    if (WaitSlice(fd, e == kSslErrorWantWrite, deadline, cancel) < 0) {
      SetErr(err, err_size, "The TLS handshake timed out%s", NULL);
      *result = kApTls_Cancelled;
      ApTls_Close(t);
      return NULL;
    }
  }
  long verify = o.SSL_get_verify_result(t->ssl);
  if (verify != 0) {
    SetErr(err, err_size, "The server's certificate was rejected: %s", o.X509_verify_cert_error_string(verify));
    *result = kApTls_BadCert;
  } else {
    char why[160] = "the server doesn't speak TLS";
    unsigned long e = o.ERR_get_error();
    if (e)
      o.ERR_error_string_n(e, why, sizeof(why));
    SetErr(err, err_size, "TLS handshake failed: %s", why);
    *result = kApTls_Failed;
  }
  ApTls_Close(t);
  return NULL;
}

int ApTls_Send(ApTls *t, const void *data, int len) {
  o.ERR_clear_error();
  int r = o.SSL_write(t->ssl, data, len);
  if (r > 0)
    return r;
  int e = o.SSL_get_error(t->ssl, r);
  return e == kSslErrorWantRead || e == kSslErrorWantWrite ? 0 : -1;
}

int ApTls_Recv(ApTls *t, void *data, int len) {
  o.ERR_clear_error();
  int r = o.SSL_read(t->ssl, data, len);
  if (r > 0)
    return r;
  int e = o.SSL_get_error(t->ssl, r);
  return e == kSslErrorWantRead || e == kSslErrorWantWrite ? 0 : -1;
}

// Decrypted bytes only. SSL_has_pending is also true for the first part of a
// record, which can't be read before the rest arrives: then the caller has to
// wait for the socket, not try again at once (that spins). Without read_ahead
// OpenSSL never holds more than the record it is reading.
bool ApTls_Pending(ApTls *t) {
  return o.SSL_pending(t->ssl) > 0;
}

void ApTls_Close(ApTls *t) {
  if (!t)
    return;
  if (t->ssl) {
    if (t->established)
      o.SSL_shutdown(t->ssl);  // sends close_notify if the socket takes it; doesn't wait for the reply
    o.SSL_free(t->ssl);
  }
  if (t->ctx)
    o.SSL_CTX_free(t->ctx);
  o.ERR_clear_error();
  free(t);
}
#endif
