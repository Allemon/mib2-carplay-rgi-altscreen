/*
 * p1404_iap2.h - Gate 1: iAP2 IdentificationInformation -> Theme Assets, plus
 * complete bounded iAP2 control-plane evidence capture.
 *
 * Framing model taken from capture-verified open-source iAP2 implementations:
 *   link header  : >HHBBBB  start=0xFF5A, length, control, seq, ack, session, +1 cksum
 *   link cksum   : (-sum(bytes)) & 0xff, computed separately for header and payload
 *   CSM header   : >HHH     start=0x4040, length (whole CSM incl. 6B header), msg id
 *   CSM param    : >HH      length (incl. header), param id
 * IdentificationInformation is CSM 0x1D01. 0xEA00/0xEA01 are EAP session
 * control messages and are explicitly excluded from mutation. The phone answers
 * with 0x4300 CarPlayAvailability.
 *
 * 0x4300 has its own schema:
 *   param0 WiredAttributes       sub0 Available(bool) sub1 USBTransportId
 *   param1 WirelessAttributes    sub0 Available(bool) sub1 BluetoothTransportId
 *   param2 ThemeAssetsAttributes sub0 Available(bool)
 * G1B therefore always reads param2/sub0 independently of the outgoing profile.
 */
#ifndef P1404_IAP2_H
#define P1404_IAP2_H
#include <stddef.h>
#include <stdint.h>

#define IAP2_LINK_START      0xff5au
#define IAP2_LINK_HDR_LEN    9u
#define IAP2_CSM_START       0x4040u
#define IAP2_CSM_HDR_LEN     6u
/* Existing parent growth is 4 bytes. Missing modern parent21 may clone bounded
 * stock sub0/sub1 identity and needs at most 82 bytes; reserve 96 throughout. */
#define IAP2_GROW            96u
/* Keep protocol policy, initialized assembler storage, patch scratch, wire
 * output and evidence truncation separate.  A declared link length is 16-bit;
 * frames above MUTATABLE_FRAME_MAX are valid pass-through traffic. */
#define IAP2_MUTATABLE_FRAME_MAX 1024u
#define IAP2_PATCH_SCRATCH_MAX   (IAP2_MUTATABLE_FRAME_MAX + IAP2_GROW + 1u)
#define IAP2_ASSEMBLER_MAX       4096u
#define IAP2_TX_MAX              6144u
#define IAP2_DUMP_MAX            1024u

#define CP_AVAIL_WIRED_PARAM        0u
#define CP_AVAIL_WIRELESS_PARAM     1u
#define CP_AVAIL_THEME_PARAM        2u
#define CP_AVAIL_AVAILABLE_SUB      0u
#define CP_AVAIL_TRANSPORT_ID_SUB   1u
#define CP_AVAIL_BOOL_LEN           1u

#define CP_AVAIL_YES          1
#define CP_AVAIL_NO           0
#define CP_AVAIL_UNKNOWN    (-1)
#define CP_AVAIL_MALFORMED  (-2)

uint8_t iap2_link_checksum(const uint8_t *p, size_t n);
int     iap2_link_checksum_ok(const uint8_t *p, size_t n);
long    iap2_find_csm(const uint8_t *buf, size_t n, size_t from, uint16_t *msg_id);
int     iap2_csm_params_consistent(const uint8_t *body, size_t blen);
int     iap2_scan_and_patch(uint8_t *buf, size_t n, size_t capacity);
int     iap2_scan_incoming(const uint8_t *buf, size_t n);
int     iap2_observe_outgoing(const uint8_t *buf, size_t n);
int     iap2_patch_outgoing(const uint8_t *src, size_t n, uint8_t *dst, size_t cap);
int     iap2_parse_theme_availability(const uint8_t *body, size_t blen);
int     iap2_parse_attr_available(const uint8_t *body, size_t blen, uint16_t param,
                                  const char *label);
/* Raw hex is retained only as an explicit developer helper. Runtime control
 * observation logs metadata and allowlisted availability fields, never bodies. */
int     iap2_control_fd_note_outgoing(int fd, const uint8_t *buf, size_t n);
int     iap2_control_fd_note_outgoing_gen(int fd, uint32_t generation,
                                          const uint8_t *buf, size_t n);
/* Read-only preflight used before reserving scarce mutation state. */
int     iap2_mutation_buffer_candidate(const uint8_t *buf, size_t n);
int     iap2_control_fd_mutation_candidate(int fd, const uint8_t *buf, size_t n);
int     iap2_control_fd_mutation_candidate_gen(int fd, uint32_t generation,
                                                const uint8_t *buf, size_t n);
/* Mutation assembler result: PASS sends the caller buffer unchanged, BUFFERED
 * means the source bytes were accepted pending a complete frame, and OUTPUT
 * supplies assembled (and, where applicable, patched) wire bytes. OUTPUT keeps
 * a mutation transaction open: the actual wrapper must commit after positive
 * wire progress or cancel after zero progress, while it still owns the TX slot. */
#define IAP2_MUTATE_ERROR    (-1)
#define IAP2_MUTATE_PASS       0
#define IAP2_MUTATE_BUFFERED   1
#define IAP2_MUTATE_OUTPUT     2
int     iap2_control_fd_prepare_mutation(int fd, const uint8_t *buf, size_t n,
                                         uint8_t *out, size_t out_cap,
                                         size_t *out_len, int *had_pending);
int     iap2_control_fd_prepare_mutation_gen(int fd, uint32_t generation,
                                             const uint8_t *buf, size_t n,
                                             uint8_t *out, size_t out_cap,
                                             size_t *out_len, int *had_pending);
/* Promote already acknowledged assembler bytes to unchanged wire output.  The
 * returned transaction must be committed after positive progress or cancelled
 * after zero progress while bearer ownership is still held. */
int     iap2_control_fd_prepare_drain_gen(int fd, uint32_t generation,
                                          uint8_t *out, size_t out_cap,
                                          size_t *out_len);
int     iap2_control_fd_commit_mutation(int fd);
int     iap2_control_fd_commit_mutation_gen(int fd, uint32_t generation);
int     iap2_control_fd_cancel_mutation(int fd);
int     iap2_control_fd_cancel_mutation_gen(int fd, uint32_t generation);
int     iap2_control_fd_feed_incoming(int fd, const uint8_t *buf, size_t n);
int     iap2_control_fd_feed_incoming_gen(int fd, uint32_t generation,
                                          const uint8_t *buf, size_t n);
void    iap2_control_fd_peer_eof_gen(int fd, uint32_t generation);
void    iap2_control_fd_close(int fd);
void    iap2_control_fd_close_gen(int fd, uint32_t generation);
void    iap2_control_fd_reset(void);
void    iap2_dump_hex(const char *tag, const uint8_t *buf, size_t n);
int     iap2_dump_frame(const char *tag, const uint8_t *buf, size_t n);
#endif
