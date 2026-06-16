// Default expansion: real enums

#if !defined(RANDO_ENUM_BEGIN) && !defined(RANDO_ENUM_ITEM) && !defined(RANDO_ENUM_END)
// clang-format off
#define RANDO_ENUM_BEGIN(EnumName) typedef enum EnumName {
#define RANDO_ENUM_ITEM_1(name) name,
#define RANDO_ENUM_ITEM_2(name, value) name = value,
#define RANDO_ENUM_GET_ITEM_MACRO(_1, _2, NAME, ...) NAME
#define RANDO_ENUM_ITEM(...) RANDO_ENUM_GET_ITEM_MACRO(__VA_ARGS__, RANDO_ENUM_ITEM_2, RANDO_ENUM_ITEM_1)(__VA_ARGS__)
#define RANDO_ENUM_END(EnumName) } EnumName;
#define RANDO_ENUM__CLEANUP
// clang-format on
#endif

#include "randomizerEnums/LogicVal.h"
#include "randomizerEnums/RandomizerCheck.h"
#include "randomizerEnums/RandomizerGet.h"
#include "randomizerEnums/RandomizerHintTextKey.h"
#include "randomizerEnums/RandomizerInf.h"
#include "randomizerEnums/RandomizerMiscEnums.h"
#include "randomizerEnums/RandomizerOptions.h"
#include "randomizerEnums/RandomizerRegion.h"
#include "randomizerEnums/RandomizerSettingKey.h"
#include "randomizerEnums/RandomizerTrick.h"

// Clean up only the defaults we defined.
#ifdef RANDO_ENUM__CLEANUP
#undef RANDO_ENUM_BEGIN
#undef RANDO_ENUM_ITEM
#undef RANDO_ENUM_ITEM_1
#undef RANDO_ENUM_ITEM_2
#undef RANDO_ENUM_GET_ITEM_MACRO
#undef RANDO_ENUM_END
#undef RANDO_ENUM__CLEANUP
#endif
