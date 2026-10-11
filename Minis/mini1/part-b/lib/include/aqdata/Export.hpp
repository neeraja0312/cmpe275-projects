#pragma once

// Symbol visibility. The library is compiled with -fvisibility=hidden, so only
// names marked AQ_API are visible outside it. That is what keeps the storage
// classes, loader and tokenizer private even in the shared-library build.

#define AQ_API __attribute__((visibility("default")))
