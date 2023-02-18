/**
 * @file
 * @brief branch predictor helper
 */

#ifndef WS_CTUBE_LIKELY_H
#define WS_CTUBE_LIKELY_H

#define ws_ctube_likely(x) __builtin_expect(!!(x), 1)
#define ws_ctube_unlikely(x) __builtin_expect(!!(x), 0)

#endif /* WS_CTUBE_LIKELY_H */
