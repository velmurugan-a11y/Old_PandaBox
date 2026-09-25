/*
 * version.h - version string reported in LxBoxInfo.
 *
 * V2.89 reports "2.4,250502,2.891,260827".  Per decision D4 (see the rebuild plan) we keep the
 * same format but our own build number so a field box can tell the firmwares apart. Set
 * FW_MATCH_289 to 1 to report Leo's exact V2.89 string instead (byte-for-byte parity testing).
 */
#ifndef VERSION_H
#define VERSION_H

#define FW_MATCH_289   0

#define HW_VER_STR     "2.4"
#define HW_DATE_STR    "250502"

#if FW_MATCH_289
#define FW_VER_STR     "2.891"
#define FW_DATE_STR    "260827"
#else
#define FW_VER_STR     "2.901"
#define FW_DATE_STR    "260925"
#endif

/* LxBoxInfo = "<HW>,<HWDATE>,<FW>,<FWDATE>,<IMEI>,LCR" */
#define BOXINFO_PREFIX HW_VER_STR "," HW_DATE_STR "," FW_VER_STR "," FW_DATE_STR ","

#endif
