#include "smu_common.h"

const char* get_code_name(const smu_processor_codename codename) {
    switch (codename) {
        case CODENAME_COLFAX: return "Colfax";
        case CODENAME_RENOIR: return "Renoir";
        case CODENAME_PICASSO: return "Picasso";
        case CODENAME_MATISSE: return "Matisse";
        case CODENAME_THREADRIPPER: return "ThreadRipper";
        case CODENAME_CASTLEPEAK: return "CastelPeak";
        case CODENAME_RAVENRIDGE: return "RavenRidge";
        case CODENAME_RAVENRIDGE2: return "RavenRidge2";
        case CODENAME_SUMMITRIDGE: return "SummitRidge";
        case CODENAME_PINNACLERIDGE: return "PinnacleRidge";
        case CODENAME_REMBRANDT: return "Rembrandt";
        case CODENAME_VERMEER: return "Vermeer";
        case CODENAME_VANGOGH: return "VanGogh";
        case CODENAME_CEZANNE: return "Cezanne";
        case CODENAME_MILAN: return "Milan";
        case CODENAME_DALI: return "Dali";
        case CODENAME_LUCIENNE: return "Lucienne";
        case CODENAME_NAPLES: return "Naples";
        case CODENAME_CHAGALL: return "Chagall";
        case CODENAME_RAPHAEL: return "Raphael";
        case CODENAME_GRANITERIDGE: return "GraniteRidge";
        case CODENAME_STORMPEAK: return "StormPeak";
        case CODENAME_PHOENIX: return "Phoenix";
        case CODENAME_STRIXPOINT: return "StrixPoint";
        case CODENAME_HAWKPOINT: return "HawkPoint";
        default: return "Unknown";
    }
}
