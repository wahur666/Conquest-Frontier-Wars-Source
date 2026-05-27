#include <iostream>

#include "DACOM.H"
#include "IProfileParser.h"
#include "TSmartPointer.h"
#include "json.hpp"
#include "vtdump.h"

ICOManager *DACOM;

struct DebugProbe {
    int id;
    float value;
};

int main() {
    VTDUMP_LOG("TestProject logger startup");

    nlohmann::json debug_state = {
            {"project", "TestProject"},
            {"stage", "logger smoke test"},
            {"enabled", true}
    };
    VTDUMP_LOG(debug_state);

    DebugProbe probe{42, 3.5f};
    VTDUMP_LOG(probe);
    VTDUMP_LOG_BYTES(&probe, sizeof(probe));

    DACOM = DACOM_Acquire();
    DACOM->SetINIConfig("[Libraries]\r\nDOSFile.dll", DACOM_INI_STRING);
    COMPTR<IProfileParser> IPP;
    auto res = DACOM->QueryInterface( IID_IProfileParser, IPP.void_addr() );
    IPP->CreateSection("Library");

    std::cout << "Hello, World!" << res << std::endl;
    return 0;
}
