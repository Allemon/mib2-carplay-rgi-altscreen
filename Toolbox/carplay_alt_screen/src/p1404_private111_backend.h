/* p1404_private111_backend.h - exact P1404 backend installation seam. */
#ifndef P1404_PRIVATE111_BACKEND_H
#define P1404_PRIVATE111_BACKEND_H

/*
 * Returns 1 only when every private-111 prerequisite is implemented and proved
 * for MHI2Q_CN_AUG22_P1404.
 *
 * Current source already reflects these candidate semantics but MUST stay
 * fail-closed until the exact target proofs in Tasks/AltScreen_Local_Work pass:
 *
 *   A) receiver+0x1c8 is the live 16-byte session/master AES key used by
 *      AirPlay_DeriveAESKeySHA512ForScreen, including writer/lifetime and derive
 *      failure semantics;
 *   B) [[receiver+0xc]+0x1d0] is the exact screenStreamOptions object accepted by
 *      ScreenStreamCreate for private111, including selector and NULL semantics;
 *   C) every raw CF/CFL helper call used by /info/SETUP mutation has exact P1404
 *      arity/register/status/ownership proof;
 *   D) same-session setUpStreams/tearDownStreams ordering is proved serialized,
 *      or the orchestration has an evidence-backed cancellation/generation state
 *      machine that prevents an advertised 111 response without a live committed
 *      private pair.
 *
 * Other review fixes are intentional: stock111 is stripped before stock even
 * while this backend is NOT_READY, duplicate reuse requires the same connection
 * ID as the live AES context, and formal G5B remains NOT_IMPLEMENTED.
 */
int p1404_private111_backend_install(void);

#endif
