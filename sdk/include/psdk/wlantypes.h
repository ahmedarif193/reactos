#ifndef __WLANTYPES_H__
#define __WLANTYPES_H__

typedef enum _DOT11_BSS_TYPE {
    dot11_BSS_type_infrastructure = 1,
    dot11_BSS_type_independent,
    dot11_BSS_type_any
} DOT11_BSS_TYPE;

#define DOT11_SSID_MAX_LENGTH 32

typedef struct DOT11_ACCESSNETWORKOPTIONS {
    UINT8 AccessNetworkType;
    UINT8 Internet;
    UINT8 ASRA;
    UINT8 ESR;
    UINT8 UESA;
} DOT11_ACCESSNETWORKOPTIONS, *PDOT11_ACCESSNETWORKOPTIONS;

typedef struct DOT11_VENUEINFO {
    UINT8 VenueGroup;
    UINT8 VenueType;
} DOT11_VENUEINFO, *PDOT11_VENUEINFO;

#endif

